// SPDX-License-Identifier: GPL-3.0-or-later
//
// 由各项结果合成的判断：房间与位置的决策表、距离标尺、方向测试的提示方式。纯函数，不读样本。
// 这里的建议还没有线上名字：接进桥快照时在 Enums.h 末尾追加，再换掉这里的枚举。
#pragma once

#include <cstdint>
#include <optional>

#include "direction/Direction.h"
#include "result/Results.h"

namespace miciri::core
{
// ---- 决策表：T20（长 = 偏混或很混；干和正常都算正常）乘 D/R（低、中、高）----
enum class RoomAdvice : std::uint8_t
{
    Unknown, // T20 或 D/R 缺，或要再测
    Good, // 正常 + 高
    FineNotPriority, // 正常 + 中：可以录，不进“最该改”
    MoveCloserCheckDirection, // 正常 + 低：离太远或方向错，先靠近、查方向
    ChangeRoomAndMoveCloser, // 长 + 低：先换地方或加吸声，同时靠近
    KeepCloseDontBackOff, // 长 + 高：近距离录音压住了房间，不必换，但别退后
    MoveCloserThenTreat // 长 + 中：房间声开始明显，先靠近一点再测，还不行再加吸声
};
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
enum class DistanceAdvice : std::uint8_t
{
    None,
    BackOffALittle,
    DrinkWater, // 退后后口水音仍重：口干也会加重
    MoveCloser,
    FixRoomOrDirectionFirst // 两边都有证据：矛盾
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

enum class DirectionPrompt : std::uint8_t
{
    None, // 耳机麦，或测过且没过期
    Stale, // 挪过位置且测过：卡片标“已过期”，可重测
    MainButton, // T20 正常 + D/R 低
    ActionWithButton, // 动作里一条，加次要按钮
    CardButton // 其余：方向卡上一个小按钮
};
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
