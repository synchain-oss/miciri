<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# 建议与“最该改”进桥：五张枚举、三个快照键、方向确认的那一对、拍手明细、priority 阈值分组

Session 开始给出“最该改”的排序、房间与距离的建议、方向测试的提示放在哪，以及“上次→现在”。版本号不变（`BRIDGE_API_VERSION` 仍是 1），
名表只在末尾追加，已有项的名字与顺序都没动。

## 追加了什么

| 位置 | 追加项 | 用途 |
|---|---|---|
| `Enums.h` 表尾 | `ClapBandStatus`、`RoomAdvice`、`DistanceAdvice`、`DirectionPrompt`、`FixItem` | 前四个原来定义在 `room/Room.h`、`decide/Decide.h`，取值与顺序不变，搬到表尾并给出线上名字；`FixItem` 是“最该改”的候选项 |
| 桥名表 | `ENUM_CLAP_BAND_STATUS`、`ENUM_ROOM_ADVICE`、`ENUM_DISTANCE_ADVICE`、`ENUM_DIRECTION_PROMPT`、`ENUM_FIX_ITEM`、`SNAPSHOT_CLAP_DETAIL`、`SNAPSHOT_ADVICE` | 对应上面的枚举与两种对象 |
| `SNAPSHOT_ROOT` 末尾 | `fixes`、`advice`、`previous` | 排好的“最该改”；房间、距离的建议与方向提示；每项被换下来的上一次结果（只在内存里） |
| `SNAPSHOT_LIVE` 末尾 | `clapDetails` | 房间步每一下拍手的时刻、峰值、是否削波、各频带的状态与 T20 |
| `RESULT_DIRECTION` 末尾 | `repeatIndex`、`confirmedAdvantageDb` | 方向确认的那一对与两次里较小的优势 |
| `thresholds.json` | 新分组 `priority` | 每个可排序的候选项一对“影响”与“修改代价”，全部 `pending`、来源“工程起始值” |
| `RESULT.md` | “进最该改”一句、色调一行 | 指向排序表与 `fixes`；色调一行注明那是 `toneOf` 的映射，未分档项卡片无色 |

C++ 一侧是 `src/plugin/BridgeApi.h` 与 `src/core/result/Enums.h`，与 JS 同一提交改，`bridge_parity_test` 逐项对拍。

## 为什么

- 房间、距离的建议与方向提示原来只在 core 里，没有线上名字，界面拿不到；拍手的逐下明细与方向确认的那一对同样只在 core 的分析里。
- “最该改”的排序规格只给了先后（方向 > 房间 > 距离 > 电流声 > 增益 > 宽带噪声），没给影响与代价的数。按“代码里不写魔法数”放进
  `priority` 分组，默认值排出来的就是这个先后；喷麦排在最后。设备处理按 `RESULT.md` 只作横幅，不进候选。
- “上次→现在”只在内存里，不进插件 state（state 格式还没有这一项），卸载插件就没了。

## 兼容性

- 旧界面不认识新键时忽略即可；`fixes` 为空数组表示没有要改的。
- 阈值只追加分组，不改已有键。

contract-neutral 不适用：这是契约追加，故附本文件。
