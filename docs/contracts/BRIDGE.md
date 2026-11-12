status: frozen

# 桥契约（界面与插件之间）

两份同源文件：`web/shared/bridge-api.js` 与 `src/plugin/BridgeApi.h`。它们列出消息名、状态枚举和快照字段，
`tests/contracts/bridge_parity_test.cpp` 逐项对拍（表名、内容、顺序、版本号），任何一边多一项、少一项或顺序不同都判红。
传输层是 `web/shared/bridge.js`，只按这里的名表接线，不复制字段语义。

## 版本与扩展

- `BRIDGE_API_VERSION` 当前是 1，快照的 `apiVersion` 带同一个数。
- 同一版本内**只追加**：新函数、新事件、新字段、枚举新值只能加在表末尾，已有项不改名、不删除、不调换顺序。
  界面必须忽略不认识的字段与枚举值（当作 `unknown` 或不显示）。
- 改名、删除、调换顺序、改字段含义：升版本号，并按 `CLAUDE.md` 的契约变更规则新增 `docs/contract-changes/<日期>-<名字>.md`。
- 要新增名字：同一个提交里改 JS、`BridgeApi.h`（枚举改 `src/core/result/Enums.h`）和本文，三处对拍才会绿。

## 函数（界面调原生，`BRIDGE_FUNCTIONS`）

回执是 `{ok: true}` 或 `{ok: false, reason}`，`reason` 取 `BRIDGE_REASONS`。宿主此刻显示兜底面板时一律回 `not-ready`，不改任何状态。

| 函数 | 参数 | 说明 |
|---|---|---|
| `requestInitialState` | 无 | 回快照。页面首次装载、编辑器重建后都要调一次；桥不代调、不缓存 |
| `setLang` | `"zh"` 或 `"en"` | 不认识回 `bad-lang` |
| `setUiScale` / `commitUiScale` | 缩放档位 / 无 | 窗口缩放的 10 秒确认回退：先设置，确认才提交；没有待确认项回 `no-pending` |
| `setGuideSeen` | 布尔 | 指南已看 |
| `setMicType` | `ENUM_MIC_TYPE` 之一 | 影响“增益偏低”是否算问题、耳机麦是否做方向测试 |
| `startStep` | `ENUM_STEP_ID` 之一 | 开始或重测一步；步骤正忙回 `busy`，不认识回 `bad-step` |
| `cancelStep` | 无 | 放弃当前步 |
| `reportChange` | `ENUM_CHANGE_KIND` 之一 | 用户说“我动过了”：对应结果按过期规则标 `stale` |
| `dismissChannelPrompt` | 无 | “两路是同一个声音”的“不再提醒” |
| `openExternal` | 链接 | 交给系统浏览器，页面不自己打开外链 |
| `showLicense` | 无 | 打开许可证 |
| `clearClipLatch` | 无 | 用户点削波灯：清掉锁存，单声道和立体声都清 |

失败原因（`BRIDGE_REASONS`）：`bad-lang` 语言不认识；`bad-scale` 缩放档位越界；`bad-step` 步骤名不认识；`bad-arg` 其他参数类型不符；
`no-pending` 没有待确认的缩放；`scale-locked` 此时不允许改缩放；`not-ready` 宿主在显示兜底面板；`busy` 当前步骤正忙。
越界或类型不符一律由 C++ 侧夹取或拒绝，界面不校验。

## 事件（原生推给界面，`BRIDGE_EVENTS`）

纯下行，界面只订阅。

| 事件 | 载荷 | 说明 |
|---|---|---|
| `miciri.state` | 完整快照 | 状态变化时推送，节流在 C++ 侧；界面按整体替换，不做 diff |
| `miciri.meters` | `METER_FIELDS` | 电平，dBFS；单声道只用 L 一路；高频事件，和快照分开 |
| `miciri.error` | `{reason}` | 带外错误，`reason` 取 `BRIDGE_REASONS` |

## 快照

`SNAPSHOT_ROOT`：`apiVersion`、`lang`、`uiScale`、`guideSeen`、`micType`，加四个对象：

- `device`（`SNAPSHOT_DEVICE`）：采样率、缓冲大小、`layout`（`ENUM_CHANNEL_LAYOUT`）、宿主名（用于“没有单声道轨的宿主”那条文案）、
  `offlineRender`（宿主正在离线渲染，测试暂停；追加字段）。
- `connect`（`SNAPSHOT_CONNECT`）：接通与诊断面板所需。`signalState` 取 `ENUM_SIGNAL_STATE`；`channelStatusL/R` 取
  `ENUM_CHANNEL_STATUS`；`channelPrompt` 取 `ENUM_CHANNEL_PROMPT`，是声道检查结果加宿主能力换算出的界面提示；
  `callbackAgeSec` 由编辑器的消息线程定时器按墙钟读心跳得出，其余都来自采样计数。
- `steps`：键是 `ENUM_STEP_ID`，值有 `SNAPSHOT_STEP` 的字段；`status` 取 `ENUM_STEP_STATUS`，`invalidReason` 取 `ENUM_RETEST_REASON`。
  作废的原因里，`offline-render`（宿主在离线渲染，或测试中音频明显快于实时）与 `layout-changed`（测试中声道数变了）是追加的；
  `overflow`、`callback-gap`、`rate-changed` 同样只出现在作废上。
- `results`：键是 `RESULT_SECTIONS`，没做过的步骤没有这个键。每个区段的字段 = 该结果的专有字段（`RESULT_<区段>`）
  加 `RESULT_COMMON`：`tone`（`ENUM_TONE`）、`reliability`、`retestReason`、`stale`。专有字段的名字就是
  `src/core/result/Results.h` 里 measures 与 verdict 字段的 camelCase 名，档位字段（`band`、`kind`、`severity`、`state`）取对应的 `ENUM_*`。
  数组量（`t20Sec`、`bandPnrDb`、`bandR2`、`levelDb`、`brightnessDb`）是 JSON 数组。

## 流程页用的追加字段（`bands`、`live`）

- `bands`（`SNAPSHOT_BANDS`）：档位边界，由 `thresholds.json` 换算（`gain.tp_*_dbfs`、`gain.target_peak_dbfs`、`noise.snr_*_db`、
  `reverb.*_below_s`、`direct_ratio.*_db`、`gain.clip_advice_drop_db`、`reverb.bands_hz`），单位在键名后缀里。界面画区间条、目标区和文案里的数都从这里取，自己不写数字。
  目标区（电平管上的绿区）是 `tpQuietDbfs` 到 `tpLoudDbfs`。
- `live`（`SNAPSHOT_LIVE`）：正在进行的那一步的实时信息。同一时刻只有一步在跑（`startStep` 在忙时回 `busy`），所以不按步骤分键；
  没有步骤在跑时 `phase` 是 `none`，`claps` 是空数组，`clapsNeeded`、`clapsMax` 仍有值（准备页要写“凑够几下、最多几下”）。
  - `phase`（`ENUM_STEP_PHASE`）：位置步依次是 `soft-clap`、`plosive`、`sibilant`、`natural`；方向步是 `direction` 与 `confirm`；
    其余步骤只有 `none`。界面只认枚举名，不认顺序：哪几个阶段、第几部分由界面的步骤表决定，阶段增减不改契约。
  - `round`、`rounds`：当前阶段里第几遍或第几下（从 1 起）和总数；`phaseElapsedSec`、`phaseDurationSec`：当前阶段已用和总时长（采样计数）。
  - `claps`：房间步到目前为止每一下拍手的判定（`ENUM_CLAP_STATE`，按拍的先后），没拍的不在数组里；
    `clapsNeeded`、`clapsMax`：凑够几下有效、最多拍几下。出现 `truncated` 整组作废。房间步结束后 `claps` 保留到下一次开始这一步，失败页要用它画每一下的判定。
  - `directionIndex`：方向步当前是第几个方向（0 起）。
  - `notice`（`ENUM_FLOW_NOTICE`）、`noticeSec`：测试中的提示。`success` 是这一步完成；`spoiled` 安静时检测到声音；`clap-clipped` 轻拍削波了；
    `too-quiet` 念得太轻；`gated` 设备的降噪或噪声门让这组拍手作废；`redo` 其他需要重来的情况。除 `success` 外都表示这一步将在 `noticeSec`
    秒后自动重新开始。
- `steps.<id>.durationSec` 在 `idle` 时也给出这一步的计划时长（准备页写“约 N 秒”）。

## 约束

- 载荷只含数值与枚举名，不含音频、不含路径；界面文案全部走 i18n，不从原生拿句子。
- 单位：电平 dBFS 或 dB，频率 Hz，时间秒（`Ms` 结尾的字段是毫秒），占比 `Pct` 结尾为百分数。
