// SPDX-License-Identifier: GPL-3.0-or-later
//
// “最该改”与各项建议：由一份 SessionView 合成，纯函数，不读样本。
// 无信号与数字削波固定在前；其余按 影响 × 置信度 ÷ 修改代价 排序（表在 thresholds.json 的 priority 分组），
// 要再测的（置信度为零）不进候选，过期的结果也不进（它说的是改动之前的情况）。
// 固定在前的两项只看过期、不看置信度：削波样本与接通状态是直接数出来的，要再测时它们也已经发生。
#pragma once

#include <vector>

#include "session/Session.h"

namespace miciri::session
{
struct Advice
{
    std::vector<core::FixItem> fixes; // 第一项就是“最该改”；空表示没有要改的
    core::RoomAdvice room = core::RoomAdvice::Unknown;
    core::DistanceAdvice distance = core::DistanceAdvice::None;
    core::DirectionPrompt directionPrompt = core::DirectionPrompt::None;
};

Advice decideAdvice(const SessionView& view, core::MicType mic);
} // namespace miciri::session
