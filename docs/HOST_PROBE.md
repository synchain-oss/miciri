<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# 宿主探针（仅测试构建）

给做宿主测试的人看。外部的宿主测试脚本读不到 WebView，插件也没有参数可读，所以测试构建的插件自己把计数写成一个 JSON 文件，
脚本去读这个文件，就能断言“插件在各种渲染模式下看到的离线标志”和“离线时分析次数为 0”。

**发布版完全不含这段代码。** 探针的源文件不编进发布版插件，二进制里没有探针代码，也没有下面的环境变量名。

## 开关

| 开关 | 说明 |
|---|---|
| CMake 选项 `MICIRI_HOST_PROBE` | 默认 `OFF`。`ON` 时探针的源文件编进插件，并给插件目标加编译定义 `MICIRI_HOST_PROBE=1` |
| 环境变量 `MICIRI_HOST_PROBE_JSON` | 计数 JSON 的输出路径（宿主进程启动前设好）。没设或为空，就不写计数 |
| 环境变量 `MICIRI_HOST_PROBE_SCRIPT` | 自动操作脚本的路径，见下文“自动操作”。没设或为空，不执行任何动作 |
| 环境变量 `MICIRI_HOST_PROBE_OUT` | 自动操作的输出目录（要事先存在），见下文“输出文件”。没设或为空，不写 step 与 final 文件 |

三个变量都没设时探针什么都不创建，也不起定时器。

构建：`cmake -S . -B <构建目录> -DJUCE_PATH=<JUCE 检出> -DMICIRI_HOST_PROBE=ON`。ON 构建只用于宿主测试，不得发布。

## 什么时候写

- 消息线程上约每 250 ms 写一次，内容没变就不写。
- `prepareToPlay`、`releaseResources` 和插件实例销毁时各再写一次，所以宿主关掉插件后文件里是最后的数字。
- 写法是先写同目录的临时文件，再原子改名覆盖：读文件的一方永远看不到写了一半的文件。目标目录要事先存在，插件不替你建。
- 写失败（目录不存在、没权限、文件被读者占着）不会影响音频，也不会弹窗；下一个节拍会再试。

## 字段

`schema` 固定为 1；字段改名或含义变化时才升。所有计数从进程里第一个插件实例创建起累计，**同一进程里的多个实例合计，写进同一个文件**，
所以做宿主测试时每个宿主进程只放一个实例，最好每次测试换一个输出路径。

| 字段 | 含义 |
|---|---|
| `schema` | 固定 1 |
| `pid` | 宿主进程号（用来确认读到的是这次启动写的文件） |
| `sampleRate`、`maxBlockSize` | 最近一次 `prepareToPlay` 的采样率与最大块长 |
| `numInputChannels`、`numOutputChannels` | 最近一次 `prepareToPlay` 时主总线的声道数 |
| `prepareCount` | `prepareToPlay` 被调用的次数 |
| `blocksTotal` | 收到的有样本的音频块数（0 样本的调用不计），恒等于 `blocksNonRealtime + blocksRealtime`，也恒等于 `blocksAllZero + blocksNonZero` |
| `blocksNonRealtime` | 宿主告诉插件“现在是离线处理”（`isNonRealtime()` 为真）时收到的块数 |
| `blocksRealtime` | 其余的块数 |
| `blocksAllZero` | 内容全是 0 的块数（+0 与 -0 都算 0；NaN、Inf、非规格化数算有内容） |
| `blocksNonZero` | 有内容的块数 |
| `editorOpens`、`editorCloses` | 编辑器被创建、被销毁的次数 |
| `analysisCount` | 测试的工作线程每调一次分析函数加一（房间步边收边看拍手也算）；验证宿主与宿主报离线渲染时不开始测试，必须是 0。例外：宿主没报的离线渲染要等快于实时的判断，房间步在那之前边收边看拍手的几次会计入（这一步随后作废），见 `KNOWN_ISSUES.md` |
| `lastIsNonRealtime` | 最近一个块收到时的离线标志；还没收到任何块时为 `false` |

示例（离线渲染 3 块之后）：

```json
{
  "schema": 1,
  "pid": 12345,
  "sampleRate": 48000,
  "maxBlockSize": 512,
  "numInputChannels": 2,
  "numOutputChannels": 2,
  "prepareCount": 1,
  "blocksTotal": 3,
  "blocksNonRealtime": 3,
  "blocksRealtime": 0,
  "blocksAllZero": 1,
  "blocksNonZero": 2,
  "editorOpens": 0,
  "editorCloses": 0,
  "analysisCount": 0,
  "lastIsNonRealtime": true
}
```

## 自动操作

宿主测试工具读不到也点不了 WebView，所以测试构建的探针可以按脚本自己调用桥上的几个函数，把一段真实录音按测试步骤的时间轴拼好，
挂在轨道上实时播放（1 倍速渲染），插件就像用户在界面上点了一样跑完测试流程。动作直接作用在处理器持有的测试会话上，**不需要打开编辑器**。

脚本是一个 JSON 文件（UTF-8，不超过 1 MiB、10000 个动作）：

```json
{
  "schema": 1,
  "actions": [
    { "at": 0.5, "do": "setMicType", "arg": "condenser" },
    { "at": 1.0, "do": "startStep", "arg": "connect" },
    { "at": 12, "do": "startStep", "arg": "gain" },
    { "at": 200, "do": "reportChange", "arg": "position" },
    { "at": 201, "do": "startStep", "arg": "position" }
  ]
}
```

- `at`：**插件已处理的样本秒**，从探针看到的第一个有样本的块起算（样本数除以当前采样率），不是墙钟。渲染里样本流是确定的，
  墙钟会被宿主启动、打开工程带偏。还没收到有样本的块时时钟没开始，`at` 为 0 的动作也要等第一块。`at` 不得小于前一个动作的 `at`。
- `do`：只认桥上已有的函数 `setMicType`、`startStep`、`cancelStep`、`reportChange`、`dismissChannelPrompt`。带参数的三个
  （`setMicType`、`startStep`、`reportChange`）必须给字符串 `arg`，取值同桥契约的枚举；`cancelStep`、`dismissChannelPrompt` 不带 `arg`。
- 动作按顺序执行。到了 `at` 时若回执是 `busy`（上一步还在跑或还在收尾，比如阶段自动重来拖长了；探针最近一眼还看到有一步在跑时也直接回 `busy`，
  保证上一步的 step 文件先写），这个动作每一拍重试，
  直到不是 `busy`；它后面的动作都等它。其余回执（`bad-step`、`bad-arg`、`disabled`）原样记下，接着往下走。
- 回执与桥相同（`{ok: true}` 或 `{ok: false, reason}`，原因取 `BRIDGE_REASONS`）。
- 脚本读不到、不是合法 JSON、`schema` 不是 1、任何一个动作不合规（未知的 `do`、缺参数、`at` 倒退等）：**一个动作也不执行**，
  原因写进 final 文件的 `script.error`。
- 动作在消息线程的探针定时器上执行。有脚本或输出目录时定时器每 50 ms 一拍，所以动作的实际执行时刻比 `at` 晚不到 50 ms 再加一个块长，
  实际时刻记在日志的 `doneSec` 里；计数 JSON 仍约每 250 ms 写一次。音频线程只多做一次 relaxed 原子加法（每实例的样本计数）。

## 输出文件

写进 `MICIRI_HOST_PROBE_OUT`，写法同计数 JSON（先写临时文件再原子改名；写失败不影响音频，留到下一拍再试）。文件名带宿主进程号，
宿主测试工具在渲染前还有一次“学习”启动也会加载插件，两次的文件不会混在一起。每个宿主进程只放一个插件实例。

- `step-<步骤>-<序号>-pid<进程号>.json`：每一步结束（做完、作废、取消）时写一份。序号是本实例里 step 文件的顺序，从 001 起。
  这一步自动重来（倒数后重做一个阶段或整步）不算结束。编辑器开着时从界面开始的步骤也照样写。
- `final-pid<进程号>.json`：`releaseResources` 与实例销毁时各写一次（后写的覆盖先写的）。

| 字段 | step | final | 含义 |
|---|---|---|---|
| `schema` | ✓ | ✓ | 固定 1 |
| `kind` | ✓ | ✓ | `step` 或 `final` |
| `pid` | ✓ | ✓ | 宿主进程号 |
| `processedSec` | ✓ | ✓ | 写文件时插件已处理的样本秒；还没有样本时为 `null` |
| `script` | ✓ | ✓ | `state`（`none` 没给脚本、`ok`、`error`）、`error`（只在 `error` 时有）、`actions`（脚本里的动作数）、`remaining`（还没执行完的） |
| `errors`、`lastError` | ✓ | ✓ | 探针自己出错（异常被吞下、不影响插件）的次数与最近一次的说明，没出过错时 `lastError` 为 `null` |
| `pendingWrites` | ✓ | ✓ | 写失败、排队等下一拍重写的文件数 |
| `seq`、`step`、`outcome` | ✓ | | 序号、步骤名、结局：`done` 做完，`invalid` 作废（原因看快照里这一步的 `invalidReason`），`cancelled` 取消 |
| `reason`、`stepFiles` | | ✓ | `release` 或 `destroy`；本实例写过几个 step 文件 |
| `snapshot` | ✓ | ✓ | 此刻的快照，见下 |
| `evidence` | ✓ | | 这一步的证据，形状同桥 `getEvidence` 的回执（`{ok: true, evidence}` 或 `{ok: false, reason: "no-evidence"}`），但包络只有 `hopSec` 与 `cells`（格数），没有逐格的 `min`、`max`。作废、取消时是这一步上一次做完的那份（`evidence.rev` 不变），从没做完过就是 `no-evidence` |
| `waiting` | ✓ | ✓ | 到期了但回了 `busy`、正在等的那个动作（字段同下），没有为 `null` |
| `actions` | ✓ | ✓ | 已执行完的动作日志：`index`、`at`、`do`、`arg`、`firstTriedSec`（第一次尝试时的样本秒）、`doneSec`（拿到非 `busy` 回执时）、`busyReplies`、`reply` |

`snapshot` 与桥的快照（`requestInitialState` 的回执）出自同一个拼装函数（`src/plugin/BridgeSnapshot.h`，编辑器也用它），
`micType`、`device`、`recommendedDistance` 等非界面字段都在，键序相同。区别只有三处：没有编辑器才有的界面字段
`lang`、`uiScale`、`guideSeen`；`connect.callbackAgeSec` 是会话的占位值（编辑器用墙钟覆盖它）；`fixes` 里没有只有墙钟才看得出的“没有回调”。

音频与它的波形只存在于内存：包络的逐格采样就是录音的波形，所以不写进文件；写出去的只有分析给出的数值（结果、事件的时刻与电平）。
输出文件只在宿主测试构建里写，写到测试者自己指定的仓库外目录。

## 拼时间轴：计划时长从哪里读

时长都按样本计，与脚本的 `at` 同一把尺子。最可靠的是读快照（任何一份 step 或 final 文件都有），数值来自 `thresholds.json`：

| 读哪里 | 含义 |
|---|---|
| `snapshot.steps.<步骤>.durationSec` | 这一步的计划时长（空闲时也给出）。方向步追加确认后，进行中的值会变长 |
| `snapshot.live.phase`、`round`、`rounds` | 进行中这一步的阶段与第几遍 |
| `snapshot.live.phaseElapsedSec`、`phaseDurationSec` | 当前这一遍已用与总时长 |
| `snapshot.live.notice`、`noticeSec` | 非 `success` 时这一步要倒数 `noticeSec` 秒后自动重来 |

| 步骤 | 阶段与时长（`thresholds.json` 的键） |
|---|---|
| `connect` | 一段 `no_signal.low_flat_window_s` |
| `gain` | 一段 `session.gain_s` |
| `quiet` | 一段 `session.quiet_s` |
| `room` | 最多 `session.room_max_s`；凑够 `session.room_claps_needed` 下有效拍手、或拍满 `session.room_claps_max` 下就提前收尾 |
| `position` | `soft-clap`：`session.soft_claps` 下 × `session.soft_clap_round_s`；`plosive`、`sibilant`：各 `session.word_list_rounds` 遍 × `session.word_list_round_s`；`natural`：一段 `click.analysis_window_s` |
| `direction` | `direction`：4 个方向 × `session.direction_take_s`；别的方向明显更响时追加 `confirm` 2 遍 × `session.direction_take_s` |

自动重来：阶段收满时要求重来（轻拍削波、念得太轻、门限让拍手整组作废等），先丢掉 `session.notice_s` 秒的音频作为倒数，再从这一阶段
的第一遍重来；一步最多自动重来 `session.restarts_max` 次。时间轴上要给这些留余量，后面的 `startStep` 回 `busy` 时会自己等。

## 对音频与状态的影响

- 不加任何自动化参数，不进插件 state；音频仍然逐位直通。
- 音频线程只做三件事：读一遍块内容判断是否全 0，再做一次 relaxed 原子加一，给本实例的样本计数做一次 relaxed 加法。不分配、不加锁、不做 I/O。
- 文件只在消息线程与宿主的准备线程上写，写文件的动作之间用一把进程内的锁串行化，音频线程碰不到它。
- 自动操作只在测试构建里存在；发布版没有这段代码、没有这些环境变量名，桥与快照的契约不变。

## 给后续代码接钩子

分析线程每做一次分析，调一次 `miciri::probe::noteAnalysis()`（头文件 `src/plugin/HostProbe.h`）。发布构建里它是空函数，
调用不产生任何代码。

## 测试

计数、全 0 判断、JSON 与原子写文件的逻辑不依赖宿主，`tests/plugin/host_probe_test.cpp` 在默认（OFF）构建里照常编译、运行；
带探针的处理器接线由 `tests/plugin/host_probe_processor_test.cpp` 守着，它把处理器源文件在 `MICIRI_HOST_PROBE=1` 下再编一遍。
脚本的解析与按样本秒调度（`busy` 时等待、不合规的脚本一个动作也不执行）在 `tests/plugin/host_probe_script_test.cpp`，同样在 OFF 构建里跑；
`tests/plugin/host_probe_autorun_test.cpp` 用假宿主在没有编辑器时按脚本跑接通步，核对 step、final 文件与快照里的状态。
