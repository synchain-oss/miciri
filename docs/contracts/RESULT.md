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
| `gain` | `band`、`digitalClip`、`maybeFlatTop` | **数字削波次数与模拟平顶处数分开计数**；数字削波红并点亮削波灯；平顶只到琥珀“可能削波”，不点灯、不单独变红 | 数字削波固定在前，其余按排序 |
| `noise` | `band`（A 加权 SNR 唯一分档依据） | 绝对 dBFS 只作信息，不分档 | 按排序 |
| `hum` | `kind`（hum、buzz、whistle）、`slight` | 频率、高出本底 dB、占噪声能量 %；占比低于阈值仍报，标“轻微” | 轻微不进 |
| `deviceProcessing` | `detected` | 只作横幅；极低不上升态下不显示 | 否 |
| `reverb`、`directRatio` | `band`（`gated`、`lowerBoundOnly`） | 拍手算。T20 门限只作废那一下的那个频带，`gated` 表示作废太多、主指标给不出；给不出值的频带 `t20Sec` 为 0。D/R 只用没削波的几下，尾部测不到时只报下限 | 按决策表（`src/core/decide`） |
| `plosive`、`sibilance`、`click` | `severity`（`lowSensitivity`、`highBandDisabled`） | 喷麦按一遍字表的最大值分档；齿音只给绝对电平供 A/B、口水音只作提示，这两项不分档，`severity` 停在 `light` 表示未分档，不表示良好；档位来自实录 | 按排序 |
| `direction` | `kind` | 只回答“当前方向是不是最响”；亮度只显示、不参与判断；放反要重测那一对确认；没测过不算问题 | 两次的差都 ≥10 dB 才进 |
| `distance` | `kind` | 由别的结果的证据合成（`src/core/decide`），没有自己的阈值；只有弱证据时置信度为再测 | 按排序 |

喷麦、齿音、口水音的算法后做：在算法落地前不得产生这些结果，字段名与枚举先冻结，让界面与桥可以并行开发。

## 色调

`toneOf(...)` 是档位到颜色的固定映射（`Results.cpp`），不读阈值。无信号三种红、极低琥珀；声道五种琥珀、真立体声绿；
增益 偏低琥珀、过低红、偏高琥珀、过高红、数字削波红（平顶最多把绿抬到琥珀）；噪声 差红、一般琥珀、好绿；电流声 hum 和 buzz 琥珀、啸叫红、标了轻微的一律琥珀；
T20 干和正常绿、偏混琥珀、很混红，`gated`（门限作废太多、给不出）时无色；D/R 低红、中琥珀、高绿；喷麦、齿音、口水音 轻绿、中琥珀、重红（这是 `toneOf` 的映射；齿音、口水音未分档时卡片无色，见本节下文）；方向 最响和差别不大绿、可能放反红、不确定无色。距离没有色调。`worse(a, b)` 取更严重者。

再测（`confidence.level` 为 `retest`）的结果没有结论，卡片无色：卡片颜色取 `cardTone(result)`，它在再测时给无色、可信时才用 `toneOf`；
`toneOf` 只是档位到颜色的映射，不看置信度。
齿音、口水音现在不分档：`severity` 停在最低档只表示“未分档”，不表示良好，所以卡片无色。`toneOf` 的映射不变，无色由 Session 在快照里处理（见 `BRIDGE.md`）；距离标尺也不把这两项当作证据。
电平管上的标记另有规则：电流声检出就红，不看 `tone`；底噪和口水音跟结果档位。电平管要的绝对电平是 `noiseDbfs`、`noisePeakDbfs`、`hum.levelDbfs`、`hum.peakDbfs`、`click.loudestDbfs`。

## 过期规则（Session 实现，这里只记契约）

调增益：增益、噪声、电流声、齿音（齿音给的是固定增益下的绝对电平，增益一变 A/B 比较就失效）；挪位置：增益、噪声、D/R、喷麦、齿音、口水音、方向、距离；换房间：全部；
声道布局变了：增益、噪声、电流声，同时撤掉声道提示。采样率变化：当前步作废（`invalid`，原因 `rate-changed`），旧结果标过期。

## 变更

字段与枚举只追加；改名、删除、改含义要同步改 `docs/contracts/BRIDGE.md` 与桥的两份名表，并走契约变更规则。
