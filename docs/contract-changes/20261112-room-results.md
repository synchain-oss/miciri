<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# 房间、位置、方向、距离的结果落地

房间 T20、位置 D/R、方向与距离标尺的算法实现后，这几项结果开始产生。字段与枚举不增不删、不改名，桥的名表不动；
改的是两个字段的含义、零值的约定，以及阈值表里的一个键。

## Result

| 字段 | 以前 | 现在 |
|---|---|---|
| `reverb.gated` | 任何一下尾部出现门限，整组作废 | 门限只作废那一下的那个频带；本标志表示作废太多、500 Hz 或 1 kHz 留不下足够的几下，主指标给不出（再测原因 `gate-detected`）。色调规则不变：为真时无色 |
| `reverb.repeatMaxOverMin` | 没有定义口径 | 每个频带去掉偏离中位数的几下之后，留下的最大比最小，取最差的频带 |
| `reverb.t20Sec[i]` | 没有约定 | 该频带给不出值时为 0；`tMidSec` 要 500 Hz 与 1 kHz 都有值，否则为 0 |
| `reverb.bandPnrDb`、`bandR2` | 没有定义口径 | 各频带峰噪比的中位数（每一下都算），留下的几下 R² 的中位数 |

原因（按实录调整）：宽带上的骤降判据会在正常的衰减上误触发，按“一下有门限就整组作废”实现时，
正常的房间也会判成“无法判断”；门限的影响只在那一下那个频带上，所以判断与作废都缩到那一下那个频带。

`reverb`、`directRatio`、`direction`、`distance` 从“只定字段”变成会产生；喷麦、齿音、口水音仍只定字段。

- 方向：以前写“当前方向是不是最响最亮”，现在只看最响，亮度只显示、不参与判断。原因（按实录调整）：从背面进来的主要是
  房间声，亮度反而可能比正面高，“最亮”会把放反的情况判成正常。
- 色调：新增 `cardTone(result)`（只追加）。再测的结果没有结论，卡片无色；以前 `toneOf` 只看档位，再测时会带着默认档位的颜色
  （比如 T20 拍手太少时显示绿色）。`toneOf` 本身不变。

## 阈值

- 删除 `reverb.repeat_max_over_min`，由 `reverb.repeat_outlier_frac` 与 `reverb.repeat_min_kept` 取代：按实录调整，`repeat_outlier_frac`、`repeat_min_kept`、`gate_margin_db` 的数值取自规格、仍待标定，`source` 记 `规格起始值，待实录标定`。
  收满八九下时“最大比最小”偏严。
- 新增分组 `clap`（拍手的检测与起音、分段、噪声窗、削波），房间与位置共用。
- `reverb`、`direct_ratio`、`direction` 追加实现用的窗长、块长与门限判据的参数，见 `thresholds.json`。

## 没有改的

桥的名表（`BRIDGE.md`、`bridge-api.js`、`BridgeApi.h`）与 `Enums.h`。逐下的明细（`ReverbClap`、`DirectRatioClap`）、
方向的“还要重测哪一对”、决策表与距离标尺的建议目前只在 `src/core/room`、`src/core/direction`、`src/core/decide`
的返回值里，接进桥快照时再在名表末尾追加。
