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
| `source` | 公开措辞：`合成验证`、`规格起始值`、`待实录标定` |
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
`reverb`、`direct_ratio`、`plosive`、`sibilance`、`click`、`direction`：规格里的起始值，全部 `pending`；
喷麦的档界与口水音的残差倍数是按实录调整过的起始值，仍未标定；齿音、口水音的档界和喷麦的低基频界还没有值（`null`）。`distance` 没有自己的阈值，由别的结果的证据合成。

## 没有带进来的内容

- 声道检查里“两路差 ≥80 dB”的旧判据已废弃：它对“同一信号加各自底噪”全部漏判，由“差减底噪和”与“差相对信号”取代。
- 电流声的“占噪声能量低于 9.1% 就不报”不再存在：占比低于 `hum.slight_energy_share_frac`（比例；`HumMeasures.energySharePct` 是百分比，比较前除以 100） 仍报，只标“轻微”，不进最该改。
- 噪声的“丢弃跳变帧”：合成实验里没有可测效果，不设阈值。

## 变更

改值：同一个 PR 附新旧差异说明与依据。实录标定完成的值会用新状态 `calibrated` 标记：那时往 schema 的 `state` 枚举末尾追加它，已有取值不动。
改键名、改单位、改分组：属于契约变更，按 `CLAUDE.md` 的规则处理，并同步 schema。
