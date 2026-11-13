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
| `state` | `synthetic`（合成验证）：在合成信号上核对过，`date` 是核对日期；`pending`（待标定）：规格里的起始值，或还没有值，等实录标定，`date` 为 `null` |
| `source` | 公开措辞：`合成验证`、`规格起始值`、`待实录标定`；数值取自规格、仍待标定时写 `规格起始值，待实录标定` |
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
`clap`：拍手的检测、分段、噪声窗、削波与截断，房间与位置共用，值来自合成验证。
`reverb`、`direct_ratio`、`direction`：规格里的档界与判据是起始值，标 `pending`；实现用的窗长与块长来自合成验证。
`plosive`、`sibilance`、`click`：规格里的起始值，全部 `pending`；喷麦的档界与口水音的残差倍数、高频背景门限是按实录调整过的起始值，仍未标定；齿音、口水音的档界和喷麦的低基频界还没有值（`null`）。
`distance` 没有自己的阈值，由别的结果的证据合成；决策表用 T20 与 D/R 的档位，也没有自己的阈值。
`session`：测试的编排，各步与各阶段的时长、房间步凑够与最多几下拍手、测试中提示的倒数与自动重来的次数上限、
采集环的容量、“音频明显快于实时就当作离线渲染”的倍数与观察窗，全部 `pending`。时长与拍手数取自规格（来源记“规格起始值”）；
房间步看拍手的间隔、倒数、重来上限、采集环与快于实时的判据是规格没有给、实现时定的起始值（来源记“工程起始值”）。接通步的观察窗与
“没有回调”的秒数沿用 `no_signal.low_flat_window_s` 与 `no_signal.no_callback_s`，自然朗读的长度沿用 `click.analysis_window_s`。
`priority`：“最该改”的排序表，每个可排序的候选项（方向、房间、距离、电流声、增益、宽带噪声、喷麦）一对“影响”与“修改代价”，
得分 = 影响 × 置信度 ÷ 代价。规格只给了先后，没给数，这些是实现时定的起始值（来源记“工程起始值”），全部 `pending`；
默认值排出来的顺序是 方向 > 房间 > 距离 > 电流声 > 增益 > 宽带噪声 > 喷麦。无信号与数字削波固定在前，不在这张表里。

## 没有带进来的内容

- 声道检查里“两路差 ≥80 dB”的旧判据已废弃：它对“同一信号加各自底噪”全部漏判，由“差减底噪和”与“差相对信号”取代。
- 电流声的“占噪声能量低于 9.1% 就不报”不再存在，占比也不再决定“轻微”：实录里真电流声的占比反而比安静房间里很弱的谐波小（宽带噪声越大占比越小）。
  现在看最强谐波高出周围本底的 dB，低于 `hum.slight_below_over_floor_db` 仍报，只标“轻微”，不进最该改；`energySharePct` 只作信息。
- 噪声的“丢弃跳变帧”：合成实验里没有可测效果，不设阈值。
- T20 重复性的“最大比最小 < 1.4”：按实录调整，收满八九下时偏严，改成每个频带去掉偏离中位数超过 `reverb.repeat_outlier_frac` 的几下、剩下不少于 `reverb.repeat_min_kept` 下。

## 变更

改值：同一个 PR 附新旧差异说明与依据。实录标定完成的值会用新状态 `calibrated` 标记：那时往 schema 的 `state` 枚举末尾追加它，已有取值不动。
改键名、改单位、改分组：属于契约变更，按 `CLAUDE.md` 的规则处理，并同步 schema。
