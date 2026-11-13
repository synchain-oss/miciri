<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# 已知问题

## 离线渲染时测试的保护

Miciri 只在实时播放或监听时做测试。宿主告诉插件“正在离线渲染”时，测试不会开始，正在进行的那一步作废。

有的宿主可以关掉这个通知（例如 REAPER 的 “Inform plug-ins of offline rendering state”），这时离线渲染也报成实时。
Miciri 另外比较收到的音频时长与实际经过的时间：明显快于实时（`session.realtime_ratio_max` 倍以上）就把这一步作废，
原因显示为离线渲染。局限：

- 这一判断要等测试开始后至少 `session.realtime_check_min_s` 秒，或者这一步收满时才做；在那之前收到的音频不会被分析，
  房间步边收边看拍手的那几次除外（它们的结果随这一步一起作废）。
- 渲染速度不快于实时（例如宿主的“实时渲染”或很重的工程）时分不出来：这时插件收到的是工程里的音频，测出来的是那段音频，
  不是话筒。测试时请不要同时渲染。

## Test protection during offline rendering

Miciri runs its tests only during real-time playback or monitoring. When the host tells the plug-in that it is rendering
offline, a test will not start, and a step already in progress is voided.

Some hosts let you turn that notice off (for example REAPER's "Inform plug-ins of offline rendering state"), and then
an offline render is reported as real time. Miciri also compares how much audio has arrived with how much time has
actually passed: audio arriving clearly faster than real time (more than `session.realtime_ratio_max` times) voids the
step, and the reason is shown as offline rendering. Limits:

- The check waits until the step has run for at least `session.realtime_check_min_s` seconds, or until the step has
  collected all its audio. Audio received before that is not analysed, except for the room step's live clap checks
  (their results are voided together with the step).
- A render that is not faster than real time (for example the host's "real-time render", or a heavy project) cannot be
  told apart: the plug-in then receives the project's audio and measures that audio, not the microphone. Please do not
  render while running a test.

## 宿主兼容（DAW）

下表只写实测过的结果。**没测过的一律写“未测”**；测完后每行补上宿主版本、日期和证据，超过 6 个月没有重测的行降回“未验证”。
目前没有任何一行有完整的宿主实测记录。开发中用 pluginval（strictness 5）检查插件的基本行为，那是格式校验，不等于在宿主里能用。

| 宿主 | 系统与格式 | 状态 |
|---|---|---|
| REAPER | Windows，VST3 | 未测 |
| REAPER | macOS，VST3 与 AU | 未测 |
| Cubase | Windows 与 macOS，VST3 | 未测 |
| FL Studio | Windows 与 macOS，VST3 | 未测 |
| GarageBand、Logic | macOS，AU | 未测 |
| 其他宿主 | - | 未测 |

插件收到的是宿主送来的音频：宿主没开监听、轨道没 arm、或把轨道静音时，插件收不到信号，接通页会提示“可能”是这些原因。
插件读不到宿主的监听和 arm 状态，所以这类提示只能写“可能”。具体到每个宿主怎么设置，本项目不教，请看宿主自己的说明。

## Host compatibility (DAWs)

The table records results that were actually observed. **A host that has not been tested is listed as "Not tested".** When a
host is tested, its row gets the host version, the date and the evidence, and a row that has not been retested for 6 months
goes back to "Unverified". At the moment no row has a full host test record. During development the plug-in is checked with
pluginval (strictness 5); that validates the plug-in format and does not mean it works in a host.

| Host | OS and format | Status |
|---|---|---|
| REAPER | Windows, VST3 | Not tested |
| REAPER | macOS, VST3 and AU | Not tested |
| Cubase | Windows and macOS, VST3 | Not tested |
| FL Studio | Windows and macOS, VST3 | Not tested |
| GarageBand, Logic | macOS, AU | Not tested |
| Any other host | - | Not tested |

The plug-in only sees the audio the host sends it. If monitoring is off, the track is not armed or the track is muted, the plug-in
receives no signal, and the connect step says that is a possible reason. The plug-in cannot read the host's monitoring or arm state,
so it can only say "possibly". How to set that up in each host is not covered here; see the host's own manual.

## 没有信号和很安静分不开

话筒没接好、幻象电源没开、增益为零时，插件收到的是一路极低的噪声；房间很安静又没人说话时，信号看起来也一样。两种情况在信号上分不开，
所以“没有信号”的提示一律写“可能”，主按钮不会被锁住。

## No signal and a very quiet room look the same

A microphone that is not connected, a missing phantom power or a gain at zero gives the plug-in a very low noise; so does a very
quiet room with nobody speaking. The two cannot be told apart from the signal, so the no-signal hints always say "possibly" and the
main button is never locked.

## 判档的数值还没有用真实录音标定

`thresholds.json` 里每个阈值都标着状态：`pending`（规格起始值）或 `synthetic`（合成信号验证过），还没有一项在真实录音上标定过。
读数本身可以参考；档位（好、一般、差，干、偏混、很混）只是起始判断，不是认证，也不能代替耳朵。房间混响只测 500 Hz 以上。

## Thresholds are not yet calibrated on real recordings

Every threshold in `thresholds.json` carries a state: `pending` (a starting value) or `synthetic` (checked with synthetic signals).
None has been calibrated on real recordings yet. The numbers are useful as readings; the bands (good, fair, poor; dry, lively,
very reverberant) are starting judgements, not a certification, and they do not replace your ears. Room reverberation is
measured above 500 Hz only.

## 支持的平台和声道布局

v1 提供 Windows x64 的 VST3，以及 macOS arm64 的 VST3 与 Audio Unit。没有 Intel Mac、Windows ARM、Linux、AAX（Pro Tools）和独立运行版。
插件只接受输入输出相同的单声道或立体声布局，其他布局宿主会拒绝加载；这两种之外的情形未测。

## Supported platforms and channel layouts

v1 ships a VST3 for Windows x64, and a VST3 and an Audio Unit for macOS arm64. There is no Intel Mac, Windows ARM, Linux, AAX
(Pro Tools) or standalone build. The plug-in accepts only a mono or a stereo layout with the same input and output; a host is
refused any other layout, and anything outside those two layouts is untested.

## 没有签名和公证

macOS 的安装包和插件没有签名，也没有公证，下载后系统会拦截，需要手动解除隔离，步骤见 [USER_GUIDE.zh-CN.md](USER_GUIDE.zh-CN.md)。
Windows 的安装包同样没有签名，SmartScreen 可能提示“未知发布者”。

## Not signed or notarized

The macOS package and plug-ins are not signed or notarized, so macOS blocks them after a download and you have to clear the
quarantine flag yourself; the steps are in [USER_GUIDE.md](USER_GUIDE.md). The Windows installer is not signed either, and
SmartScreen may show "unknown publisher".

## Windows 上缺 WebView2 运行时

插件窗口用 Microsoft Edge WebView2 运行时绘制，新版 Windows 10 和 11 自带。缺它时插件显示一块说明面板，而不是正常窗口；
从微软安装 Evergreen 运行时即可。音频不受影响，照常直通。

## Missing WebView2 Runtime on Windows

The plug-in window is drawn with the Microsoft Edge WebView2 Runtime, which is part of current Windows 10 and 11. Without it the
plug-in shows an explanation panel instead of its usual window; install the Evergreen runtime from Microsoft. Audio is not
affected and still passes through.

## 用音箱监听可能啸叫

监听打开、又用音箱外放时，话筒会拾到音箱的声音，可能啸叫，也会影响测量。请戴耳机，不得已用音箱时把音量调低。这不是插件的缺陷，
但第一屏会提醒。

## Speaker monitoring can feed back

With monitoring on and speakers playing, the microphone picks the speakers up. That can cause feedback and it disturbs the
measurements. Wear headphones, or turn the speakers down. This is not a plug-in defect, and the first screen reminds you.
