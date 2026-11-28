<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# 手动开始时，轻拍凑够就自己往下走，并逐下给判定（桥版本不变）

手动开始时，位置步的轻拍原来随这一步的“开始”启动，固定听 `session.soft_claps` × `session.soft_clap_round_s`（6 秒），
用户点完“开始”还在电脑前，来不及走回唱歌的位置。现在这一段不按遍计时：一直听，每一下尾巴收完就给判定，
凑够 `session.soft_claps` 下没削波、量得出的就自己结束，进爆破音字表（字表照旧等“开始”）。
自动推进（宿主测试的探针）不变：仍是 `session.soft_claps` 遍、每遍 `session.soft_clap_round_s` 秒，时间轴与分析次数和改动之前相同。
所有改动都是追加，`BRIDGE_API_VERSION` 仍是 2。

| 追加 | 内容 |
|---|---|
| `SNAPSHOT_LIVE` 末尾 | `taps`：手动开始时轻拍每一下的判定（`ENUM_TAP_STATE`，按先后，尾巴还没收完的那一下先不报）；轻拍阶段之外、自动推进与没有步骤在跑时是空数组。`tapsNeeded`：凑够几下有效就自己结束（`session.soft_claps`），一直有值 |
| 新枚举 `ENUM_TAP_STATE` | `valid` 没削波、量得出直达声，算进 D/R；`clipped` 削波了，不算；`unmeasured` 没削波但量不出（拍手前的噪声窗不够长，或直达能量不为正），不算 |
| `ENUM_FLOW_NOTICE` 的用法 | 手动开始时，轻拍到上限有效的不够 `direct_ratio.min_clean_claps` 下：削波把能用的几下刷掉了是 `clap-clipped`，否则是 `redo`；都退回这一段的待开始 |
| `thresholds.json` | 新增 `session.soft_clap_max_s`（20 秒，`pending`）；`soft_claps`、`soft_clap_round_s`、`live_clap_every_s`、`restarts_max` 的说明跟着改 |

轻拍收够 `direct_ratio.min_clean_claps` 下就可以点“结束”（`canEnd`），最低量不变。Result 的字段与分析算法都没有变。

## 上限为什么是 20 秒

工程起始值，没有实录依据，由这几段时间加起来：

- 点完“开始”走回唱歌的位置，几秒；
- 按自动推进的节奏（每 `soft_clap_round_s` 秒一下）拍够 `soft_claps` 下，6 秒；
- 每一下的判定要等尾巴收完（`direct_ratio.segment_after_s`，1.2 秒），再加点击保护和数拍手的间隔，约 1.5 秒后才出现；削波了重拍两三下还要几秒。

和房间步的上限 `session.room_max_s` 相同，两段“拍到够数为止”的部分上限一样。到上限时有 `direct_ratio.min_clean_claps` 下以上能用的，
照常往下走，D/R 用这几下；不够就退回待开始重拍。一步最多重来 `session.restarts_max` 次，之后照常往下走，由 D/R 给出再测。

## 削波的整段重拍改成当场反馈

原来轻拍收满之后，没削波的不够才整段重来，提示“拍轻一点”。现在每一下削波了，当场就在那一下的槽位上写“拍轻一点”，这一下不算数，
用户接着拍就行，凑够了自己往下走，所以正常情况下用不着收尾时的整段重来。`clap-clipped` 只在到上限还不够、而且是削波刷掉的时候出现，
判断与 D/R 的再测原因 `clipped` 相同；不是削波刷掉的（拍得太少，或太轻没被认成拍手）用 `redo`，界面写“时间到了，有效的轻拍还不够”。

## 凑够就自己结束的两段不再说“够了”

房间步在第 `reverb.repeat_min_kept` 下有效时就能点“结束”，第 `session.room_claps_needed` 下有效时自己结束，原来界面在能点时说
“够了，随时可以点结束”，下一下就自己结束了，让人以为要回来点。现在房间步和轻拍这两段进行中只写“有效 n / 总数”（总数是
`clapsNeeded` 与 `tapsNeeded`），“结束”能点时照样能点，不另说一句。其余按最低量结束的段（按秒、按字）没有自动结束，仍写“够了”。
这一条只改界面，契约字段的含义不变。
