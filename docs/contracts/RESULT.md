status: frozen

# Result 契约

定义在 `src/core/result/`（`Enums.h`、`Results.h`、`Results.cpp`），不依赖 JUCE。测试：`tests/contracts/result_test.cpp`。

## 形状

每项结果是 `Result<Measures, Verdict>`，四块：

| 块 | 内容 | 规则 |
|---|---|---|
| `measures` | 连续量：dB、Hz、秒、次数、比值 | double 精度，只含算术类型（`isMeasures` 静态断言）；**golden 只锁这一块** |
| `verdict` | 档位判定 | 由 measures 加 `thresholds.json` 的阈值得出；改阈值只动 verdict，不动 measures 与 golden |
| `confidence` | `Reliability`（`trusted` 或 `retest`）加 `RetestReason` | `trusted` 时原因必为 `none`；`retest` 的原因不会是 `none`（构造时 `none` 换成 `ambiguous`），`isConsistent` 检查这两条 |
| `evidence` | `EvidenceMark` 列表：时间段、频率、电平 | 给证据波形与详情用；时间是相对该步开始的采样计时 |

`AllResults` 是 Session 持有的全部结果，没做过的步骤是空。过期由步骤状态（`StepStatus::Stale`）表达，结果本身不改。
分析函数的形状是 `analyseX(samples, sr, ctx) -> XResult`，纯函数。

## 各项结果

“进最该改”一栏：无信号与数字削波固定在前；其余按 影响 × 置信度 ÷ 修改代价 排序，低置信不进候选。

| 结果 | 档位字段 | 要点 | 进最该改 |
|---|---|---|---|
| `noSignal` | `state`：等你出声、信号正常、没有回调、从一开始全零、之前有信号后来全零、极低不上升 | 前三种没收到音频红（主按钮禁用），极低琥珀（主按钮可点）；文案一律写“可能” | 固定在前 |
| `channels` | `kind`、`statusL/R`、`analysedChannel` | 只在立体声轨上做；五种提示都琥珀，不拦测试；分析哪一路由 `analysedChannel` 给出，不把两路相加 | 否 |
| `gain` | `band`、`digitalClip`、`maybeFlatTop` | **数字削波次数与模拟平顶处数分开计数**；数字削波红并点亮削波灯；平顶只到琥珀“可能削波”，不点灯、不单独变红 | 数字削波固定在前，其余按排序 |
| `noise` | `band`（A 加权 SNR 唯一分档依据） | 绝对 dBFS 只作信息，不分档 | 按排序 |
| `hum` | `kind`（hum、buzz、whistle）、`slight` | 频率、高出本底 dB、占噪声能量 %；占比低于阈值仍报，标“轻微” | 轻微不进 |
| `deviceProcessing` | `detected` | 只作横幅；极低不上升态下不显示 | 否 |
| `reverb`、`directRatio` | `band`（`gated`、`lowerBoundOnly`） | 算法未实现，先定字段 | 按决策表 |
| `plosive`、`sibilance`、`click` | `severity`（`lowSensitivity`、`highBandDisabled`） | 算法未实现，先定字段；档位来自实录 | 按排序 |
| `direction` | `kind` | 只回答“当前方向是不是最响最亮”；没测过不算问题 | 差 ≥10 dB 才进 |
| `distance` | `kind` | 由别的结果的证据合成，没有自己的 measures 之外的阈值 | 按排序 |

房间、位置、喷麦、齿音、口水音、方向、距离的算法后做：在算法落地前不得产生这些结果，字段名与枚举先冻结，让界面与桥可以并行开发。

## 色调

`toneOf(...)` 是档位到颜色的固定映射（`Results.cpp`），不读阈值。无信号三种红、极低琥珀；声道五种琥珀、真立体声绿；
增益 偏低琥珀、过低红、偏高琥珀、过高红、数字削波红（平顶最多把绿抬到琥珀）；噪声 差红、一般琥珀、好绿；电流声检出红、轻微琥珀；
T20 偏混琥珀、很混红；D/R 低红、中琥珀、高绿；喷麦、齿音、口水音 轻绿、中琥珀、重红。`worse(a, b)` 取更严重者。

## 过期规则（Session 实现，这里只记契约）

调增益：增益、噪声、电流声；挪位置：增益、噪声、D/R、喷麦、齿音、口水音、方向、距离；换房间：全部；
声道布局变了：增益、噪声、电流声，同时撤掉声道提示。采样率变化：当前步作废（`invalid`，原因 `rate-changed`），旧结果标过期。

## 变更

字段与枚举只追加；改名、删除、改含义要同步改 `docs/contracts/BRIDGE.md` 与桥的两份名表，并走契约变更规则。
