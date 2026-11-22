<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# 实录标定后的退路：D/R 与喷麦不分档、房间建议只看 T20、距离标尺改成前后对比（桥版本 2）

实录标定：3 间房、2 位说话人、2 支话筒（2026-11）。T20 达标，保留；D/R 随距离单调，但 T20 × D/R 的决策表判对 6/10；
喷麦只在一位说话人上能用同一个阈值分开近与远；齿音的绝对电平随挪远方向一致，偏轴 15° 只差 1–2 dB。维护者据此决定：

- D/R 去掉三档与决策表，测量保留，只用于重测位置之后的“上次 → 现在”；房间建议只看 T20。
- 喷麦不给档位，只显示数值与“上次 → 现在”，不作“太近”的证据。
- 齿音删掉“试偏轴再测”，只用固定增益下的绝对电平做前后对比。
- 距离标尺不给绝对判断：第一次只有“可能离远了”这条弱提示，重测位置之后按 D/R、喷麦、齿音的变化说更近、更远、没有明显变化或不确定。
  口水音仍只作提示，不进标尺。
- 方向测试的主按钮从“T20 正常 + D/R 低”改成“T20 干或正常 + 可能离远了”。

这些是删除与改含义，按 `BRIDGE.md` 的规则把 `BRIDGE_API_VERSION` 从 1 升到 2。

## Result（`Enums.h`、`Results.h`）

| 项 | 以前 | 现在 |
|---|---|---|
| `DrBand`、`DirectRatioVerdict::band` | 低、中、高三档，色调红、琥珀、绿 | 删除。`DirectRatioVerdict` 为空，D/R 无色 |
| `PlosiveVerdict::severity` | 按一遍字表最大值分轻、中、重 | 恒为 `light`（未分档），与齿音、口水音相同，卡片无色 |
| `RoomAdvice` | 决策表的七种 | `unknown`、`good`（干、正常）、`change-room-and-move-closer`（很混：先换地方或加吸声，同时靠近）、`move-closer-then-treat`（偏混：靠近一点，还不行再加吸声）；删掉 `fine-not-priority`、`move-closer-check-direction`、`keep-close-dont-back-off` |
| `DistanceCase` | `unknown`、`fine`、`too-close`、`too-far`、`conflicting` | `unknown`、`conflicting`、`maybe-too-far`、`closer`、`farther`、`no-change`；删掉三种绝对判断 |
| `DistanceSign`（新） | — | 一项读数的“上次 → 现在”：`not-compared`、`closer`、`farther`、`no-change`、`gain-changed`、`room-changed` |
| `DistanceAdvice` | `none`、`back-off-a-little`、`drink-water`、`fix-room-or-direction-first`、`move-closer` | 只剩 `none`、`move-closer`（只在可能离远了时给）。不再有“两边都有证据”的矛盾；口水音的“退后一点”“喝点水”跟着口水音走，以后作为口水音自己的建议追加，不占距离这一格 |
| `DistanceMeasures` | `closeEvidence`、`farEvidence` 是太近、太远的证据数 | 改为重测后指向更近、更远的项数；追加 `drDeltaDb`、`plosiveDeltaDb`、`sibilanceDeltaDb`（现在减上次）与 `empiricalLimitM`（按 T20 估的经验距离上限，米） |
| `DistanceVerdict` | `kind` | 追加 `drSign`、`plosiveSign`、`sibilanceSign`；置信度恒为可信 |
| `FixItem` | 含 `distance`、`plosive` | 删掉这两项：距离与喷麦都不进“最该改” |

过期规则追加：调增益与声道布局变化也让距离过期（弱提示要用 SNR，齿音的比较要同一增益）。

## 桥（`bridge-api.js`、`BridgeApi.h`）

- `BRIDGE_API_VERSION` 1 → 2。
- 删除：`ENUM_DR_BAND`；`RESULT_DIRECT_RATIO` 的 `band`；`SNAPSHOT_BANDS` 的 `drLowBelowDb`、`drHighAboveDb`；上表里删掉的枚举值。
- 新增：`ENUM_DISTANCE_SIGN`（紧跟 `ENUM_DISTANCE_CASE`）；`RESULT_DISTANCE` 现在是 `closeEvidence`、`farEvidence`、`drDeltaDb`、`plosiveDeltaDb`、
  `sibilanceDeltaDb`、`empiricalLimitM`、`kind`、`drSign`、`plosiveSign`、`sibilanceSign`；`SNAPSHOT_BANDS` 末尾追加 `drResolutionDb`、
  `plosiveResolutionDb`、`sibilanceResolutionDb`。

快照里的距离长这样（重测位置后挪近了，两次之间调过增益）：

```json
"distance": {
  "closeEvidence": 2, "farEvidence": 0,
  "drDeltaDb": 4.1, "plosiveDeltaDb": 7.5, "sibilanceDeltaDb": 0, "empiricalLimitM": 0.41,
  "kind": "closer", "drSign": "closer", "plosiveSign": "closer", "sibilanceSign": "gain-changed",
  "tone": "none", "reliability": "trusted", "retestReason": "none", "stale": false
}
```

## 阈值（`thresholds.json`）

- 删除：`direct_ratio.low_below_db`、`direct_ratio.high_above_db`；`plosive.moderate_above_db`、`plosive.heavy_above_db`；
  `sibilance.off_axis_try_deg`；`priority.distance_impact`、`distance_cost`、`plosive_impact`、`plosive_cost`。
- 新分组 `distance`，全部 `pending`：三个分辨力（D/R 3.0 dB、喷麦 6.0 dB、齿音 5.2 dB，来源里写了实录依据与还缺什么），
  经验距离上限用的临界距离系数 0.057、典型房间体积 30 m³、声源指向性 2、比例 0.5。
- “最该改”的默认顺序从“方向 > 房间 > 距离 > 电流声 > 增益 > 宽带噪声 > 喷麦”变成“方向 > 房间 > 电流声 > 增益 > 宽带噪声”。

## 兼容性

- 界面要按版本 2 的名表改：D/R 卡不再有档位与颜色；房间建议只剩四种；距离卡改成前后对比；“最该改”里不会再出现距离与喷麦。
  界面忽略不认识的字段与枚举值的规则不变。
- 插件 state 不含这些项，不受影响；“上次 → 现在”仍只在内存里。

contract-neutral 不适用：这是契约变更，故附本文件。
