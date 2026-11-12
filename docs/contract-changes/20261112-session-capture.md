<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Session 的采集与状态机：两种作废原因与 session 阈值分组

测试的采集环、工作线程、步骤状态机、过期规则与离线保护落地（`src/session/`），处理器开始把测试中的样本推进采集环。
桥快照还没有接上 Session（下一步），这里只追加它要用的枚举值与阈值。版本号不变（`BRIDGE_API_VERSION` 仍是 1），
名表只在末尾追加，已有项的名字与顺序都没动。

## 追加了什么

| 位置 | 追加项 | 用途 |
|---|---|---|
| `ENUM_RETEST_REASON` | `offline-render`、`layout-changed` | 一步作废的原因：宿主在离线渲染（宿主报的，或测试中音频明显快于实时）；测试中声道数变了 |
| `thresholds.json` | 新分组 `session` | 各步与各阶段的时长、房间步的拍手数、测试中提示的倒数与自动重来的上限、采集环容量、快于实时的判据 |

C++ 一侧是 `src/core/result/Enums.h` 的 `RetestReason`，与 JS 同一提交改，`bridge_parity_test` 逐项对拍。

## 为什么

- 离线保护不能只靠宿主的离线标志：有的宿主（实测 REAPER 关掉 “Inform plug-ins of offline rendering state” 时）离线渲染也报实时。
  这时音频一块接一块地来，比实时快得多；Session 拿收到的音频时长和墙钟比，快于 `session.realtime_ratio_max` 倍就把当前步作废，
  原因记 `offline-render`。界面要能说出“宿主在渲染”，所以它是单独的一项，而不是笼统的重来。局限写在 `docs/KNOWN_ISSUES.md`。
- 声道数在测试中途变了，已收的样本没法接着用，单独记 `layout-changed`；采样率变化仍是 `rate-changed`。
- 步骤时长、拍手数原来只写在规格里，按“代码里不写魔法数”的规则放进 `session` 分组。规格没有给的几项（看拍手的间隔、倒数、
  重来上限、采集环、快于实时的判据）是实现时定的起始值，来源记“工程起始值”，与取自规格的区分开，标定时不当成规格要求。

## 兼容性

- 旧界面不认识的原因按 `unknown` 处理（契约要求），显示通用的“重来”。
- 阈值只追加分组，不改已有键。

contract-neutral 不适用：这是契约追加，故附本文件。
