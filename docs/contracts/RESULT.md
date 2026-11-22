status: frozen

# Result 契约

定义在 `src/core/result/`（`Enums.h`、`Results.h`、`Results.cpp`），不依赖 JUCE。测试：`tests/contracts/result_test.cpp`。

## 形状

每项结果是 `Result<Measures, Verdict>`，四块：

| 块 | 内容 | 规则 |
|---|---|---|
| `measures` | 连续量：dB、Hz、秒、次数、比值 | double 精度，只放数值和少量标志。`isMeasures` 静态断言只能查可平凡复制和标准布局，成员是否全是数值靠评审和 golden；**golden 只锁这一块** |
| `verdict` | 档位判定 | 由 measures 加 `thresholds.json` 的阈值得出；改阈值只动 verdict，不动 measures 与 golden |
| `confidence` | `Reliability`（`trusted` 或 `retest`）加 `RetestReason` | `trusted` 时原因必为 `none`；`retest` 的原因不会是 `none`（构造时 `none` 换成 `ambiguous`），`isConsistent` 检查这两条 |
| `evidence` | `EvidenceMark` 列表：时间段、频率、电平 | 给证据波形与详情用；时间是相对该步开始的采样计时 |

`AllResults` 是 Session 持有的全部结果，没做过的步骤是空。过期由步骤状态（`StepStatus::Stale`）表达，结果本身不改。
分析函数的形状是 `analyseX(samples, sr, ctx) -> XResult`，纯函数。

## 各项结果

“进最该改”一栏：无信号与数字削波固定在前；其余按 影响 × 置信度 ÷ 修改代价 排序，低置信不进候选。影响与代价的表在 `thresholds.json` 的 `priority` 分组；过期的结果也不进候选；排好的顺序在桥快照的 `fixes`（见 `BRIDGE.md`）。

| 结果 | 档位字段 | 要点 | 进最该改 |
|---|---|---|---|
| `noSignal` | `state`：等你出声、信号正常、没有回调、从一开始全零、之前有信号后来全零、极低不上升 | 前三种没收到音频红（主按钮禁用），极低琥珀（主按钮可点）；文案一律写“可能”。“没有回调”时没有样本，由编辑器的定时器按墙钟判断，秒数在桥快照的 `connect.callbackAgeSec`，不进 `measures`，golden 因此不依赖时序 | 固定在前 |
| `channels` | `kind`、`statusL/R`、`analysedChannel` | 只在立体声轨上做；五种提示都琥珀，不拦测试；分析哪一路由 `analysedChannel` 给出，不把两路相加 | 否 |
| `gain` | `band`、`digitalClip`、`maybeFlatTop` | **数字削波次数与模拟平顶处数分开计数**；数字削波红并点亮削波灯；平顶只到琥珀“可能削波”，不点灯、不单独变红；样本峰值低于 `gain.flat_min_peak_dbfs` 的声道不查平顶（处数为 0） | 数字削波固定在前，其余按排序 |
| `noise` | `band`（A 加权 SNR 唯一分档依据） | 绝对 dBFS 只作信息，不分档 | 按排序 |
| `hum` | `kind`（hum、buzz、whistle）、`slight` | 频率、高出本底 dB、占噪声能量 %。严重度看 `overFloorDb`（最强谐波高出周围本底多少）与 `levelDbfs`（电流声电平）：前者低于 `hum.slight_below_over_floor_db`、或后者低于 `hum.slight_below_dbfs`，仍报，标“轻微”；占比只作信息，不定严重度。啸叫只查 2 kHz 到 `hum.whistle_max_hz`、自身功率不低于 `hum.whistle_min_dbfs` 的窄峰 | 轻微不进 |
| `deviceProcessing` | `detected` | 只作横幅；极低不上升态下不显示 | 否 |
| `reverb` | `band`（`gated`） | 拍手算。T20 门限只作废那一下的那个频带，`gated` 表示作废太多、主指标给不出；给不出值的频带 `t20Sec` 为 0。房间建议只看它：干、正常不提，偏混与很混给建议（`src/core/decide`） | 偏混、很混按排序 |
| `directRatio` | 无（`lowerBoundOnly`） | 拍手算，只用没削波的几下，尾部测不到时只报下限（真值只会更高）。**不分档**：拍手相对扫频真值的偏移随房间与位置变化，绝对档位判不准，只用于重测位置之后的“上次 → 现在” | 否 |
| `plosive`、`sibilance`、`click` | `severity`（`lowSensitivity`、`highBandDisabled`） | **三项都不分档**，`severity` 停在 `light` 表示未分档，不表示良好。喷麦取一遍字表的最大值，齿音给固定增益下的绝对电平，两者只供重测位置之后的“上次 → 现在”；口水音只作“可能有口水音”的提示。三项另有参考值（经验值）与 `overReference`，见下文“参考值与推荐距离” | 否 |
| `direction` | `kind` | 只回答“当前方向是不是最响”；亮度只显示、不参与判断；放反要重测那一对确认；没测过不算问题 | 两次的差都 ≥10 dB 才进 |
| `distance` | `kind`（`drSign`、`plosiveSign`、`sibilanceSign`） | 由别的结果合成（`src/core/decide`），不给绝对判断，置信度恒为可信、不确定写在 `kind` 里。第一次测试只有 `maybe-too-far` 这条弱提示（当前、可信的噪声档位为差，且有当前、可信的 T20）；重测位置之后按 D/R、喷麦、齿音的“上次 → 现在”给 `closer`、`farther`、`no-change`，方向不一致为 `conflicting`。阈值在 `thresholds.json` 的 `distance` 分组，规则见下文 | 否 |

### 距离标尺的前后对比

- 读数：D/R 取 `drDb`；喷麦取 `worstRatioDb`；齿音取 `highDb`，两次里有一次 `highBandDisabled` 时两次都取 `midHighDb`。
  上一次是 `previous` 里的那一项（被这一次位置步换下来的），有一边要再测的那项不比。
- 一项“现在减上次”超过它的分辨力（`distance.dr_resolution_db`、`plosive_resolution_db`、`sibilance_resolution_db`）算一个方向的迹象，
  值越大越近；不超过算没有明显变化。D/R 只知道下限的一边真值只会更高：上次是下限时“更近”说不出，这次是下限时“更远”说不出，
  定不了的记 `not-compared`。
- 两次位置步之间（含其中一次进行中）报过调增益或声道布局变了：齿音记 `gain-changed`，不比；报过换房间：三项都记 `room-changed`。
- 三项一项都比不了时，回到第一次的规则：有弱提示就是 `maybe-too-far`，否则 `unknown`。
- `empiricalLimitM` 是按 T20 估的经验距离上限：`limit_critical_frac × critical_distance_coef × √(source_directivity × room_volume_m3 / T20)`，
  房间体积与指向性取典型值，是经验值；没有当前、可信的 T20 时为 0。距离标尺不显示厘米，这个数只供提示的文案使用。
- 距离的建议只由 `kind` 决定：`maybe-too-far` 给 `move-closer`，其余不给。口水音的“退后一点”“喝点水”跟着口水音走，不占距离这一格。
- 距离不进“最该改”：第一次只有弱提示，重测后的前后对比是报告，不当作要先改的问题。

### 参考值与推荐距离

参考值是经验值，不是档位：卡片照样无色，超过时只在这张卡上给一条轻提示（`advice.plosive`、`advice.sibilance`、`advice.click`），
不进“最该改”，也不作距离证据。只看当前、可信的结果。

| 项 | 比的是什么 | 参考 | 超过时 |
|---|---|---|---|
| 喷麦 | `worstRatioDb`（每遍字表最响音节的瞬态比，取最大） | `plosive.reference_max_db`（9 dB 以下） | `overReference`；建议 `pop-filter-or-back-off`：加防喷罩，或在推荐距离范围内退后一点再测 |
| 齿音 | `highOverVowelDb`（6-12 kHz 对紧随其后的元音 200-4000 Hz，有元音参考的擦音取中位数，`vowelEvents` 是这样的擦音个数） | `sibilance.reference_max_db`（−3 dB 以下） | `overReference`；建议 `angle-soften-de-ess`：话筒偏开嘴的正前方 15-30°（朝下巴或嘴角），s 念轻一点，录完用齿音消除。不建议退后（挪远后比值反而变大），不说再测对比 |
| 口水音 | `perMinute` | `click.reference_max_per_min`（还没有值） | `overReference`（参考值定下来之前恒为 `false`）；建议 `back-off-a-little`：在推荐距离范围内退后一点；上一次位置步（`previous.click`，可信）也超过、这次仍超过时 `drink-water` |

- 齿音：高频项关闭（`highBandDisabled`，有效带宽不足 10 kHz）或 `vowelEvents` 为 0 时不比，`overReference` 为 `false`，参考值不显示。
  对元音的比值随距离的方向不稳（近讲效应），只作参考；前后对比仍只用固定增益下的绝对电平。
- 推荐距离（快照根上的 `recommendedDistance`）：静态的常见做法，不是测量，按接通步选的话筒类型给。电容、USB、不确定：约 15 cm，
  范围 10-30 cm；动圈：约 5-10 cm；耳机麦不给（`null`）。数值在 `thresholds.json` 的 `recommended_distance` 分组。
  喷麦、口水音的“退后一点”都以这个范围为界。

## 色调

`toneOf(...)` 是档位到颜色的固定映射（`Results.cpp`），不读阈值。无信号三种红、极低琥珀；声道五种琥珀、真立体声绿；
增益 偏低琥珀、过低红、偏高琥珀、过高红、数字削波红（平顶最多把绿抬到琥珀）；噪声 差红、一般琥珀、好绿；电流声 hum 和 buzz 琥珀、啸叫红、标了轻微的一律琥珀；
T20 干和正常绿、偏混琥珀、很混红，`gated`（门限作废太多、给不出）时无色；D/R 不分档，无色；喷麦、齿音、口水音 轻绿、中琥珀、重红（这是 `toneOf(Severity)` 的映射；三项现在都不分档，卡片无色，见本节下文）；方向 最响和差别不大绿、可能放反红、不确定无色。距离没有色调。`worse(a, b)` 取更严重者。

再测（`confidence.level` 为 `retest`）的结果没有结论，卡片无色：卡片颜色取 `cardTone(result)`，它在再测时给无色、可信时才用 `toneOf`；
`toneOf` 只是档位到颜色的映射，不看置信度。
喷麦、齿音、口水音现在都不分档：`severity` 停在最低档只表示“未分档”，不表示良好，所以卡片无色。`toneOf` 的映射不变，无色由 Session 在快照里处理（见 `BRIDGE.md`）。
电平管上的标记另有规则：电流声检出就红，不看 `tone`；底噪和口水音跟结果档位。电平管要的绝对电平是 `noiseDbfs`、`noisePeakDbfs`、`hum.levelDbfs`、`hum.peakDbfs`、`click.loudestDbfs`。

## 过期规则（Session 实现，这里只记契约）

调增益：增益、噪声、电流声、齿音、距离（齿音给的是固定增益下的绝对电平，增益一变 A/B 比较就失效；距离的弱提示要用 SNR）；挪位置：增益、噪声、D/R、喷麦、齿音、口水音、方向、距离；换房间：全部；
声道布局变了：增益、噪声、电流声、距离，同时撤掉声道提示。采样率变化：当前步作废（`invalid`，原因 `rate-changed`），旧结果标过期。
距离每完成一步都重算；重算后它用到的 D/R、喷麦、齿音有一项过期，或这一步进行中报过让距离过期的改动，距离就仍是过期的。

## 变更

字段与枚举只追加；改名、删除、改含义要同步改 `docs/contracts/BRIDGE.md` 与桥的两份名表，并走契约变更规则。
