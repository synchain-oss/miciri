status: frozen

# 阈值契约

数据在仓根 `thresholds.json`，格式由 `thresholds.schema.json` 规定，C++ 经 `src/core/config/Thresholds.h` 读取。
代码里不写魔法数：要用的数先进 `thresholds.json`。

## 条目

```json
"frame_ms": { "value": 100, "state": "synthetic", "source": "合成验证", "date": "2026-11-02" }
```

| 字段 | 含义 |
|---|---|
| `value` | 数字、布尔、数字数组，或 `null`（还没有值） |
| `state` | `synthetic`（合成验证）：在合成信号上核对过，`date` 是核对日期；`pending`（待标定）：规格里的起始值，或还没有值，等实录标定，`date` 为 `null`；`calibrated`（实录标定）：在实录上核对过，`date` 是标定日期，`source` 写依据和还缺什么 |
| `source` | 公开措辞：以一个固定的开头起句，需要时在中文冒号后写依据或还缺什么。开头有：`合成验证`；`规格起始值`、`工程起始值`、`产品约定`、`经验值`；`规格`（取法按实录改过，值仍待标定）；`待实录标定`、`待定`（还没有值或依据，冒号后写缺什么）；`实录标定（YYYY-MM）`（只用于 `calibrated`）。只写中性描述：房间、说话人、话筒的数目与观测到的数值，不写录音文件名、人名与私有目录 |
| `date` | 标定日期 `YYYY-MM-DD`；`pending` 必须是 `null` |
| `note` | 可选，一句话说明单位以外的口径 |

规则：除 `pending` 外的状态都必须有值与日期；`null` 与空日期只允许 `pending`；单位写在键名后缀（`_s`、`_ms`、`_db`、`_dbfs`、`_hz`、`_us`、`_frac`、`_pct` 等）。
合成验证不等于实录标定：所有 `synthetic` 的值都还要在实录上复核，复核后由标定的 PR 把它们改成带新日期的值。

## 怎么进 C++

配置期 `cmake/MiciriThresholds.cmake` 调 `tools/gen-thresholds.cmake`：用 CMake 自带的 JSON 解析读 `thresholds.json`，
按 schema 校验（必填字段、允许的字段、`state` 取值都从 schema 读），生成 `<构建目录>/generated/miciri/thresholds_generated.h`：

```cpp
#include "config/Thresholds.h"
double w = miciri::thr::noise::frame_ms;  // 数值一律 double
```

- 命名空间 `miciri::thr::<组>`；布尔是 `bool`；数组是 `std::array<double, N>`；`null` 项不生成，引用会编译失败。
- JSON、schema、脚本任何一个改了，CMake 自动重新配置与重编；校验失败直接让配置失败。
- `tests/contracts/CMakeLists.txt` 里的 `thresholds_*` 用例确认坏输入会被拒；`thresholds_test.cpp` 守档界的先后关系。

## 分组

`no_signal`、`channel_check`、`gain`、`noise`、`hum`、`device_processing`：值来自合成验证，少数起始值标 `pending`。
其中 `gain.flat_min_peak_dbfs` 与电流声的 `f0_accept_hz`、`whistle_max_hz`、`whistle_min_dbfs`、`slight_below_over_floor_db`、
`slight_below_dbfs` 已按实录标成 `calibrated`（2026-11-21，值不变）；啸叫的两项和 `slight_below_dbfs` 只有不触发一侧的录音。
`clap`：拍手的检测、分段、噪声窗、削波与截断，房间与位置共用，值来自合成验证。
`reverb`、`direct_ratio`、`direction`：规格里的档界与判据是起始值，标 `pending`；实现用的窗长与块长来自合成验证。
`plosive`、`sibilance`、`click`：规格里的起始值，全部 `pending`；口水音的残差倍数、高频背景门限是按实录调整过的起始值，仍未标定；齿音、口水音的档界和喷麦的低基频界还没有值（`null`）。喷麦不分档，没有档界。
三项各有一个参考值（经验值，不是档界）：`plosive.reference_max_db`、`sibilance.reference_max_db`（6-12 kHz 对元音的比值）、
`click.reference_max_per_min`（每分钟次数）；齿音对元音比值的取法（`sibilance.vowel_*`）与实录分析用的同一种。
元音参考最少要留多长按毫秒给（`plosive.vowel_kept_min_ms`、`sibilance.vowel_kept_min_ms`），代码按实际帧距换成帧数，帧距改了不用跟着改。
有效带宽（`sibilance.bandwidth_*`）每 500 Hz 一个频点、看 1 kHz 宽的频带，底噪取带内帧功率的第 2 百分位（数字静音的帧不算），连续 2 个频点不满足才停。
`recommended_distance`：推荐距离，静态的常见做法（电容、USB、不确定一组，动圈一组，耳机麦不给），全部 `pending`。
`direct_ratio`：只有测量用的窗、频带与判据，没有档界（D/R 不分档）。
`distance`：距离标尺的前后对比与弱提示。三个分辨力（`dr_resolution_db`、`plosive_resolution_db`、`sibilance_resolution_db`）是“现在减上次”要超过多少才算一个方向的迹象，
依据实录的同一位置散布与 A/B 能分辨的差（来源里写了数和还缺什么）；经验距离上限的临界距离系数、典型房间体积、声源指向性与比例是工程起始值。全部 `pending`。
房间建议只看 T20 的档位，没有自己的阈值。
`session`：测试的编排，各步与各阶段的时长、房间步凑够与最多几下拍手、测试中提示的倒数与自动重来的次数上限、
采集环的容量、“音频明显快于实时就当作离线渲染”的倍数与观察窗，全部 `pending`。时长与拍手数取自规格（来源记“规格起始值”）；
房间步看拍手的间隔、倒数、重来上限、采集环与快于实时的判据是规格没有给、实现时定的起始值（来源记“工程起始值”）。接通步的观察窗与
“没有回调”的秒数沿用 `no_signal.low_flat_window_s` 与 `no_signal.no_callback_s`，自然朗读的长度沿用 `click.analysis_window_s`。
证据波形的三个上限也在这一组：包络最窄一格 `evidence_hop_min_s`、一步最多几格 `evidence_cells_max`、最多几个事件 `evidence_events_max`，
是规格没有给、实现时定的工程值（来源记“工程起始值”），口径见 `BRIDGE.md` 的“证据波形”。
`priority`：“最该改”的排序表，每个可排序的候选项（方向、房间、电流声、增益、宽带噪声）一对“影响”与“修改代价”，
得分 = 影响 × 置信度 ÷ 代价。规格只给了先后，没给数，这些是实现时定的起始值（来源记“工程起始值”），全部 `pending`；
默认值排出来的顺序是 方向 > 房间 > 电流声 > 增益 > 宽带噪声。无信号与数字削波固定在前，不在这张表里；距离与喷麦不进候选。

## 没有带进来的内容

- 声道检查里“两路差 ≥80 dB”的旧判据已废弃：它对“同一信号加各自底噪”全部漏判，由“差减底噪和”与“差相对信号”取代。
- 电流声的“占噪声能量低于 9.1% 就不报”不再存在，占比也不再决定“轻微”：实录里真电流声的占比反而比安静房间里很弱的谐波小（宽带噪声越大占比越小）。
  现在看最强谐波高出周围本底的 dB 与电流声电平：前者低于 `hum.slight_below_over_floor_db`、或后者低于 `hum.slight_below_dbfs`，仍报，只标“轻微”，
  不进最该改；`energySharePct` 只作信息。`hum.slight_below_dbfs` 是绝对电平，前提是增益步已把人声带到目标附近。
- 噪声的“丢弃跳变帧”：合成实验里没有可测效果，不设阈值。
- T20 重复性的“最大比最小 < 1.4”：按实录调整，收满八九下时偏严，改成每个频带去掉偏离中位数超过 `reverb.repeat_outlier_frac` 的几下、剩下不少于 `reverb.repeat_min_kept` 下。
- D/R 的三档（低于 +5 dB 多、+5..+12 中、高于 +12 少）与 T20 × D/R 的决策表：实录里拍手相对扫频真值的偏移在房间之间差约 4 dB，决策表判对 6/10，
  加一个常数修正只被一间房约束、留一间房检验不成立，所以删掉，D/R 只做前后对比。
- 喷麦的中、重两档（按一遍字表最大值 9 dB、12 dB）：两个人里只有一个能用同一个阈值分开近与远（另一个去掉削波的音节后约 58%），
  跨人用对方的阈值也达不到，所以删掉，只给数值与前后对比。
- 齿音的“试偏轴再测”（15°）：偏轴 15° 只差 1–2 dB，逐事件一致只有 0.65–0.75，所以删掉这条建议与它的角度。

## 变更

改值：同一个 PR 附新旧差异说明与依据。实录标定过的值用 `calibrated` 标记（追加在 schema 的 `state` 枚举末尾，已有取值不动）；
依据只覆盖一侧时照样标 `calibrated`，`source` 写明缺哪一侧的录音。
改键名、改单位、改分组：属于契约变更，按 `CLAUDE.md` 的规则处理，并同步 schema。
