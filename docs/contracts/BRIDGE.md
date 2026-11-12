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
| `startStep` | `ENUM_STEP_ID` 之一 | 开始或重测一步；步骤正忙回 `busy`，不认识回 `bad-step`，此刻不能测试回 `disabled` |
| `cancelStep` | 无 | 放弃当前步：这一步回到开始前的状态，旧结果不动 |
| `reportChange` | `ENUM_CHANGE_KIND` 之一 | 用户说“我动过了”：对应结果按过期规则标 `stale` |
| `dismissChannelPrompt` | 无 | “两路是同一个声音”的“不再提醒” |
| `openExternal` | 链接 | 交给系统浏览器，页面不自己打开外链 |
| `showLicense` | 无 | 打开许可证 |
| `clearClipLatch` | 无 | 用户点削波灯：清掉锁存，单声道和立体声都清 |
| `getEvidence` | `ENUM_STEP_ID` 之一 | 追加。回 `{ok: true, evidence}`，`evidence` 是这一步最近一次做完时的证据波形（见下文“证据波形”）；不认识回 `bad-step`，这一步没做完过回 `no-evidence`。只读，不改状态、不推快照 |

失败原因（`BRIDGE_REASONS`）：`bad-lang` 语言不认识；`bad-scale` 缩放档位越界；`bad-step` 步骤名不认识；`bad-arg` 其他参数类型不符；
`no-pending` 没有待确认的缩放；`scale-locked` 此时不允许改缩放；`not-ready` 宿主在显示兜底面板；`busy` 当前步骤正忙；
`disabled` 此刻不能测试：宿主在离线渲染、宿主是插件验证工具、音频还没准备好，或分配采集缓冲、起分析线程失败（追加）。
`busy` 也包括刚取消或作废的那一步还在做收尾分析的片刻（取消不等分析做完），稍后重试即可。
`no-evidence` 这一步还没有证据波形：没做完过（作废、取消的不算）（追加）。
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
  `offlineRender`（宿主正在离线渲染，测试暂停；追加字段）。宿主自己报离线渲染时为真；宿主没报、但测试中音频到得明显快于实时
  （`session.realtime_ratio_max` 倍以上）时也为真，并保持到下一次开始测试。
- `connect`（`SNAPSHOT_CONNECT`）：接通与诊断面板所需。`signalState` 取 `ENUM_SIGNAL_STATE`；`channelStatusL/R` 取
  `ENUM_CHANNEL_STATUS`；`channelPrompt` 取 `ENUM_CHANNEL_PROMPT`，是声道检查结果加宿主能力换算出的界面提示；
  `callbackAgeSec` 由编辑器的消息线程定时器按墙钟读心跳得出，其余都来自采样计数。接通的诊断来自 `startStep("connect")`
  的观察窗（`no_signal.low_flat_window_s` 秒）：`noiseDbfs` 是观察窗内帧电平的 P10，`allZero` 表示观察窗里严格全 0 达到
  `no_signal.strict_zero_window_s` 秒且没有正常信号；没做过接通时 `signalState` 是 `waiting`、`noiseDbfs` 是 `null`。
- `steps`：键是 `ENUM_STEP_ID`，值有 `SNAPSHOT_STEP` 的字段；`status` 取 `ENUM_STEP_STATUS`，`invalidReason` 取 `ENUM_RETEST_REASON`。
  作废的原因里，`offline-render`（宿主在离线渲染，或音频明显快于实时）与 `layout-changed`（测试中声道数变了）是追加的；
  `overflow`、`callback-gap`、`rate-changed` 同样只出现在作废上。做完的一步只要它产出的某一项过期，`status` 就是 `stale`。
- `results`：键是 `RESULT_SECTIONS`，没做过的步骤没有这个键。每个区段的字段 = 该结果的专有字段（`RESULT_<区段>`）
  加 `RESULT_COMMON`：`tone`（`ENUM_TONE`）、`reliability`、`retestReason`、`stale`。专有字段的名字就是
  `src/core/result/Results.h` 里 measures 与 verdict 字段的 camelCase 名，档位字段（`band`、`kind`、`severity`、`state`）取对应的 `ENUM_*`。
  `tone` 是卡片的颜色：再测时一律 `none`；齿音与口水音不分档，`severity` 停在 `light` 只表示“未分档”，`tone` 也是 `none`，
  界面不能按 `light` 着成绿色；设备处理与距离没有色调。
  数组量（`t20Sec`、`bandPnrDb`、`bandR2`、`levelDb`、`brightnessDb`）是 JSON 数组。

## 流程页用的追加字段（`bands`、`live`）

- `bands`（`SNAPSHOT_BANDS`）：档位边界，由 `thresholds.json` 换算（`gain.tp_*_dbfs`、`gain.target_peak_dbfs`、`noise.snr_*_db`、
  `reverb.*_below_s`、`direct_ratio.*_db`、`gain.clip_advice_drop_db`、`reverb.bands_hz`），单位在键名后缀里。界面画区间条、目标区和文案里的数都从这里取，自己不写数字。
  目标区（电平管上的绿区）是 `tpQuietDbfs` 到 `tpLoudDbfs`。
- `live`（`SNAPSHOT_LIVE`）：正在进行的那一步的实时信息。同一时刻只有一步在跑（`startStep` 在忙时回 `busy`），所以不按步骤分键；
  没有步骤在跑时 `phase` 是 `none`，`claps` 是空数组，`clapsNeeded`、`clapsMax` 仍有值（准备页要写“凑够几下、最多几下”）。
  - `phase`（`ENUM_STEP_PHASE`）：位置步依次是 `soft-clap`、`plosive`、`sibilant`、`natural`；方向步是 `direction` 与 `confirm`；
    其余步骤只有 `none`。界面只认枚举名，不认顺序：哪几个阶段、第几部分由界面的步骤表决定，阶段增减不改契约。
  - `round`、`rounds`：当前阶段里第几遍或第几下（从 1 起）和总数；`phaseElapsedSec`、`phaseDurationSec`：当前这一遍（这一下）
    已用和总时长（采样计数）。只有一遍的阶段（包括 `none`）就是整个阶段。
  - `claps`：房间步到目前为止每一下拍手的判定（`ENUM_CLAP_STATE`，按拍的先后），尾巴还没收完的那一下先不报，没拍的不在数组里；
    `clapsNeeded`、`clapsMax`：凑够几下有效、最多拍几下。`valid` 是 500 Hz 与 1 kHz 都量出了衰减；`truncated` 是其中一个频带的尾巴被门限切掉，
    只作废这一下；门限作废的太多、主指标给不出时整组作废，这时 `notice` 是 `gated`。房间步结束后 `claps` 保留到下一次开始这一步，失败页要用它画每一下的判定。
  - `directionIndex`：方向步当前是第几个方向（0 起，0 是开始时面对的方向）。`confirm` 阶段两遍，第 1 遍是 0，第 2 遍是那个更响的方向。
  - `notice`（`ENUM_FLOW_NOTICE`）、`noticeSec`：测试中的提示。`success` 是这一步完成；`spoiled` 安静时检测到声音；`clap-clipped` 轻拍削波了；
    `too-quiet` 念得太轻；`gated` 设备的降噪或噪声门让这组拍手作废；`redo` 其他需要重来的情况。除 `success` 外都表示这一步将在 `noticeSec`
    秒后自动重新开始。
  - `clapDetails`（追加，每项是 `SNAPSHOT_CLAP_DETAIL`）：与 `claps` 一一对应的明细。`timeSec` 这一下在本步里的时刻，`peakDbfs` 峰值，
    `clipped` 是否削波，`bandStatus`（`ENUM_CLAP_BAND_STATUS`）与 `t20Sec` 按 `bands.t20BandsHz` 的顺序给各频带的状态与 T20（拟合不出为 0）。
    房间步结束后与 `claps` 一起保留。
- `steps.<id>.durationSec` 在 `idle` 时也给出这一步的计划时长（准备页写“约 N 秒”）。
- `steps.<id>.evidenceRev`、`steps.<id>.runs`（追加）：见下文“证据波形”。

## 建议与“最该改”（追加：`fixes`、`advice`、`previous`）

- `fixes`：`ENUM_FIX_ITEM` 的数组，第一项就是“最该改”，空数组表示没有要改的。`no-signal`（接通的状态是问题，包括编辑器按墙钟判出的没有回调）与
  `digital-clip` 固定在前（有数字削波时 `gain` 不再单列，免得同一个原因说两次）；其余按 影响 × 置信度 ÷ 修改代价 排序，表在
  `thresholds.json` 的 `priority` 分组。要再测的、过期的结果不进候选。固定在前的两项只看是否过期、不看置信度：削波样本与接通状态都是直接数出来的，
  这一步即使因为太短等原因要再测，削波或没信号也已经发生，照样列出。候选条件见 `RESULT.md` 的“进最该改”一栏：轻微电流声、没确认的方向、
  未分档的齿音与口水音都不进。
- `advice`（`SNAPSHOT_ADVICE`）：`room` 取 `ENUM_ROOM_ADVICE`（T20 × D/R 的决策表，缺一项或要再测时 `unknown`），`distance` 取
  `ENUM_DISTANCE_ADVICE`，`directionPrompt` 取 `ENUM_DIRECTION_PROMPT`（方向测试的提示放在哪；没测过方向不算问题）。文案由界面按这些名字取词条。
- `previous`：形状与 `results` 相同，每个区段是被最近一次新结果换下来的那一次（“上次→现在”），没有上次的区段不出现。只在内存里，
  卸载插件就没了，不进插件 state；`stale` 恒为 `false`，`direction` 的 `repeatIndex` 为 -1。
- `results.direction` 追加 `repeatIndex`、`confirmedAdvantageDb`：那一对是当前方向与 `repeatIndex`，只在还要重唱这一对、
  或重唱后确认成立（`kind` 为 `maybe-reversed`）时给出，其余为 -1；`confirmedAdvantageDb` 只在确认成立时给出两次里较小的
  优势（dB），其余为 `null`（重唱后没确认也是 `null`）。

## 证据波形（追加：`getEvidence`、`evidenceRev`、`runs`）

报告详情里画的波形与事件标记。界面看到 `steps.<id>.evidenceRev` 变了再调 `getEvidence(stepId)` 取一次，不要每帧取。

- `steps.<id>.runs`：这一步做完过几次，0 表示没做过；作废、取消的不算。`steps.<id>.evidenceRev`：这一步的证据更新过几次，没有证据为 0。
  两者都只在这一步做完时加 1，各步各自单调、不回退。作废（`invalid`）、取消、过期（用户说动过了、换了房间、采样率或声道数变了）都不动证据，
  它和这一步留着的旧结果是同一次测试；卸载插件就没了，下次从 0 起。每步只留最近一份（包络最多 `evidence_cells_max` 格 × 2 个 16 位整数，约 32 KB，加事件）。
- 回执载荷 `EvidencePack`（`EVIDENCE_PACK`）：`stepId`、`rev`（等于取的那一刻的 `evidenceRev`）、`durationSec`、`envelope`、`events`。
- 时间轴：相对这一步开始的采样计时，只算分析实际用到的样本：阶段重来时倒数的那段与被丢掉的那次不算，`durationSec` 就是这些样本的时长。
  事件与包络在同一条轴上；位置步的喷麦、齿音、口水音标记由会话换算到这条轴（三项分析各自从自己那段音频的开头计时）。
- `envelope`（`EVIDENCE_ENVELOPE`）：被分析的那一路（单声道轨就是唯一那一路；立体声轨是开始这一步时的分析声道，声道检查给出，
  没做过时是左路），每格的最小、最大样本，取整到 -1000..1000 的整数（千分之一满刻度，超过满刻度的夹到 ±1000，NaN 不计入，整格都是 NaN 时记 0）。
  `min`、`max` 等长，第 i 格是 `[i·hopSec, (i+1)·hopSec)` 里的样本，最后一格可能不满。格数不超过 `session.evidence_cells_max`（现为 8192）；
  `hopSec` 是不短于 `session.evidence_hop_min_s`（现为 2 ms）的最少整数个采样（基础格）的整数倍，取让格数不超限的最小倍数，
  所以 `hopSec` ≥ max(`evidence_hop_min_s`, `durationSec` / `evidence_cells_max`)，多出的不到一个基础格。
- `events`（每项是 `EVIDENCE_EVENT`）：这一步各结果的证据标记里只取三种，`kind` 取 `EVIDENCE_EVENT_KINDS`，其余种类不输出。按 `timeSec` 排序，
  最多 `session.evidence_events_max`（现为 200）个，超出时留最早的。`timeSec` 是起点，`durSec` 是长度。`levelDb` 的口径随种类：
  - `plosive`：起音段低频峰值相对同音节元音稳态低频电平，dB（相对值）；
  - `sibilance`：这个擦音 6–12 kHz 减底噪后的均方电平，dBFS；高频项关闭（`highBandDisabled`）时是 3–6 kHz；
  - `mouth`（口水音）：约 `click.highpass_hz` 高通之后的峰值，dBFS，与 `click.loudestDbfs` 同口径。
- 隐私：包络是由录到的音频派生的数据，只在内存里：不写盘、不进插件 state、不上传、不进“复制结果”、不写日志、不进 issue。

## 口水音的口径与未分档的显示

- `click.loudestRelDb` 与 `click.loudestDbfs` 是约 `click.highpass_hz`（现为 2 kHz）高通之后的峰值，前者相对本人语音，后者是绝对电平；
  `count` 为 0 时这两项不是测量值（留 0），界面不显示数值，也不在电平管上画圆点。
- 齿音与口水音不分档：`tone` 是 `none`、`severity` 停在 `light` 只表示未分档。界面写“未分档”，卡片无色，不写“正常”或“少”，也不当作距离标尺的证据。
- `direction.repeatIndex` 与 `directionIndex` 用同一套编号：0 是测试开始时面对的方向。界面里的“方向 n”就是这个编号加 1，
  重唱确认的文案用它点出要重唱的那两个方向，不写转了多少度。

## 约束

- 载荷只含数值与枚举名，不含音频、不含路径；界面文案全部走 i18n，不从原生拿句子。
- 单位：电平 dBFS 或 dB，频率 Hz，时间秒（`Ms` 结尾的字段是毫秒），占比 `Pct` 结尾为百分数。
