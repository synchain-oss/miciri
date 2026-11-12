// SPDX-License-Identifier: GPL-3.0-or-later
//
// 由各项结果合成的判断：房间与位置的决策表、距离标尺、方向测试的提示方式。纯函数，不读样本。
// 建议的枚举（RoomAdvice、DistanceAdvice、DirectionPrompt）进了桥名表，定义在 result/Enums.h 末尾。
#pragma once

#include <cstdint>
#include <optional>

#include "direction/Direction.h"
#include "result/Results.h"

namespace miciri::core
{
// ---- 决策表：T20（长 = 偏混或很混；干和正常都算正常）乘 D/R（低、中、高）----
struct RoomDecision
{
    RoomAdvice advice = RoomAdvice::Unknown;
    bool priority = false; // 进“最该改”的候选
};
RoomDecision decideRoom(const std::optional<ReverbResult>& reverb, const std::optional<DirectRatioResult>& directRatio);

// ---- 距离标尺：不显示厘米，只看两边的证据 ----
enum class EvidenceLevel : std::uint8_t
{
    NotMeasured,
    None,
    Weak,
    Strong
};
// 喷麦、齿音、口水音的轻重换成证据：轻是没有，中是弱，重是强；没测是 NotMeasured
EvidenceLevel evidenceOf(const std::optional<Severity>& severity);

struct DistanceEvidence
{
    // 太近的证据
    EvidenceLevel plosive = EvidenceLevel::NotMeasured;
    EvidenceLevel sibilance = EvidenceLevel::NotMeasured;
    EvidenceLevel click = EvidenceLevel::NotMeasured;
    // 太远的证据：D/R 低是主证据；SNR 低只作次要，单独出现不算太远
    EvidenceLevel lowDirectRatio = EvidenceLevel::NotMeasured;
    EvidenceLevel lowSnr = EvidenceLevel::NotMeasured;
    // 上一次口水音重、用户已经退后，这次仍然重
    bool clickStillHeavyAfterBackingOff = false;
};
struct DistanceDecision
{
    DistanceResult result; // 只有弱证据时置信度为再测（不进“最该改”的候选）
    DistanceAdvice advice = DistanceAdvice::None;
};
DistanceDecision decideDistance(const DistanceEvidence& evidence);

// ---- 方向 ----
// 可能放反，且两次测量里较小的优势也不低于 priority_db，才进“最该改”
bool directionIsPriority(const DirectionAnalysis& direction);

struct DirectionPromptInput
{
    MicType mic = MicType::Unknown;
    bool directionTested = false;
    bool directionStale = false; // 测过之后挪过位置
    RoomAdvice room = RoomAdvice::Unknown;
    DistanceCase distance = DistanceCase::Unknown;
    bool gainLowOrNoiseBad = false;
};
// 没测过方向不算问题：这里只决定提示放在哪，不影响“可以开始录音了”
DirectionPrompt decideDirectionPrompt(const DirectionPromptInput& in);
} // namespace miciri::core
