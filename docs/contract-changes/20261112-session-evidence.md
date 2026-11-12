<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# 证据波形进桥：`getEvidence`、`no-evidence`、步骤的 `evidenceRev` 与 `runs`、四张证据名表、三个 session 阈值

报告详情要画证据波形：被分析那一路的包络，加上喷麦、齿音、口水音的事件标记。Session 在工作线程上把它做好、按步存在内存里，
界面按需取。版本号不变（`BRIDGE_API_VERSION` 仍是 1），名表只在末尾追加，已有项的名字与顺序都没动。

## 追加了什么

| 位置 | 追加项 | 用途 |
|---|---|---|
| `BRIDGE_FUNCTIONS` 末尾 | `getEvidence` | 取某一步最近一次做完时的证据波形，回 `{ok: true, evidence}` |
| `BRIDGE_REASONS` 末尾 | `no-evidence` | 这一步还没做完过（作废、取消的不算） |
| `SNAPSHOT_STEP` 末尾 | `evidenceRev`、`runs` | 证据更新过几次（界面据此决定要不要重取）、这一步做完过几次 |
| 新表 | `EVIDENCE_PACK`、`EVIDENCE_ENVELOPE`、`EVIDENCE_EVENT`、`EVIDENCE_EVENT_KINDS` | 回执载荷、包络、事件的字段，事件种类 `plosive`、`sibilance`、`mouth` |
| `thresholds.json` 的 `session` 分组 | `evidence_hop_min_s`（2 ms）、`evidence_cells_max`（8192）、`evidence_events_max`（200） | 包络最窄一格、一步最多几格、最多几个事件；工程值，`pending`、来源“工程起始值” |
| `BRIDGE.md` | “证据波形”一节 | 时间轴、包络取值与格宽、事件口径、生命周期、隐私 |

C++ 一侧是 `src/plugin/BridgeApi.h`，与 JS 同一提交改，`bridge_parity_test` 逐项对拍。

## 为什么

- 报告详情要把喷麦、齿音、口水音标在波形上，并能缩放。core 的结果里已有 `EvidenceMark`，但桥上没有波形，也没有导出这些标记。
- 包络很大（最多 8192 格），放进每次推送的快照太重，所以单独一个函数取；快照里只放 `evidenceRev`，界面看到它变了再取。
- 时间轴按“这一步分析实际用到的样本”：阶段重来时被丢掉的那次与倒数不算，标记与波形才对得上。位置步的喷麦、齿音、口水音分析
  原来各自从自己那段音频的开头计时，这次由会话换算到这一步的时间轴，`RESULT.md` 里“相对该步开始”的说法因此对所有步骤成立。
- 格宽取整到整数个采样、按整数个基础格合并，每一格都恰好覆盖 `[i·hopSec, (i+1)·hopSec)`；代价是 `hopSec` 可能比
  `durationSec / evidence_cells_max` 多出不到一个基础格（2 ms）。

## 兼容性

- 旧界面不认识新键、新函数时忽略即可；`getEvidence` 只读，不改任何状态。
- 阈值只在 `session` 分组末尾追加，不改已有键。
- 包络只在内存里：不进插件 state、不写盘、不上传、不进“复制结果”、不写日志、不进 issue。

contract-neutral 不适用：这是契约追加，故附本文件。
