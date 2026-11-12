<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Session 接上桥：快照、测试命令、`disabled` 原因、调增益让齿音过期

Session 开始给桥快照填 `steps`、`results`、`bands`、`live`、`connect`。编辑器接上 `startStep`、`cancelStep`、`reportChange`、
`dismissChannelPrompt` 四个命令。版本号不变（`BRIDGE_API_VERSION` 仍是 1），名表只在末尾追加，已有项的名字与顺序都没动。

## 改了什么

| 位置 | 改动 | 用途 |
|---|---|---|
| `BRIDGE_REASONS` | 追加 `disabled` | `startStep` 此刻不能测试：宿主在离线渲染、宿主是插件验证工具、音频还没准备好，或分配采集缓冲、起分析线程失败 |
| `BRIDGE.md` | 写清 Session 填快照的语义 | `cancelStep` 恢复开始前的状态且不等分析做完（刚取消的那一步收尾时 `startStep` 回 `busy`）；`offlineRender`；接通的各字段；未分档项无色；`live` 的每遍时长、拍手判定与方向确认 |
| `RESULT.md` 色调 | 补一句 | 齿音、口水音未分档时卡片无色；`toneOf` 不变，无色在 Session 的快照里处理 |
| `RESULT.md` 过期规则 | 调增益一行加齿音 | 齿音给的是固定增益下的绝对电平，增益一变 A/B 比较就失效 |

C++ 一侧的名表是 `src/plugin/BridgeApi.h`，与 JS 同一提交改，`bridge_parity_test` 逐项对拍。

## 兼容性

- 旧界面不认识的原因按通用失败处理，显示“重来”。
- 调增益后齿音显示为过期，而不是继续显示旧增益下的电平；距离仍只随挪位置、换房间过期。

contract-neutral 不适用：这是契约追加与过期表的改动，故附本文件。
