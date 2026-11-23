<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# 喷麦与齿音的元音参考下限改为按毫秒的键（桥版本不变）

喷麦的包络帧距从 2.5 ms 改为 0.5 ms，齿音的谱帧距从 5 ms 改为 1.25 ms，读数才不随分析起点变化。两个“元音参考至少要几帧”的阈值原来按帧数写，
帧距一改就得跟着换算，所以改成按毫秒的键，值取原来的时长，代码按实际帧距换成帧数。在现在的帧距下帧数与改名前相同，读数不变。

| 改名前 | 改名后 | 值 |
|---|---|---|
| `plosive.vowel_min_frames`（6 帧，2.5 ms 帧距） | `plosive.vowel_kept_min_ms` | 15 ms（0.5 ms 帧距下 30 帧） |
| `sibilance.vowel_min_frames`（4 帧，5 ms 帧距） | `sibilance.vowel_kept_min_ms` | 20 ms（1.25 ms 帧距下 16 帧） |

同一个 PR 里另有几项只改值或新增、不改键名与分组：`plosive.envelope_hop_ms` 2.5 → 0.5、`sibilance.hop_ms` 5 → 1.25，
新增 `direction.hop_ms`（5 ms，方向的帧互相重叠）。

Result、快照字段、名表与建议键都没有变，`BRIDGE_API_VERSION` 仍是 2。界面要做的事：没有。
