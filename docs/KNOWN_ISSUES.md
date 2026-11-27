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

实测过的部分：在 REAPER 7.82（Windows）上，这个偏好关着时，全速离线、1x 离线和在线渲染对插件都报“实时”；开着时，离线渲染报“非实时”。
数据见 [DAW_COMPATIBILITY.md](DAW_COMPATIBILITY.md)。没有手测过的是上面“偏好关时靠音频快于实时作废”这一支：它只有单元测试，
还没有在宿主里实际试过。

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

What has been measured: in REAPER 7.82 on Windows, with that preference off, a full-speed offline render, a 1x offline render and an
online render are all reported to the plug-in as real time; with it on, an offline render is reported as non-real-time. The numbers
are in [DAW_COMPATIBILITY.md](DAW_COMPATIBILITY.md). What has not been tried by hand is the branch above for the preference being
off, where audio arriving faster than real time voids the step: it is covered by unit tests only and has not been run in a host.

## 宿主兼容（DAW）

下表是摘要，只写实测过的结果。**没测过的一律写“未测”**；测完后每行补上宿主版本、日期和证据，超过 6 个月没有重测的行降回“未验证”。
目前只有 REAPER（Windows，VST3）有自动化测试记录；Cubase 15（Windows）由维护者手动试过：先发现了界面启动的问题，修复后复测通过，测试流程能走完；
其余都没有做过手测。每一行的版本、日期、证据和没有测到的部分，
见 [DAW_COMPATIBILITY.md](DAW_COMPATIBILITY.md)。开发中用 pluginval（strictness 5）检查插件的基本行为，那是格式校验，不等于在宿主里能用。

| 宿主 | 系统与格式 | 状态 |
|---|---|---|
| REAPER | Windows，VST3 | 音频与界面启动都已进入自动化测试（REAPER 7.82，2026-11-26）：界面开窗保持 10 秒、页面完成启动，有合成输入时电平表与削波灯的截图也看过；手测未做 |
| REAPER | macOS，VST3 与 AU | 未测 |
| Cubase | Windows，VST3 | 界面启动的缺陷（所有宿主共有的打包缺陷）已修复，界面已复测通过：测试流程能走完，各项都出了读数；走带、arm、监听各状态与离线导出未测（Cubase 15，单声道轨，复测日期 2026-11-24） |
| Cubase | macOS，VST3 | 未测 |
| FL Studio | Windows 与 macOS，VST3 | 未测 |
| GarageBand、Logic | macOS，AU | 未测 |
| 其他宿主 | - | 未测 |

插件收到的是宿主送来的音频：宿主没开监听、轨道没 arm、或把轨道静音时，插件收不到信号，接通页会提示“可能”是这些原因。
插件读不到宿主的监听和 arm 状态，所以这类提示只能写“可能”。具体到每个宿主怎么设置，本项目不教，请看宿主自己的说明。

## Host compatibility (DAWs)

The table is a summary and records results that were actually observed. **A host that has not been tested is listed as "Not
tested".** When a host is tested, its row gets the host version, the date and the evidence, and a row that has not been retested
for 6 months goes back to "Unverified". At the moment only REAPER on Windows (VST3) has automated test results; Cubase 15 on
Windows was tried by hand by a maintainer, which first found a UI start-up problem and, after the fix, passed the re-test (the UI
starts and the test flow runs through); nothing else has been tested by hand. The version, date, evidence and the parts that were
not covered for each row are in
[DAW_COMPATIBILITY.md](DAW_COMPATIBILITY.md). During development the plug-in is checked with pluginval (strictness 5); that
validates the plug-in format and does not mean it works in a host.

| Host | OS and format | Status |
|---|---|---|
| REAPER | Windows, VST3 | Both audio and UI start-up are in the automated tests (REAPER 7.82, 2026-11-26): the window is held open for 10 s and the page finishes starting; the meters and the clip lamp with a synthetic input were also checked in screenshots; manual test not done |
| REAPER | macOS, VST3 and AU | Not tested |
| Cubase | Windows, VST3 | The UI start-up defect (a packaging defect shared by all hosts) is fixed and the UI passed the re-test: the test flow runs through and every item gave a reading; transport, arm and monitoring states and offline export not tested (Cubase 15, mono track, re-test date 2026-11-24) |
| Cubase | macOS, VST3 | Not tested |
| FL Studio | Windows and macOS, VST3 | Not tested |
| GarageBand, Logic | macOS, AU | Not tested |
| Any other host | - | Not tested |

The plug-in only sees the audio the host sends it. If monitoring is off, the track is not armed or the track is muted, the plug-in
receives no signal, and the connect step says that is a possible reason. The plug-in cannot read the host's monitoring or arm state,
so it can only say "possibly". How to set that up in each host is not covered here; see the host's own manual.

## 还没有测过的

下面这些没有测过，不要当成“应该可以”：

- 宿主把挂着 Miciri 的轨静音时的自动旁路；
- 真实的硬件输入（自动化测试用的是 REAPER 自带的虚拟音频设备）；
- 运行中改采样率，以及 48 kHz、512 个样本以外的采样率和缓冲大小；
- macOS 上的任何宿主，包括 Audio Unit 格式；
- 关掉宿主的离线通知后，靠“收到的音频明显快于实时”作废离线渲染的那一支：只有单元测试，没有在宿主里试过；
- 所有宿主上的手测：走带、arm、监听的各种状态，反复开关编辑器，离线导出。

## Not tested yet

None of the following has been tested. Please do not read it as "should work":

- The automatic bypass a host applies when it mutes the track that holds Miciri.
- Real hardware input (the automated tests use REAPER's built-in virtual audio device).
- Changing the sample rate while running, and any sample rate or buffer size other than 48 kHz and 512 samples.
- Any host on macOS, including the Audio Unit format.
- The branch that voids an offline render when the host's offline notice is turned off and the audio arrives clearly faster than
  real time: it has unit tests only and has not been tried in a host.
- Manual tests in any host: every transport, arm and monitoring state, opening and closing the editor repeatedly, offline export.

## 没有单声道轨的宿主

REAPER 里即使把输出渲染成单声道，插件收到的主总线仍然是两路，所以“只有一路有声音”“一路明显更响”“两路是同一个声音”这几条提示里
“改成单声道轨道”的建议，在 REAPER 里做不到。这些提示仍然只说“可能”，也不影响测试。FL Studio 等宿主是否也没有单声道轨，未测。
[DAW_COMPATIBILITY.md](DAW_COMPATIBILITY.md) 里记着改写这类提示的建议文案，等手测确认后再采用。

## Hosts without mono tracks

In REAPER the plug-in's main bus is two channels even when the output is rendered to a mono file, so the advice "switch to a mono
track" in the notices "only one channel has sound", "one channel is much louder" and "both channels carry the same sound" cannot be
followed there. Those notices still only say "possibly", and they do not affect the tests. Whether FL Studio and similar hosts are
the same is untested. Suggested rewording for such notices is in [DAW_COMPATIBILITY.md](DAW_COMPATIBILITY.md); it will be adopted
once a manual test confirms it.

## 没有信号和很安静分不开

话筒没接好、幻象电源没开、增益为零时，插件收到的是一路极低的噪声；房间很安静又没人说话时，信号看起来也一样。两种情况在信号上分不开，
所以“没有信号”的提示一律写“可能”，主按钮不会被锁住。

## No signal and a very quiet room look the same

A microphone that is not connected, a missing phantom power or a gain at zero gives the plug-in a very low noise; so does a very
quiet room with nobody speaking. The two cannot be told apart from the signal, so the no-signal hints always say "possibly" and the
main button is never locked.

## 判档的数值大多还没有用真实录音标定

`thresholds.json` 里每个阈值都标着状态：`pending`（规格起始值）、`synthetic`（合成信号验证过）或 `calibrated`（在真实录音上核对过）。
按实录标定的只有少数几项（削波检查的电平下限、电流声与啸叫的几项判据），其余仍待标定；各状态各有多少项，见 [ALGORITHMS.md](ALGORITHMS.md)
里阈值表开头的计数（由 `thresholds.json` 生成）。
读数本身可以参考；档位（好、一般、差，干、偏混、很混）只是起始判断，不是认证，也不能代替耳朵。房间混响只测 500 Hz 以上。

现有的实录样本是 2026 年 11 月的一批录音：3 间房、2 位说话人、2 支话筒。说话人和话筒是一一绑定的，分不开“人的差别”和“话筒的差别”；
房间里没有很干的，也没有很大的空间。样本能说明什么、不能说明什么，见 [ALGORITHMS.md](ALGORITHMS.md)。

D/R、喷麦、齿音、口水音不分档，卡片无色，只给读数、参考值和重测后的“上次 → 现在”。参考值是经验值，不是档界，也不是合格线。

## Most thresholds are not yet calibrated on real recordings

Every threshold in `thresholds.json` carries a state: `pending` (a starting value), `synthetic` (checked with synthetic signals)
or `calibrated` (checked on real recordings). Only a few are calibrated so far (the level floor of the clipping check and some of
the hum and whistle checks); the rest are still waiting. How many thresholds are in each state is the count at the top of the
threshold table in [ALGORITHMS.md](ALGORITHMS.md), which is generated from `thresholds.json`. The numbers are useful as readings;
the bands (good, fair, poor; dry, lively, very reverberant) are starting judgements, not a certification, and they do not replace
your ears. Room reverberation is measured above 500 Hz only.

The real recordings so far are one batch from November 2026: 3 rooms, 2 speakers, 2 microphones. Each speaker used one microphone,
so the difference between people cannot be told from the difference between microphones; there is no very dry room and no very
large space. What the batch does and does not show is written in [ALGORITHMS.md](ALGORITHMS.md).

The direct-to-room ratio, plosives, sibilance and mouth noise are not graded: their cards have no colour and show the reading, a
reference value and, after a retest, "last time → now". The reference values are rules of thumb, not band limits and not a pass mark.

## 口水音的参考值只是粗提示

口水音的检测会把 t、k 的送气音误算进去，所以读数会偏高；有的说话人刻意咂嘴时，读数仍低于参考值，所以没有提示不等于没有口水音。
读数在参考值附近时，提示可能时有时无。把它当作“可能有”的粗提示，不要当作结论，也不要拿它和别人的读数比。

## The mouth-noise reference is only a rough hint

The mouth-noise detector also counts the aspiration of t and k, so its count reads high. Some speakers who smack their lips on
purpose still read below the reference value, so no hint does not mean no mouth noise. Near the reference value the hint can come
and go. Treat it as a rough "possibly", not as a conclusion, and do not compare your reading with other people's.

## 支持的平台和声道布局

v1 提供 Windows x64 的 VST3，以及 macOS arm64 的 VST3 与 Audio Unit。没有 Intel Mac、Windows ARM、Linux、AAX（Pro Tools）和独立运行版。
插件只接受输入输出相同的单声道或立体声布局，其他布局宿主会拒绝加载；这两种之外的情形未测。
macOS 的 VST3 与 Audio Unit 目前只在持续集成的 macOS 构建机上通过了 pluginval（strictness 5）和 `auval`，没有在真机的宿主里测过。

## Supported platforms and channel layouts

v1 ships a VST3 for Windows x64, and a VST3 and an Audio Unit for macOS arm64. There is no Intel Mac, Windows ARM, Linux, AAX
(Pro Tools) or standalone build. The plug-in accepts only a mono or a stereo layout with the same input and output; a host is
refused any other layout, and anything outside those two layouts is untested. The macOS VST3 and Audio Unit have so far only passed
pluginval (strictness 5) and `auval` on the continuous-integration macOS runner; they have not been tested in a host on a real Mac.

## 没有签名和公证

macOS 的安装包和插件没有签名，也没有公证，下载后系统会拦截，需要手动解除隔离，步骤见 [USER_GUIDE.zh-CN.md](USER_GUIDE.zh-CN.md)。
Windows 的安装包同样没有签名，SmartScreen 可能提示“未知发布者”。

## Not signed or notarized

The macOS package and plug-ins are not signed or notarized, so macOS blocks them after a download and you have to clear the
quarantine flag yourself; the steps are in [USER_GUIDE.md](USER_GUIDE.md). The Windows installer is not signed either, and
SmartScreen may show "unknown publisher".

## 插件窗口停在兜底面板

插件窗口起不来时，会换成一块说明面板（兜底面板），文字跟随界面语言，下面有一个“重试”按钮和几行诊断信息。音频不受影响，照常直通。
诊断信息里有一行 `script … > frame … > bridge … > ready …`，依次是页面脚本开始运行、外壳画出来、页面第一次调到插件、
页面拿到并应用了第一份数据的毫秒数，没走到的那一步写“-”；页面脚本出错时最后一行会写出错的位置。反馈问题时请把这几行原样发来。

2026-11-24 之前的构建（包括预览构建 `bd472d4`）有一个打包缺陷：页面调插件用的脚本缺了一个文件，所以在**任何宿主**里界面都起不来，
外壳出来以后主区一直空着，15 秒后切到这块面板。修复后的构建没有这个问题。

## The plug-in window stays on the fallback panel

When the plug-in window cannot start, it is replaced by an explanation panel (the fallback panel) in the UI language, with a Retry
button and a few lines of diagnostics. Audio is not affected and still passes through. One diagnostics line reads
`script … > frame … > bridge … > ready …`: the milliseconds at which the page script started, the shell was drawn, the page first
reached the plug-in, and the page received and applied its first data; a step that was never reached shows "-". When a page script
fails, the last line says where. When you report a problem, please send these lines as they are.

Builds before 2026-11-24 (including the preview build `bd472d4`) have a packaging defect: the script the page uses to call the
plug-in was missing a file, so the UI could not start in **any host**; after the shell appeared the main area stayed empty, and
after 15 s the window switched to this panel. Builds with the fix do not have this problem.

## Windows 上缺 WebView2 运行时

插件窗口用 Microsoft Edge WebView2 运行时绘制，新版 Windows 10 和 11 自带。缺它时插件显示一块说明面板，而不是正常窗口；
从微软安装 Evergreen 运行时即可。音频不受影响，照常直通。

## Missing WebView2 Runtime on Windows

The plug-in window is drawn with the Microsoft Edge WebView2 Runtime, which is part of current Windows 10 and 11. Without it the
plug-in shows an explanation panel instead of its usual window; install the Evergreen runtime from Microsoft. Audio is not
affected and still passes through.

## Windows 上同时开着几个宿主

所有 Miciri 窗口，不论在几个宿主进程里，共用同一个 WebView2 数据目录 `%LOCALAPPDATA%\Synchain\Miciri\WebView2`。
用两个测试进程同时开着试过：后开的窗口接入已经在跑的浏览器进程，没有冲突。REAPER 与 Cubase 这样的真实宿主的组合没有测过。
目录里只有网页缓存，没有录音和测量结果。限制：

- 窗口开不出来、说明面板写着“WebView2 环境没有启动”（英文界面下是 “The WebView2 environment did not start”）时，常见原因是这个目录不可写，或被别的程序用不同的设置占着。
  先确认目录可写；仍不行就关掉所有开着 Miciri 窗口的宿主，删掉这个目录再打开，Miciri 会重建它。
- 宿主以不同权限级别运行（一个以管理员身份、一个不是）时没有测过。若后开的窗口开不出来，会是上面那块说明面板；
  请先让两个宿主以同一权限级别运行再试。

## Several hosts open at once on Windows

Every Miciri window, in however many host processes, uses the same WebView2 data folder,
`%LOCALAPPDATA%\Synchain\Miciri\WebView2`. With two test processes open at the same time, the window opened later joined the
browser process that was already running, and they did not conflict. Real hosts such as REAPER and Cubase together have not been
tested. The folder holds only web page cache, no recordings and no measurement results. Limits:

- If the window does not open and you see the panel saying “The WebView2 environment did not start”, the usual causes are that the
  folder is not writable, or that another program holds it with different settings. Check that the folder is writable; if that
  is not it, close every host that has a Miciri window open, delete the folder, and open the plug-in again. Miciri recreates it.
- Hosts running at different privilege levels (one as administrator, one not) have not been tested. If the window opened later
  does not open, it will be the panel above; please run both hosts at the same privilege level and try again.

## 用音箱监听可能啸叫

监听打开、又用音箱外放时，话筒会拾到音箱的声音，可能啸叫，也会影响测量。请戴耳机，不得已用音箱时把音量调低。这不是插件的缺陷，
但第一屏会提醒。

## Speaker monitoring can feed back

With monitoring on and speakers playing, the microphone picks the speakers up. That can cause feedback and it disturbs the
measurements. Wear headphones, or turn the speakers down. This is not a plug-in defect, and the first screen reminds you.
