<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# 阈值的实录标定状态，有效带宽改按频带底噪（桥版本不变）

实录标定：3 间房、2 位说话人、2 支话筒（2026-11）。有实录依据的阈值改标“实录标定”，数值不变。
没有依据的保持待标定，`source` 写明还缺什么。

有效带宽原来拿最安静那部分帧的平均当底噪。在一段说话几乎不停的实录上，这样取的“底噪”大半是人声，
带宽被判成 2.1 kHz，实际高于 16 kHz。所以改了算法。

## 阈值（`thresholds.json`、`thresholds.schema.json`）

| 项 | 以前 | 现在 |
|---|---|---|
| `state` 枚举 | `synthetic`、`pending` | 末尾追加 `calibrated`：在实录上核对过，`date` 是标定日期，`source` 写依据和还缺哪一侧。已有取值不动；生成器原有的规则不变：`pending` 以外都要带值与日期 |
| 标成 `calibrated` 的项（2026-11-21，值不变） | `pending` 或 `synthetic` | `gain.flat_min_peak_dbfs`；`hum.f0_accept_hz`、`whistle_max_hz`、`whistle_min_dbfs`、`slight_below_over_floor_db`、`slight_below_dbfs` |
| 仍待标定、`source` 写明缺什么 | `待实录标定` | `plosive.low_f0_sensitivity_below_hz`、`click.moderate_per_min`、`click.heavy_per_min`（值仍是 `null`） |
| `source` 的措辞 | 限定为 `合成验证`、`规格起始值`、`待实录标定` | 写明一组固定的开头（含新增的 `实录标定（YYYY-MM）`、`待定`、`规格`，以及已在用的 `工程起始值`、`产品约定`、`经验值`），需要时在冒号后写依据或还缺什么 |
| `sibilance.bandwidth_smoothing_hz` | 250 Hz：底噪谱的平滑宽度 | 删除 |
| `sibilance.bandwidth_band_hz`、`bandwidth_step_hz`、`bandwidth_stop_points`、`bandwidth_noise_percentile` | 无 | 新增，均 `pending`：1000 Hz、500 Hz、2、2 |

## 测量（`Results.h` 的字段名不变）

`SibilanceMeasures::effectiveBandwidthHz` 的含义不变，仍是“人声高出底噪至少 10 dB 的最高频率”，取法改为：

- 从 `bandwidth_search_from_hz` 起，每 `bandwidth_step_hz` 一个频点，看以它为中心、`bandwidth_band_hz` 宽的频带；
- 底噪：该频带逐帧功率的第 `bandwidth_noise_percentile` 百分位，数字静音的帧（功率为零）不算在内；
- 人声：活跃帧（宽带电平在第 95 百分位以下 15 dB 以内）的平均；
- 高出 `bandwidth_over_noise_db` 的频点记为当前带宽；连续 `bandwidth_stop_points` 个频点不满足就停。一个窄谷不会让它提前停下。

因此读数落在 500 Hz 的网格上，最大值是最后一个整个频带落在奈奎斯特频率以内的频点。比如 48 kHz 下是 23500 Hz，以前可以到 24000 Hz。
`highBandDisabled` 的判据（不足 10 kHz）不变。

## 桥与界面

字段与名表都没变，`BRIDGE_API_VERSION` 仍是 2。界面显示有效带宽时，读数是 500 Hz 的整数倍；阈值的 `state` 不进快照。
