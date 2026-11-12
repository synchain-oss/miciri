// SPDX-License-Identifier: GPL-3.0-or-later
//
// 喷麦、齿音、口水音三项里，哪几项的 severity 是真的分了档。
// 齿音只给两段绝对电平供同一增益下的 A/B 比较，口水音只作“可能有口水音”的提示，这两项不分档：
// 它们的 severity 停在最低档只表示“未分档”，不表示良好。所以快照里不给它们颜色，
// 距离标尺也不把它们当成“没有太近的证据”，而是当成没测。
// 等实录标定给出档界、core 开始给这两项分档时，在同一个 PR 里把这里改成 true。
#pragma once

namespace miciri::session
{
struct SeverityGrading
{
    bool plosive = false;
    bool sibilance = false;
    bool click = false;
};
inline constexpr SeverityGrading kSeverityGraded{true, false, false};
} // namespace miciri::session
