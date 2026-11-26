<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# 测试的各段可以手动开始与结束（桥版本不变）

原来一步里的各段按时长连续自动推进：轻拍收满就念字表，字表念完要等满两遍的时长，四个方向一个接一个，阶段要重来时倒数后自动重开。
现在会话可以按“手动开始”分段：第一段随 `startStep` 开始，之后每一段等用户开始；收够这一段的最低量后用户可以提前结束，时长上限仍在。
自动推进保留给宿主测试的探针，它的时间轴和分析次数与改动之前相同。所有改动都是追加，`BRIDGE_API_VERSION` 仍是 2。

| 追加 | 内容 |
|---|---|
| `BRIDGE_FUNCTIONS` | `startPhase`、`endPhase`（都不带参数） |
| `BRIDGE_REASONS` | `no-phase`：没有在等开始或可以手动结束的一段；`not-enough`：这一段还没收够最低量 |
| `SNAPSHOT_LIVE` 末尾 | `waiting`、`canEnd`、`need`、`needLeft`、`heard` |
| 新枚举 `ENUM_PHASE_NEED` | `none`、`seconds`、`voice-seconds`、`claps`、`syllables` |
| `ENUM_FLOW_NOTICE` 的含义 | 手动开始时，非 `success` 的提示表示这一阶段已退回待开始，`noticeSec` 为 0，提示留到用户再开始；自动推进时照旧倒数 |

手动开始时，字表的几遍合成一段（`rounds` 为 1）。开始后先丢掉 `session.click_guard_s` 秒，用户结束时最后这么长的一段也不进分析，
免得把点击声当成口水音或拍手；证据波形的时间轴只算分析用到的样本。规则见 `docs/contracts/BRIDGE.md` 的“一段的开始与结束”。

各段的最低量与依据：

| 段 | 时长上限（不变） | 最低量 | 依据 |
|---|---|---|---|
| 增益 | `session.gain_s` | 有声 `session.gain_min_voice_s` 秒 | 说话电平按 50 ms 帧取第 95 百分位附近的活跃帧，有声帧用同一条规则 |
| 安静 | `session.quiet_s` | 由现有阈值推出，现在是 4 秒 | 电流声的 Welch 至少 `hum.min_segments` 段，底噪至少 `noise.min_frames` 帧；少于它们就是 too-short |
| 房间 | `session.room_max_s`；凑够或拍满照样自动收尾 | `reverb.repeat_min_kept` 下有效 | 500 Hz 与 1 kHz 都要各留下这么多下才出 T20 |
| 轻拍 | `session.soft_claps` × `session.soft_clap_round_s` | `direct_ratio.min_clean_claps` 下没削波、量得出的 | D/R 的下限 |
| 爆破音、齿音字表 | `session.word_list_rounds` × `session.word_list_round_s` | `session.word_list_min_events` 个音节 | 最短的一遍字表的字数，由各自的分析数出 |
| 自然念 | `click.analysis_window_s` | 有声 `session.natural_min_voice_s` 秒 | 口水音的次数按字表加这一段的总时长算，多留一点 |
| 方向每个方向、确认每一遍 | `session.direction_take_s` | 有声 `session.direction_min_voice_s` 秒 | 每个方向的电平取有声帧的中位数 |

`thresholds.json` 的 `session` 组新增 `gain_min_voice_s`、`word_list_min_events`、`natural_min_voice_s`、`direction_min_voice_s`、
`click_guard_s`、`live_words_every_s`（手动开始时字表多久数一次音节），全部 `pending`；`notice_s` 只在自动推进时用，说明已改。

Result 的字段与分析算法都没有变：提前结束只是给分析的音频短一些，数据不够时照原来的置信度规则判再测。
`cancelStep`、过期规则，以及离线渲染与验证宿主下不开始测试的保护都不变。

界面要做的事：按 `waiting` 显示这一段要做什么和“开始”，按 `canEnd`、`need`、`needLeft` 显示“结束”能不能按、还差什么。
这一版界面还没接，插件里的会话仍是自动推进，下一版界面接上后再切到手动开始。
