// SPDX-License-Identifier: GPL-3.0-or-later
//
// 喷麦、齿音、口水音三项里，哪几项的 severity 是真的分了档。现在三项都不分档：
// 喷麦与齿音只给数值，供重测位置后的“上次 → 现在”（齿音还要同一增益），口水音只作“可能有口水音”的提示。
// severity 停在最低档只表示“未分档”，不表示良好，所以快照里不给它们颜色。
// 以后哪一项有了实录依据、core 开始给它分档，在同一个 PR 里把这里改成 true。
#pragma once

namespace miciri::session
{
struct SeverityGrading
{
    bool plosive = false;
    bool sibilance = false;
    bool click = false;
};
inline constexpr SeverityGrading kSeverityGraded{false, false, false};
} // namespace miciri::session
