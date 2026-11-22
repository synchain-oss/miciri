<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# 口水音的参考值定为每分钟 60 次（桥版本不变）

依据：实录标定，3 间房、2 位说话人、2 支话筒（2026-11）。两位说话人在约 15 cm 处，每分钟的读数都低于 60；刻意咂嘴时高于 60。
检出里仍有送气音（t、k）的误报，所以这只是参考，不是档界。口水音照旧不分档，也不进“最该改”。

| 项 | 以前 | 现在 |
|---|---|---|
| `thresholds.json` 的 `click.reference_max_per_min` | `null`（`pending`，待定） | `60`（`pending`，经验值） |
| `ClickVerdict::overReference`（`Results.h`） | 恒为 `false` | `perMinute` 超过参考值时为 `true` |
| 快照 `bands.clickReferencePerMin` | `null`（界面不显示参考） | `60` |
| `advice.click` | 实际上恒为 `none` | 按 `RESULT.md` 的规则给出：第一次超过时 `back-off-a-little`；上一次位置步（可信）也超过、这次仍超过时 `drink-water` |

字段、名表与建议键都没有变，`BRIDGE_API_VERSION` 仍是 2。规则本身早已写在 `RESULT.md` 的“参考值与推荐距离”里，这次只是让它生效。

界面要做的事：`clickReferencePerMin` 不再是 `null`，口水音卡要显示参考值，超过时显示对应的提示。“参考值还没定时为 `null`、界面不显示参考”这条规则保留，留给以后新加的参考值。
