<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# 桥契约追加：流程页用的实时信息与档位边界

版本号不变（`BRIDGE_API_VERSION` 仍是 1），只在表末尾追加，已有项的名字、顺序、含义都没动。

## 追加了什么

| 位置 | 追加项 | 用途 |
|---|---|---|
| `SNAPSHOT_ROOT` | `bands`、`live` | 档位边界；正在进行的那一步的实时信息 |
| `SNAPSHOT_DEVICE` | `offlineRender` | 宿主离线渲染时测试暂停，界面要显示暂停提示 |
| 新表 `SNAPSHOT_BANDS` | 14 个档位边界 | 区间条、目标区、文案里的数都取自这里，界面不写数字 |
| 新表 `SNAPSHOT_LIVE` | 11 个实时字段 | 阶段、第几遍、拍手逐下判定、测试中的提示 |
| 新枚举 `ENUM_STEP_PHASE`、`ENUM_CLAP_STATE`、`ENUM_FLOW_NOTICE` | 见 `docs/contracts/BRIDGE.md` | 同上 |

C++ 一侧：`src/plugin/BridgeApi.h` 的同名表、`src/core/result/Enums.h` 的 `StepPhase`、`ClapState`、`FlowNotice`，与 JS 同一提交改，`bridge_parity_test` 逐项对拍。

## 为什么

测试进行中的远距离模式需要知道：现在是位置步的哪个阶段、第几遍、每一下拍手有没有效、为什么要重来、几秒后自动重来。这些原来的快照里没有。
档位边界（目标区、区间条的分段）原来只存在于 `thresholds.json`，界面如果自己写一份会和阈值文件漂移，所以由 Session 换算后随快照下发。

## 兼容性

- 旧界面忽略不认识的字段，不受影响。
- 新界面遇到没有 `bands` 或 `live` 的快照（旧原生侧）时，把它们当作缺省：不画区间条、不进入测试中视图。
- `phase` 只认枚举名不认顺序。位置步里“轻拍 3 次”这一阶段可以整段删掉：Session 不再发 `soft-clap`，界面的步骤表删一行，契约不动。
- `steps.<id>.durationSec` 在 `idle` 时也给出计划时长（此前只在跑的时候有意义），属于语义补充，没有已有实现依赖旧行为。

contract-neutral 不适用：这是契约追加，故附本文件。
