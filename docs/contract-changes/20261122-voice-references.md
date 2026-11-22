<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# 喷麦、齿音、口水音的参考值与轻提示，推荐距离进快照

接着 `20261121-calibration-fallbacks.md`：三项仍不分档，但各给一个参考值（经验值），超过时在各自的卡上给一条轻提示；
推荐距离按话筒类型放进快照。名表只在末尾追加，已有项的名字与顺序都没动，`BRIDGE_API_VERSION` 仍是 2。

## 每个新字段与建议键

| 位置 | 字段 | 含义 |
|---|---|---|
| `results.plosive` 末尾 | `overReference` | `worstRatioDb`（每遍字表最响音节的瞬态比，取最大）超过 `bands.plosiveReferenceDb` |
| `results.sibilance` 末尾 | `highOverVowelDb` | 6-12 kHz 对紧随其后的元音（200-4000 Hz）的比值，dB，有元音参考的擦音取中位数 |
| | `vowelEvents` | 有元音参考的擦音个数；为 0 时 `highOverVowelDb` 不是测量值 |
| | `overReference` | `highOverVowelDb` 超过 `bands.sibilanceReferenceDb`；高频项关闭（`highBandDisabled`）或 `vowelEvents` 为 0 时为 `false` |
| `results.click` 末尾 | `overReference` | `perMinute` 超过 `bands.clickReferencePerMin`；参考值还没定，现在恒为 `false` |
| `bands` 末尾 | `plosiveReferenceDb` | 9 dB（“参考 9 dB 以下”） |
| | `sibilanceReferenceDb` | −3 dB（“参考 −3 dB 以下”） |
| | `clickReferencePerMin` | 现在为 `null`：界面不显示参考 |
| `advice` 末尾 | `plosive` | `ENUM_PLOSIVE_ADVICE`：`none`；`pop-filter-or-back-off`（加防喷罩，或在推荐距离范围内退后一点再测） |
| | `sibilance` | `ENUM_SIBILANCE_ADVICE`：`none`；`angle-soften-de-ess`（话筒偏开嘴的正前方 15-30°，朝下巴或嘴角；s 念轻一点；录完用齿音消除）。不建议退后，不说再测对比 |
| | `click` | `ENUM_CLICK_ADVICE`：`none`；`back-off-a-little`（在推荐距离范围内退后一点）；`drink-water`（上一次位置步也超过、这次仍超过：喝点水，口干也会加重） |
| 根上末尾 | `recommendedDistance` | `SNAPSHOT_RECOMMENDED_DISTANCE`：`aroundLowCm`、`aroundHighCm`（“约 …”，两端相等是一个数，不等是一段）、`minCm`、`maxCm`（范围）。电容、USB、不确定：15、15、10、30；动圈：5、10、5、10；耳机麦：`null` |

规则：

- 三条建议只看当前、可信的结果，只在各自的卡上，不进“最该改”，也不作距离证据。卡片仍然无色（不分档）。
- 齿音的比值随距离的方向不稳（近讲效应，挪远后反而可能变大），只作参考；前后对比仍只用固定增益下的绝对电平。
- 推荐距离是静态的常见做法，不是测量；位置步与距离卡显示它，喷麦、口水音的“退后一点”都以它为界。耳机麦没有推荐距离，
  `pop-filter-or-back-off` 与 `back-off-a-little` 这时只说不涉及距离的部分由界面决定。

C++ 一侧是 `src/plugin/BridgeApi.h` 与 `src/core/result/Enums.h`，与 JS 同一提交改，`bridge_parity_test` 逐项对拍。编辑器把
`recommendedDistance` 原样转发（耳机麦时是 `null`）。

## 阈值（`thresholds.json`，全部 `pending`）

- `plosive.reference_max_db` 9.0：依据实录标定（3 间房、2 位说话人、2 支话筒，2026-11），两位说话人 5 cm 时每遍字表的最大值都超过它、
  60 cm 时都低于它，一位在 15 cm 处有一遍超过。
- `sibilance.reference_max_db` −3.0：常见做法的默认；同一批实录里两位说话人在唱歌位置（约 15 cm）的中位比值是 −5.4、−7.1 dB。
  比值的取法 `sibilance.vowel_gap_ms`、`vowel_window_ms`、`vowel_centroid_max_hz`、`vowel_within_db`、`vowel_min_frames`、
  `vowel_loudest_frac` 与实录分析用的同一种。
- `click.reference_max_per_min`：`null`，要等更多的试听标签。
- 新分组 `recommended_distance`：`condenser_around_cm` 15、`condenser_range_cm` [10, 30]、`dynamic_range_cm` [5, 10]。

## 兼容性

- 只追加；旧界面不认识的键忽略即可。`clickReferencePerMin` 与 `recommendedDistance` 可能是 `null`。
- 插件 state 不含这些项。

contract-neutral 不适用：这是契约追加，故附本文件。
