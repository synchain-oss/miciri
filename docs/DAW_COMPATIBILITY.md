<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# 宿主兼容表

每一格写宿主版本、验证日期、证据。**验证日期超过 6 个月的格子视为未验证**，要重测。没有写证据的一律是“未测”，不是“应该可以”。

这张表由 `scripts/checks.d/compat-table.mjs` 核对：已测的行必须写版本、真实存在的 YYYY-MM-DD 日期和证据，中英两张表要一致。过期的行在日常检查里只提示（`WARN`），不判红；发布前用严格模式让它们变红：`MICIRI_COMPAT_STRICT=1 node scripts/run-checks.mjs --only compat-table`。环境变量 `MICIRI_TODAY=YYYY-MM-DD` 可以指定“今天”。

| 宿主 | 平台与格式 | 状态 | 版本 | 验证日期 | 证据 |
|---|---|---|---|---|---|
| REAPER | Windows，VST3 | 音频与界面启动都已进入自动化测试（界面：开窗保持 10 秒，页面完成启动，开窗后截图；有合成输入时电平表与削波灯的截图也看过）；手测未做 | 7.82 (x64) | 2026-11-26 | 见下面“REAPER（Windows，VST3）” |
| REAPER | macOS，VST3 / AU | 未测 | | | |
| Cubase | Windows，VST3 | 界面启动缺陷（所有宿主共有）已修复，界面已复测通过（单声道轨，提交 `d79e994`）：测试流程能走完；走带、arm、监听各状态与离线导出未测 | 15 | 2026-11-24 | 见下面“Cubase（Windows，VST3）” |
| FL Studio | | 未测 | | | |
| GarageBand / Logic | macOS，AU | 未测 | | | |
| 其他宿主 | | 未测 | | | |

## REAPER（Windows，VST3）

**怎么测的。** 用公开的 [reaper-harness](https://github.com/synchain-oss/reaper-harness)（钉在提交 `b360827`）驱动真实的 REAPER 7.82（x64）和 Windows 10（19045），音频设备是 REAPER 自带的 Dummy Audio（48 kHz、512 样本），插件是带宿主探针的测试构建（探针说明见 [HOST_PROBE.md](HOST_PROBE.md)；发布版不含探针），最近一次是 feature/v1 的提交 `8025771`（接通页自动观察和各段手动开始都已合入）。验证日期 2026-11-26，整套重跑：13 个场景（比第一次多了界面启动 `ui-start`、电平表 `meter-steady` 与削波灯 `meter-clip-lamp`）、5 个自带反例（判据放错必须变红）和工具内置的 8 个反例全部 PASS（全程启动 REAPER 30 次，其中一次是 learn 遇到 REAPER 评估版提示后由工具自动重试）；运行后没有残留进程，REAPER 默认资源目录没有变化。更早在提交 `3a4949b` 的构建上整套也全部 PASS（启动 29 次）。下面各表的数值是 2026-11-23 第一次跑的（钉版 `b27d4a4`，测试构建提交 `391169e`）；2026-11-26 重跑的判定全部一致。块数在更早的一次整套重跑里只有一次超出表里的区间：在线渲染、偏好开的一次是 310，表里是 301–303（在线渲染按真实时间走，块数随当时机器的负载有出入）；在 `8025771` 上各次渲染的块数都在表里的区间内。怎么在本机重跑见 `tests/host/README.md`。

**音频逐位直通。** 7 次渲染（3 种方式 × 偏好开关，外加一次单声道输出）的输出和不挂插件的渲染逐位相同（`identical=yes`，延迟 0，144000 帧，无 NaN 与 Inf）。

**离线标志（`isNonRealtime`）。** REAPER 的偏好“Inform plug-ins of offline rendering state”决定宿主告不告诉插件正在离线渲染。3 秒的渲染里，插件自己数到的块数：

| 渲染方式 | 偏好开 | 偏好关 |
|---|---|---|
| 全速离线 | 非实时 301，实时 0 | 实时 300–301，非实时 0 |
| 1x 离线 | 非实时 300，实时 0 | 实时 300，非实时 0 |
| 在线（实时） | 实时 301–303，非实时 0 | 实时 301–302，非实时 0 |

要点：**偏好关时，离线渲染对插件也是“实时”**，所以离线保护不能只靠这个标志，见 [KNOWN_ISSUES.md](KNOWN_ISSUES.md)。在走带、arm、监听的所有状态里，插件收到的块都是实时的（非实时块数增量 0）。

**分析次数。** 上面 7 次渲染和 20 次开关窗口里，探针的 `analysisCount` 增量都是 0。限制：宿主测试的脚本不开始任何一步（测试要在插件界面里开始），所以这一项只证明渲染本身不触发分析、计数器接上了；“宿主报离线时拒绝开始一步”由 `tests/session` 的单元测试守着。开着编辑器、信号还不正常时界面会自己开接通观察（一轮做完计入分析次数，关窗即取消）；接上它之后 2026-11-26 复测 20 次开关窗口，`analysisCount` 仍是 +0。在提交 `8025771` 上整套重跑的增量：7 次渲染、走带 12 格（两遍）和 20 次开关窗口都是 +0；窗口开着不动时接通页一轮一轮地观察，`ui-start` 冷启动（输入静音）+3、重开 +0，`meter-steady` 与 `meter-clip-lamp`（有信号）各 +3。这三个场景不判 `analysisCount`。改成停在哪一页都观察之后（提交 `0c122e1`）同一天再测 20 次开关窗口，仍是 +0。

**走带、arm、监听 12 格。** 轨道上只有 Miciri，输入由工具往输入 FX 链喂一段立体声信号（两路不同）。“Run FX when stopped”开（REAPER 默认）与关各跑一遍：

| 轨道 | 走带 | 链被运行 | 插件收到的输入 | 监听生效 |
|---|---|---|---|---|
| 未 arm | 停止 / 播放 | 开：是；关：否 | 全 0 | 否 |
| 未 arm | 暂停 | 否 | | 否 |
| arm，监听关 | 停止 / 播放 / 暂停 | 是 | 全 0 | 否 |
| arm，监听开 | 停止 / 播放 / 暂停 | 是 | 有信号 | 是 |
| arm，监听自动 | 停止 | 是 | 有信号 | 是 |
| arm，监听自动 | 播放 | 是 | 全 0 | 否 |
| arm，监听自动 | 暂停 | 是 | 有信号 | 是 |

每格约 190 个块里，工具的探针和插件自己数的“全 0 块 / 有内容块”一致：有信号的 5 格没有全 0 块，其余各格没有有内容的块。也就是说，只有 arm 且监听生效的 5 格，Miciri 才收到话筒信号（这正是插件里“监听中才测试”的前提）。

**开关编辑器窗口 20 次。** 在走带播放、arm、监听开的状态下，工具开关插件浮窗 20 次：打开 20 次、关闭 20 次，插件自己数到的 `editorOpens`、`editorCloses` 各 +20；期间链运行的块数与设备的块数相同（2026-11-23 是 2016 / 2016，2026-11-26 重跑是 5825 / 5825，块数随开关窗口花的时间而变），音频不中断。这一项不证明页面渲染成功：工具只给每次打开留了十圈消息循环；页面有没有起来由下面的“界面启动”判。

**界面启动：2026-11-24 才查，2026-11-26 起进入自动化。** 上面这些项都只看音频和计数，分析由测试构建的探针从原生一侧驱动，不经过页面；开关窗口那一项也不等页面起来。所以在这之前，界面在任何宿主里都没有验证过。2026-11-24 在 REAPER 7.82 里把插件窗口打开 20 秒（私有配置的副本，测试构建）：提交 `bd472d4` 的构建先显示外壳，主区一直空着，15 秒后切到兜底面板“页面加载完了，但插件界面一直没有完成启动”，和 Cubase 15 里的表现一样（原因见下面 Cubase 一节）。修复后同样打开 20 秒，页面完成启动，步骤卡、电平、采样率与缓冲都在，探针的 `uiReady` 为 1、`uiFallback` 为 0。之后由 ctest 的 `plugin_editor_open_close` 在真实 WebView2 里装真页面、走真桥守着（只在 Windows 上跑）。

**界面启动的自动化（`ui-start`）。** 宿主测试工具现在能让窗口开着不动，也能在开窗后的几个时刻截图，所以界面进了宿主矩阵：把插件浮窗开着不动 10 秒，探针的三个计数按开窗前后的增量判（计数从处理器创建起累计）。判据：`uiReady` +1、`uiFallback` +0、`bridgeRoundTrips` 至少 +1，编辑器开关各 +1（`bridgeRoundTrips` 连兜底面板回的 not-ready 也计数，所以必须和 `uiReady` 一起判）。两个状态：第一次开窗（冷启动）和关掉再开一次，各在开窗后 300、2000、8000 ms 截一张图；图没截齐由工具判 INCOMPLETE，不算通过。2026-11-26 在提交 `3a4949b` 的测试构建上两个状态都 PASS：冷启动与重开各保持 10 秒（10008 ms、10021 ms），`uiReady`、`bridgeRoundTrips` 各 +1，`uiFallback` +0，6 张图都截到了（1120×727，96 DPI，插件视图在 0,27 起的 1120×700）。300 ms 时视图还只是渐变占位，2000 ms 与 8000 ms 时界面完整（步骤卡、电平、采样率与缓冲都在，还带着首次使用的引导气泡）。图只在本机，不进仓；判据只看计数，不看图里画了什么。反例：在提交 `3dc6613` 的构建上，把 `check_native_interop.js` 的打包去掉（只在本机构建，没有提交）再跑同一个场景，两个状态都变红，`uiReady` +0、`uiFallback` +1、`bridgeRoundTrips` +0，图里是兜底面板，诊断行写着取不到 `js/juce/index.js` 的动态导入。另有一个自带反例把 `uiReady` 的期望写成 2，必须变红，每次整套都跑。

**先全零、后有信号的再开窗（`ui-signal-return`）。** `ui-start` 两个状态的输入都是全零，看不出信号回来之后界面会不会自己恢复。这个场景在同一个 REAPER 进程里喂 `meter-steady` 的合成正弦，走两个状态：先停、不 arm、监听关（插件收到全零），开窗 10 秒，在 2000、8000 ms 截图；再播放、arm、监听开（信号进来），开窗 12 秒，在 300、2000、5000、8000、11000 ms 截图。判据和 `ui-start` 一样只看计数，另加输入的前提（第一个状态全静音；第二个状态每块都有信号、两路相同、监听生效）。2026-11-26 在提交 `0c122e1` 的测试构建上两个状态都 PASS，7 张图都截到了。人看图：第一个状态 8000 ms 时是“样本全是零”，“开始增益测试”是灰的；第二个状态再开窗落在接通页（不跳到增益），2000、5000 ms 时电平已到 −9 dBTP、药丸仍是关窗前的“全是零”（这一轮接通观察还没做完），8000、11000 ms 时变成“收到信号了”，药丸转绿，按钮可点。同一次 `ui-start` 的重开状态落在接通页、写“样本全是零”，不再跳到增益页。

**电平表与削波灯的截图（`meter-steady`、`meter-clip-lamp`）。** 上面各场景里电平表没有输入，什么也不画。这两个场景在走带播放、arm、监听开的状态下，用工具的 `feed` 往输入循环喂合成 WAV（`tests/host/make-feed.ps1` 现场生成，只含合成信号，单声道、两路相同），把窗口开着不动并定时截图。工具判的只有：保持时间、图拿全、`uiReady` +1、`uiFallback` +0、`bridgeRoundTrips` 至少 +1，以及信号确实到了输入；2026-11-26 两个场景都 PASS（3/3 张与 8/8 张）。**图里电平表画成什么样不进通过判据**：探针不报电平，从像素读数又要依赖主题颜色与版面尺寸，所以读数用本机脚本做、再由人看图核对。读法：左栏电平管的槽是线性 dB 刻度，0 dBFS 在槽顶、−80 在槽底（`meter-core.js` 的 `ratio`；`CEIL`（6）只限制状态值的上限，不抬高刻度的顶），槽的位置用刻度标签与目标区的边线（−6、−12）对过；1 个像素约 0.23 dB。

- **稳态（`meter-steady`）：** 1 kHz 正弦，峰值 −9 dBFS，保持 10 秒，2000、5000、8000 ms 三张读数相同：液面顶 −9.0、峰值线 −9.0，淡色内芯（均方根）左 −11.7、右 −12.0（理论值 −12.0，左边差 1 个像素），灯不亮；文字读数 −9.0 dBTP。接通页自己开始观察信号之后（2026-11-26 复测），8000 ms 那张里接通页已是“收到信号了”、左栏药丸是“信号正常”、“开始增益测试”可点；在这之前接通页一直停在“等你出声”。
- **削波（`meter-clip-lamp`）：** 12 秒一圈（开头 50 ms 满幅方波，其余 −30 dBFS 正弦），保持 32 秒，每 4 秒一张，2000 到 30000 ms 共 8 张，两路一致。**每次削波之后，只要 5 秒内还有图要拍，就至少落进一张亮着的图，所以亮、灭、再亮都能拍到；具体哪几张图里灯亮，取决于开窗相对循环的相位**（最后一张图之后的削波拍不到）。这个相位每次运行不同，灯的序列不是固定的。跑过三次：前两次（单独跑这两个场景一次，整套里一次）削波落在开窗后约 5.4、17.4、29.4 秒，8 张图里灯依次是灭、亮、亮、灭、亮、亮、灭、亮；在 `8025771` 上的整套里削波落在约 6.8、18.7 秒，灯依次是灭、灭、亮、灭、灭、亮、灭、灭（第三次削波在约 30.7 秒，晚于最后一张图）。读数随距削波的时间而定：约 0.6 秒的图（前两次的 6000、18000、30000 ms）峰值线停在槽顶（带红色）、液面约 −18 到 −20 并在以每秒 30 dB 落回、文字读数 +2.1 dBTP；约 3 到 4.6 秒的图灯还亮着，液面与内芯已落回 −30.1 与 −33.1，峰值线在停住约 2.2 秒之后以每秒 20 dB 下落（`8025771` 上距削波约 3.2 秒的两张读到 −20.5 与 −22.5）；约 7 到 8.6 秒的图灯灭；下一圈削波后灯再亮。
- **看不出来的：** 5 秒的熄灯点截图定不准（只拍到 4.6 秒时还亮、8.6 秒时已灭），精确时序由单元测试管；开窗之前发生的削波不点亮灯（打开编辑器时清基线），这一点由单元测试（`resetMeters` 那一条）管，截图里没有专门验证。

**这一栏没有测、需要别的办法的：**

- **静音轨自动旁路（`force_auto_bypass`）：未测。** 工具设不了这个标志。
- 偏好关时靠“收到的音频明显快于实时”作废离线渲染这一支：宿主测试里没法开始一步，只能手测（见下）。
- 界面的手动操作（点步骤、切语言）：在 REAPER 里没测。自动化只判页面完成启动、桥通了，图要人看；截图只在 96 DPI 下量过。电平表只在合成输入下截图看过（见上），没有用真实录音和真实硬件输入测。
- 真实硬件输入、运行中改采样率、48 kHz / 512 以外的采样率与缓冲大小：未测。
- macOS 上的 REAPER、其他格式（AU）：未测。

## Cubase（Windows，VST3）

**发现了什么。** 2026-11-24 维护者在 Cubase 15（Windows）里打开提交 `bd472d4` 的预览构建：窗口先是很久的浅色占位（看起来像白屏），然后外壳出来了，但主区空白、电平没有动静、下拉框是空的，切语言没有反应；约 15 秒后切到兜底面板“页面加载完了，但插件界面一直没有完成启动”，点重试也一样。

**原因（所有宿主都一样，不是 Cubase 特有）。** 页面经 JUCE 的前端桥脚本 `js/juce/index.js` 调原生，这个脚本第一行就 import 同目录的 `check_native_interop.js`，而构建只把前者编进了插件。真宿主里这个 import 失败，整个桥脚本加载不了，页面一个桥调用都发不出去：拿不到第一份快照，看门狗 15 秒后切兜底面板。浏览器预览用的是假桥，从不加载这个脚本，所以页面冒烟没发现。REAPER 里同样复现（见上）。浅色占位是另一个问题：原生一侧的开窗占位还是早期的浅灰配色，和页面的紫粉渐变对不上；页面也没有发首帧信号，窗口每次都要等 3 秒超时才揭开。

**修复。** 两个脚本一起编进插件（单元测试核对桥脚本的每个相对 import 都取得到）；新增真页面启动测试；页面在启动早期就报告各阶段，出错时立刻切兜底面板，诊断行写出卡在哪一步；外壳一画出来就揭开窗口，不再固定等 3 秒（开局画面的配色另外单独修改）；兜底面板跟随界面语言。2026-11-24 维护者用提交 `d79e994` 的预览构建在 Cubase 15（Windows）的单声道轨上复测：界面正常启动，测试流程能走完，各项都出了读数，所以这一行的状态是“界面已复测通过”。仍未测：走带、arm、监听各状态，开关编辑器 20 次，离线导出。

## 待手测（需要人在机器前）

| 项 | 做法 | 结果 |
|---|---|---|
| REAPER 关掉“Inform plug-ins of offline rendering state”后，开始一步的同时渲染 | 应该看到这一步作废，原因显示为离线渲染 | 待测 |
| 静音轨自动旁路 | 把挂着 Miciri 的轨静音，看测试能不能照常收音 | 待测 |
| Cubase 15（Windows）界面复测 | 用修复后的构建打开插件窗口，等页面完成启动；切一次语言；万一还出兜底面板，把诊断行（含 `script … > frame … > bridge … > ready …` 那一行）发回来 | 已完成（2026-11-24，提交 `d79e994`，单声道轨：界面正常启动，测试流程能走完） |
| Cubase、FL Studio、GarageBand / Logic、macOS 上的 REAPER | 走带、arm、监听各状态下能否收到音频，开关编辑器 20 次，离线导出（Cubase 里这三项仍未测） | 待测 |

## 没有单声道轨的宿主：提示文案

REAPER 里即使把输出渲染成单声道，插件收到的主总线仍然是两路（`numInputChannels`、`numOutputChannels` 都是 2），所以 REAPER 没有“单声道轨”可改。FL Studio 等宿主是否也没有，**未测**。对这类宿主，两路相同和只有一路有声音这两条提示里不应再建议“改成单声道轨道”。建议文案（**待实测确认后再采用**）：

| 提示 | 中文 | English |
|---|---|---|
| 两路是同一个声音 | 两路是同一个声音，通常是同一支话筒。这个软件的轨道都是立体声，不用改，可以直接录。 | Both channels carry the same sound, usually from one microphone. Tracks in this software are always stereo, so there is nothing to change. |
| 只有一路有声音 | 只有一路有声音，录出来可能只在一边。请检查输入或接线。 | Only one channel has sound, so the recording may only come out on one side. Please check the input or the cable. |

---

# Host compatibility

Each cell gives the host version, the date it was checked and the evidence. **A cell older than 6 months counts as untested** and needs a re-test. A cell without evidence is "untested", not "should work".

This table is checked by `scripts/checks.d/compat-table.mjs`: a tested row must have a version, a real YYYY-MM-DD date and evidence, and the Chinese and English tables must agree. Expired rows only produce a `WARN` in the everyday checks and do not fail them; before a release, strict mode turns them red: `MICIRI_COMPAT_STRICT=1 node scripts/run-checks.mjs --only compat-table`. The environment variable `MICIRI_TODAY=YYYY-MM-DD` sets "today".

| Host | Platform and format | Status | Version | Date | Evidence |
|---|---|---|---|---|---|
| REAPER | Windows, VST3 | Both audio and UI start-up are in the automated tests (UI: window held open for 10 s, the page finishes starting, screenshots after opening; the meters and the clip lamp with a synthetic input were also checked in screenshots); manual test not done | 7.82 (x64) | 2026-11-26 | See "REAPER (Windows, VST3)" below |
| REAPER | macOS, VST3 / AU | Untested | | | |
| Cubase | Windows, VST3 | The UI start-up defect (shared by all hosts) is fixed and the UI re-test passed (mono track, commit `d79e994`): the test flow runs to the end; transport, arm and monitoring states and offline export untested | 15 | 2026-11-24 | See "Cubase (Windows, VST3)" below |
| FL Studio | | Untested | | | |
| GarageBand / Logic | macOS, AU | Untested | | | |
| Other hosts | | Untested | | | |

## REAPER (Windows, VST3)

**How it was tested.** The public [reaper-harness](https://github.com/synchain-oss/reaper-harness) (pinned at commit `b360827`) drove a real REAPER 7.82 (x64) on Windows 10 (19045) with REAPER's built-in Dummy Audio device (48 kHz, 512 samples) and a test build of the plug-in that contains the host probe (see [HOST_PROBE.md](HOST_PROBE.md); release builds do not contain it), the latest one at feature/v1 commit `8025771` (the connect page's own observation and the manually started parts are both in). Checked on 2026-11-26, the whole suite re-run: 13 scenarios (two more than the first time: the UI start-up scenario `ui-start`, and the meter scenarios `meter-steady` and `meter-clip-lamp`), the 5 negative cases of our own (a wrongly placed criterion must turn red) and the tool's 8 built-in negative cases all came out PASS (REAPER was started 30 times in total, one of them an automatic retry by the tool after `learn` hit REAPER's evaluation reminder); after the run no process was left behind and REAPER's default resource directory had not changed. Earlier the whole suite also passed on a build of commit `3a4949b` (29 launches). The numbers in the tables below are from the first run on 2026-11-23 (pin `b27d4a4`, test build at commit `391169e`); the verdicts of the 2026-11-26 re-run are all the same. In an earlier whole-suite re-run the block counts of the renders were all inside the ranges in the tables except one: the online render with the preference on counted 310 (the table says 301–303; an online render runs in real time, so its count varies with the load on the machine at that moment); at `8025771` all render block counts are inside the ranges. How to re-run it locally: `tests/host/README.md`.

**Audio passes through bit for bit.** In all 7 renders (3 render modes × the preference on and off, plus one render to a mono file) the output is identical to the render without the plug-in (`identical=yes`, lag 0, 144000 frames, no NaN or Inf).

**Offline flag (`isNonRealtime`).** REAPER's preference "Inform plug-ins of offline rendering state" decides whether the plug-in is told that it renders offline. Blocks counted by the plug-in itself in a 3 s render:

| Render mode | Preference on | Preference off |
|---|---|---|
| Full-speed offline | non-realtime 301, realtime 0 | realtime 300–301, non-realtime 0 |
| 1x offline | non-realtime 300, realtime 0 | realtime 300, non-realtime 0 |
| Online (real time) | realtime 301–303, non-realtime 0 | realtime 301–302, non-realtime 0 |

Note: **with the preference off, an offline render looks like real time to the plug-in**, so the offline protection cannot rely on this flag alone; see [KNOWN_ISSUES.md](KNOWN_ISSUES.md). In every transport, arm and monitoring state the plug-in only received real-time blocks (non-realtime growth 0).

**Analysis count.** In the 7 renders above and the 20 window toggles, the probe's `analysisCount` grew by 0. Limit: no host-test script starts a step (tests are started from the plug-in's own page), so this only shows that rendering does not trigger analysis by itself and that the counter is wired; refusing to start a step when the host reports offline rendering is covered by the unit tests in `tests/session`. With the editor open and the signal not fine yet, the page starts watching the signal on its own (each finished round counts as an analysis; closing the window cancels it); after that change the 20 window toggles were run again on 2026-11-26 and `analysisCount` still grew by 0. In the whole-suite re-run at commit `8025771` the increments were: 0 in the 7 renders, the 12 transport cells (both passes) and the 20 window toggles; with the window held open the connect page observes round after round, and `analysisCount` grew by 3 in the cold start of `ui-start` (silent input), by 0 in its reopen, and by 3 each in `meter-steady` and `meter-clip-lamp` (with signal). These three scenarios do not judge `analysisCount`. After the watching was extended to every page (commit `0c122e1`) the 20 window toggles were run again the same day: still 0.

**Transport, arm and monitoring: 12 cells.** Miciri is the only plug-in on the track; the tool feeds a stereo signal (two different channels) into the input FX chain. Run once with "Run FX when stopped" on (REAPER's default) and once off:

| Track | Transport | FX chain is run | What the plug-in receives | Monitoring in effect |
|---|---|---|---|---|
| not armed | stopped / playing | on: yes; off: no | all zeros | no |
| not armed | paused | no | | no |
| armed, monitoring off | stopped / playing / paused | yes | all zeros | no |
| armed, monitoring on | stopped / playing / paused | yes | signal | yes |
| armed, monitoring auto | stopped | yes | signal | yes |
| armed, monitoring auto | playing | yes | all zeros | no |
| armed, monitoring auto | paused | yes | signal | yes |

In about 190 blocks per cell, the tool's probe and the plug-in's own counts of all-zero and non-zero blocks agree: the 5 cells with signal have no all-zero block, the other cells have no block with content. So only in the 5 cells that are armed with monitoring in effect does Miciri receive microphone signal, which is exactly what "test only while monitoring" relies on.

**Opening and closing the editor window 20 times.** While playing, armed and monitoring, the tool opened and closed the plug-in's floating window 20 times: 20 opens and 20 closes, and the plug-in's own `editorOpens` and `editorCloses` each grew by 20; the chain ran the same number of blocks as the device (2016 / 2016 on 2026-11-23, 5825 / 5825 in the re-run on 2026-11-26; the count depends on how long the window toggling takes), so audio did not stop. This item does not prove that the page rendered: the tool gives each opening ten turns of the message loop; whether the page started is judged by "UI start-up" below.

**UI start-up: first checked on 2026-11-24, in the automated tests since 2026-11-26.** Everything above looks only at audio and counters; analysis is driven by the test build's probe from the native side, not through the page, and the window toggles do not wait for the page either. So until then the UI had not been verified in any host. On 2026-11-24 the plug-in window was kept open for 20 s in REAPER 7.82 (a copy of a private configuration, test build): the build at commit `bd472d4` showed the shell with an empty main area and after 15 s switched to the fallback panel "The page loaded, but the plugin UI never finished starting up", the same as in Cubase 15 (cause in the Cubase section below). After the fix, kept open for 20 s again, the page finished starting: step cards, meters, sample rate and buffer size all shown, the probe's `uiReady` 1 and `uiFallback` 0. From then on ctest's `plugin_editor_open_close` loads the real page through the real bridge in a real WebView2 (Windows only).

**UI start-up in the automated tests (`ui-start`).** The host-test tool can now keep a window open and take screenshots at moments after it opens, so the UI is in the host matrix: the plug-in's floating window is kept open for 10 s and the probe's three counters are judged as increments over the opening (they accumulate from the creation of the processor). Criteria: `uiReady` +1, `uiFallback` +0, `bridgeRoundTrips` at least +1, editor opens and closes +1 each (`bridgeRoundTrips` also counts the not-ready replies of the fallback panel, so it is only judged together with `uiReady`). Two states: the first opening (a cold start) and one more after closing it, each with a screenshot 300, 2000 and 8000 ms after the window opens; a state whose screenshots are not all taken is INCOMPLETE in the tool and does not count as a pass. On 2026-11-26 both states passed with the test build at commit `3a4949b`: the cold start and the reopen were each held for 10 s (10008 ms and 10021 ms), `uiReady` and `bridgeRoundTrips` grew by 1 each, `uiFallback` by 0, and all 6 images were taken (1120×727, 96 DPI, the plug-in's view at 0,27 and 1120×700). At 300 ms the view was only the gradient placeholder; at 2000 ms and 8000 ms the UI was complete (step cards, meters, sample rate and buffer size, plus the first-use guide bubble). The images stay on the local machine and are not in the repository; the criteria look only at the counters, not at what the images show. Counter-example: on a build of commit `3dc6613` with the packaging of `check_native_interop.js` taken out (built locally only, not committed) the same scenario turned red in both states, `uiReady` +0, `uiFallback` +1, `bridgeRoundTrips` +0, and the images show the fallback panel with a diagnostic line saying the dynamic import of `js/juce/index.js` could not be fetched. A negative case of our own that expects `uiReady` to grow by 2 must also turn red; it runs with every full run.

**Reopening after all-zero input, then with a signal (`ui-signal-return`).** Both states of `ui-start` get all-zero input, so they cannot show whether the UI recovers by itself once a signal arrives. This scenario feeds the synthetic sine of `meter-steady` in one REAPER process through two states: first stopped, unarmed, monitoring off (the plug-in gets exact zeros), window held for 10 s with images at 2000 and 8000 ms; then playing, armed, monitoring on (the signal arrives), window held for 12 s with images at 300, 2000, 5000, 8000 and 11000 ms. The criteria are the counters of `ui-start` plus the input preconditions (all silent in the first state; signal in every block, both channels equal and monitoring in effect in the second). On 2026-11-26 both states passed with the test build at commit `0c122e1` and all 7 images were taken. Looked at by eye: at 8000 ms in the first state the page says the samples are all zero and "Start the gain test" is grayed out; in the second state the window opens on the connect page again (not on the gain step), at 2000 and 5000 ms the meter is at −9 dBTP while the pill still shows the all-zero state from before the window was closed (the round of watching has not finished yet), and at 8000 and 11000 ms the page says the signal was received, the pill is green and the button is available. In the same run the reopen state of `ui-start` opens on the connect page with the all-zero diagnosis instead of jumping to the gain step.

**Screenshots of the meters and the clip lamp (`meter-steady`, `meter-clip-lamp`).** In the scenarios above the meters have no input and draw nothing. These two scenarios, with the transport playing, the track armed and monitoring on, feed a looped synthetic WAV into the input through the tool's `feed` (generated on the spot by `tests/host/make-feed.ps1`, synthetic signals only, mono, both channels the same), keep the window open and take screenshots on a timer. The tool judges only: the hold time, all screenshots taken, `uiReady` +1, `uiFallback` +0, `bridgeRoundTrips` at least +1, and that the signal really reached the input; on 2026-11-26 both scenarios passed (3/3 and 8/8 images). **What the meters look like in the images is not a pass criterion**: the probe does not report levels, and reading pixels depends on the theme colours and the layout dimensions, so the readings are taken with a local script and then checked by a person looking at the images. How they are read: the slot of the left-hand meter tube is a linear dB scale, 0 dBFS at the top of the slot and −80 at the bottom (`ratio` in `meter-core.js`; `CEIL` (6) only limits the state value and does not raise the top of the scale), and the position of the slot was checked against the tick labels and the edges of the target zone (−6, −12); one pixel is about 0.23 dB.

- **Steady (`meter-steady`):** a 1 kHz sine with a −9 dBFS peak, held for 10 s; the three images at 2000, 5000 and 8000 ms read the same: liquid top −9.0, peak line −9.0, pale core (RMS) −11.7 on the left and −12.0 on the right (theory −12.0; the left one is one pixel off), lamp off; the text readout says −9.0 dBTP. Since the connect page starts watching the signal on its own (re-run on 2026-11-26), the 8000 ms image shows the connect page titled "Signal received" (the Chinese UI was captured), the pill in the left column at "Signal OK", and "Start the gain test" enabled; before that the connect page stayed on "Waiting for sound".
- **Clipping (`meter-clip-lamp`):** a 12 s loop (the first 50 ms a full-scale square wave, the rest a −30 dBFS sine), held for 32 s with one image every 4 s, 8 images from 2000 to 30000 ms, the same on both channels. **After each clip, as long as there are still images to take within 5 s, at least one image falls in the time the lamp is lit, so lit, off and lit again can all be captured; which images show the lamp lit depends on the phase of the window opening against the loop** (a clip after the last image cannot be captured). That phase differs from run to run, so the lamp sequence is not fixed. It was run three times: in the first two (the two scenarios alone once, inside the whole suite once) the clips fell about 5.4, 17.4 and 29.4 s after the window opened, and the lamp over the 8 images was off, lit, lit, off, lit, lit, off, lit; in the whole suite at `8025771` the clips fell about 6.8 and 18.7 s after, and the lamp was off, off, lit, off, off, lit, off, off (the third clip came at about 30.7 s, after the last image). The readings depend on the time since the clip: in an image about 0.6 s after a clip (6000, 18000 and 30000 ms in the first two runs) the peak line sits at the top of the slot (in red), the liquid is at about −18 to −20 and falling at 30 dB per second, and the text readout says +2.1 dBTP; in an image about 3 to 4.6 s after, the lamp is still lit, the liquid and core are back at −30.1 and −33.1, and the peak line, after holding for about 2.2 s, falls at 20 dB per second (the two images about 3.2 s after a clip at `8025771` read −20.5 and −22.5); in an image about 7 to 8.6 s after, the lamp is off; after the next loop's clip it is lit again.
- **What the images cannot show:** the 5 s point where the lamp goes out cannot be pinned down by screenshots (it was still lit at 4.6 s and already off at 8.6 s); the exact timing is covered by the unit tests. That a clip before the window opened does not light the lamp (the baseline is cleared when the editor opens) is covered by a unit test (the one for `resetMeters`); the screenshots do not verify it on their own.

**Not tested here:**

- **Automatic bypass of muted tracks (`force_auto_bypass`): untested.** The tool cannot set that flag.
- The branch that voids an offline render because the audio arrives clearly faster than real time, with the preference off: a host test cannot start a step, so this needs a manual test (below).
- Manual use of the UI (clicking through the steps, switching the language): not tested in REAPER. The automated check judges only that the page finished starting and the bridge works; the images are for a person to look at, and screenshots have only been measured at 96 DPI. The meters have been looked at in screenshots only with synthetic input (above), not with a real recording or real hardware input.
- Real hardware input, changing the sample rate while running, sample rates and buffer sizes other than 48 kHz / 512: untested.
- REAPER on macOS and the AU format: untested.

## Cubase (Windows, VST3)

**What was found.** On 2026-11-24 a maintainer opened the preview build at commit `bd472d4` in Cubase 15 (Windows): the window first showed a pale placeholder for a long time (it looked white), then the shell appeared but the main area stayed empty, the meter did not move, the drop-down was empty and switching the language did nothing; after about 15 s it switched to the fallback panel "The page loaded, but the plugin UI never finished starting up", and Retry did the same again.

**Cause (the same in every host, not specific to Cubase).** The page calls the native side through JUCE's front-end bridge script `js/juce/index.js`, whose first line imports `check_native_interop.js` from the same folder, and the build only compiled the former into the plug-in. In a real host that import fails, the bridge script cannot load and the page cannot make a single bridge call: it never gets its first snapshot, and the watchdog switches to the fallback panel after 15 s. The browser preview uses a mock bridge and never loads this script, so the page smoke tests did not catch it. REAPER shows the same (above). The pale placeholder is a separate problem: the native placeholder still had an early light-grey palette that does not match the page's purple-to-pink gradient, and the page sent no first-frame signal, so the window always waited for the 3 s timeout before it was revealed.

**Fix.** Both scripts are compiled in (a unit test checks that every relative import of the bridge script is served); a new test starts the real page; the page reports its start-up stages early and a failure switches to the fallback panel at once, with a diagnostics line that shows where it stopped; the window is revealed as soon as the shell is drawn instead of always waiting 3 s (the opening screen's colours are changed separately); the fallback panel follows the UI language. On 2026-11-24 a maintainer re-tested the preview build at commit `d79e994` in Cubase 15 (Windows) on a mono track: the UI starts normally and the test flow runs to the end with a reading for every step, so this row says "UI re-test passed". Still untested: transport, arm and monitoring states, opening the editor 20 times, and offline export.

## To test by hand (a person at the machine)

| Item | How | Result |
|---|---|---|
| Turn off "Inform plug-ins of offline rendering state" in REAPER, start a step and render at the same time | The step should be voided, with offline rendering shown as the reason | To do |
| Automatic bypass of muted tracks | Mute the track with Miciri and see whether a test still receives audio | To do |
| Cubase 15 (Windows) UI re-test | Open the plug-in window with the fixed build and wait for the page to finish starting; switch the language once; if the fallback panel still appears, send back the diagnostics (including the `script … > frame … > bridge … > ready …` line) | Done (2026-11-24, commit `d79e994`, mono track: the UI starts normally and the test flow runs to the end) |
| Cubase, FL Studio, GarageBand / Logic, REAPER on macOS | Audio received in each transport, arm and monitoring state, opening the editor 20 times, offline export (these three are still untested in Cubase) | To do |

## Hosts without mono tracks: suggested wording

In REAPER the plug-in's main bus is two channels even when the output is rendered to a mono file (`numInputChannels` and `numOutputChannels` are both 2), so there is no "mono track" to switch to. Whether FL Studio and similar hosts are the same is **untested**. For such hosts the two notices "both channels are the same sound" and "only one channel has sound" should no longer suggest "switch to a mono track". Suggested wording (**to be confirmed by a manual test before it is used**) is in the table in the Chinese section above.
