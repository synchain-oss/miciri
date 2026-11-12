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
