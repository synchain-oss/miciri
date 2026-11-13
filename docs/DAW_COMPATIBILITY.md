<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# 宿主兼容表

每一格写宿主版本、验证日期、证据。**验证日期超过 6 个月的格子视为未验证**，要重测。没有写证据的一律是“未测”，不是“应该可以”。

| 宿主 | 平台与格式 | 状态 | 版本 | 验证日期 | 证据 |
|---|---|---|---|---|---|
| REAPER | Windows，VST3 | 自动化测试通过，手测未做 | 7.82 (x64) | 2026-11-12 | 见下面“REAPER（Windows，VST3）” |
| REAPER | macOS，VST3 / AU | 未测 | | | |
| Cubase | | 未测 | | | |
| FL Studio | | 未测 | | | |
| GarageBand / Logic | macOS，AU | 未测 | | | |
| 其他宿主 | | 未测 | | | |

## REAPER（Windows，VST3）

**怎么测的。** 用公开的 [reaper-harness](https://github.com/synchain-oss/reaper-harness)（钉在提交 `b27d4a4`）驱动真实的 REAPER 7.82（x64）和 Windows 10（19045），音频设备是 REAPER 自带的 Dummy Audio（48 kHz、512 样本），插件是带宿主探针的测试构建（探针说明见 [HOST_PROBE.md](HOST_PROBE.md)；发布版不含探针）。10 个场景、4 个自带反例（判据放错必须变红）和工具内置的 8 个反例全部 PASS；每次运行后都没有残留进程，REAPER 默认资源目录没有变化。怎么在本机重跑见 `tests/host/README.md`。

**音频逐位直通。** 7 次渲染（3 种方式 × 偏好开关，外加一次单声道输出）的输出和不挂插件的渲染逐位相同（`identical=yes`，延迟 0，144000 帧，无 NaN 与 Inf）。

**离线标志（`isNonRealtime`）。** REAPER 的偏好“Inform plug-ins of offline rendering state”决定宿主告不告诉插件正在离线渲染。3 秒的渲染里，插件自己数到的块数：

| 渲染方式 | 偏好开 | 偏好关 |
|---|---|---|
| 全速离线 | 非实时 301，实时 0 | 实时 300–301，非实时 0 |
| 1x 离线 | 非实时 300，实时 0 | 实时 300，非实时 0 |
| 在线（实时） | 实时 301–302，非实时 0 | 实时 301，非实时 0 |

要点：**偏好关时，离线渲染对插件也是“实时”**，所以离线保护不能只靠这个标志，见 [KNOWN_ISSUES.md](KNOWN_ISSUES.md)。在走带、arm、监听的所有状态里，插件收到的块都是实时的（非实时块数增量 0）。

**分析次数。** 上面 7 次渲染和 20 次开关窗口里，探针的 `analysisCount` 增量都是 0。限制：宿主测试里没有任何东西会“开始一步”（开始测试要在插件界面里点），所以这一项只证明渲染本身不触发分析、计数器接上了；“宿主报离线时拒绝开始一步”由 `tests/session` 的单元测试守着。

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

**开关编辑器窗口 20 次。** 在走带播放、arm、监听开的状态下，工具开关插件浮窗 20 次：打开 20 次、关闭 20 次，插件自己数到的 `editorOpens`、`editorCloses` 各 +20；期间链运行的块数与设备的块数相同（2566 / 2566），音频不中断。这不证明页面渲染成功：工具只给每次打开留了十圈消息循环。

**这一栏没有测、需要别的办法的：**

- **静音轨自动旁路（`force_auto_bypass`）：未测。** 工具设不了这个标志。
- 偏好关时靠“收到的音频明显快于实时”作废离线渲染这一支：宿主测试里没法开始一步，只能手测（见下）。
- 真实硬件输入、运行中改采样率、48 kHz / 512 以外的采样率与缓冲大小：未测。
- macOS 上的 REAPER、其他格式（AU）：未测。

## 待手测（需要人在机器前）

| 项 | 做法 | 结果 |
|---|---|---|
| REAPER 关掉“Inform plug-ins of offline rendering state”后，开始一步的同时渲染 | 应该看到这一步作废，原因显示为离线渲染 | 待测 |
| 静音轨自动旁路 | 把挂着 Miciri 的轨静音，看测试能不能照常收音 | 待测 |
| Cubase、FL Studio、GarageBand / Logic、macOS 上的 REAPER | 走带、arm、监听各状态下能否收到音频，开关编辑器 20 次，离线导出 | 待测 |

## 没有单声道轨的宿主：提示文案

REAPER 里即使把输出渲染成单声道，插件收到的主总线仍然是两路（`numInputChannels`、`numOutputChannels` 都是 2），所以 REAPER 没有“单声道轨”可改。FL Studio 等宿主是否也没有，**未测**。对这类宿主，两路相同和只有一路有声音这两条提示里不应再建议“改成单声道轨道”。建议文案（**待实测确认后再采用**）：

| 提示 | 中文 | English |
|---|---|---|
| 两路是同一个声音 | 两路是同一个声音，通常是同一支话筒。这个软件的轨道都是立体声，不用改，可以直接录。 | Both channels carry the same sound, usually from one microphone. Tracks in this software are always stereo, so there is nothing to change. |
| 只有一路有声音 | 只有一路有声音，录出来可能只在一边。请检查输入或接线。 | Only one channel has sound, so the recording may only come out on one side. Please check the input or the cable. |

---

# Host compatibility

Each cell gives the host version, the date it was checked and the evidence. **A cell older than 6 months counts as untested** and needs a re-test. A cell without evidence is "untested", not "should work".

| Host | Platform and format | Status | Version | Date | Evidence |
|---|---|---|---|---|---|
| REAPER | Windows, VST3 | Automated tests pass, manual test not done | 7.82 (x64) | 2026-11-12 | See "REAPER (Windows, VST3)" below |
| REAPER | macOS, VST3 / AU | Untested | | | |
| Cubase | | Untested | | | |
| FL Studio | | Untested | | | |
| GarageBand / Logic | macOS, AU | Untested | | | |
| Other hosts | | Untested | | | |

## REAPER (Windows, VST3)

**How it was tested.** The public [reaper-harness](https://github.com/synchain-oss/reaper-harness) (pinned at commit `b27d4a4`) drove a real REAPER 7.82 (x64) on Windows 10 (19045) with REAPER's built-in Dummy Audio device (48 kHz, 512 samples) and a test build of the plug-in that contains the host probe (see [HOST_PROBE.md](HOST_PROBE.md); release builds do not contain it). All 10 scenarios, the 4 negative cases of our own (a wrongly placed criterion must turn red) and the tool's 8 built-in negative cases came out PASS; after every run no process was left behind and REAPER's default resource directory had not changed. How to re-run it locally: `tests/host/README.md`.

**Audio passes through bit for bit.** In all 7 renders (3 render modes × the preference on and off, plus one render to a mono file) the output is identical to the render without the plug-in (`identical=yes`, lag 0, 144000 frames, no NaN or Inf).

**Offline flag (`isNonRealtime`).** REAPER's preference "Inform plug-ins of offline rendering state" decides whether the plug-in is told that it renders offline. Blocks counted by the plug-in itself in a 3 s render:

| Render mode | Preference on | Preference off |
|---|---|---|
| Full-speed offline | non-realtime 301, realtime 0 | realtime 300–301, non-realtime 0 |
| 1x offline | non-realtime 300, realtime 0 | realtime 300, non-realtime 0 |
| Online (real time) | realtime 301–302, non-realtime 0 | realtime 301, non-realtime 0 |

Note: **with the preference off, an offline render looks like real time to the plug-in**, so the offline protection cannot rely on this flag alone; see [KNOWN_ISSUES.md](KNOWN_ISSUES.md). In every transport, arm and monitoring state the plug-in only received real-time blocks (non-realtime growth 0).

**Analysis count.** In the 7 renders above and the 20 window toggles, the probe's `analysisCount` grew by 0. Limit: nothing in a host test "starts a step" (that is done from the plug-in's own page), so this only shows that rendering does not trigger analysis by itself and that the counter is wired; refusing to start a step when the host reports offline rendering is covered by the unit tests in `tests/session`.

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

**Opening and closing the editor window 20 times.** While playing, armed and monitoring, the tool opened and closed the plug-in's floating window 20 times: 20 opens and 20 closes, and the plug-in's own `editorOpens` and `editorCloses` each grew by 20; the chain ran the same number of blocks as the device (2566 / 2566), so audio did not stop. This does not prove that the page rendered: the tool gives each opening ten turns of the message loop.

**Not tested here:**

- **Automatic bypass of muted tracks (`force_auto_bypass`): untested.** The tool cannot set that flag.
- The branch that voids an offline render because the audio arrives clearly faster than real time, with the preference off: a host test cannot start a step, so this needs a manual test (below).
- Real hardware input, changing the sample rate while running, sample rates and buffer sizes other than 48 kHz / 512: untested.
- REAPER on macOS and the AU format: untested.

## To test by hand (a person at the machine)

| Item | How | Result |
|---|---|---|
| Turn off "Inform plug-ins of offline rendering state" in REAPER, start a step and render at the same time | The step should be voided, with offline rendering shown as the reason | To do |
| Automatic bypass of muted tracks | Mute the track with Miciri and see whether a test still receives audio | To do |
| Cubase, FL Studio, GarageBand / Logic, REAPER on macOS | Audio received in each transport, arm and monitoring state, opening the editor 20 times, offline export | To do |

## Hosts without mono tracks: suggested wording

In REAPER the plug-in's main bus is two channels even when the output is rendered to a mono file (`numInputChannels` and `numOutputChannels` are both 2), so there is no "mono track" to switch to. Whether FL Studio and similar hosts are the same is **untested**. For such hosts the two notices "both channels are the same sound" and "only one channel has sound" should no longer suggest "switch to a mono track". Suggested wording (**to be confirmed by a manual test before it is used**) is in the table in the Chinese section above.
