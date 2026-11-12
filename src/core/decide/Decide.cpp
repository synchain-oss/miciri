// SPDX-License-Identifier: GPL-3.0-or-later
#include "decide/Decide.h"

#include <algorithm>
#include <initializer_list>

#include "config/Thresholds.h"

namespace miciri::core
{
namespace
{
bool has(EvidenceLevel e)
{
    return e == EvidenceLevel::Weak || e == EvidenceLevel::Strong;
}
} // namespace

RoomDecision decideRoom(const std::optional<ReverbResult>& reverb, const std::optional<DirectRatioResult>& directRatio)
{
    RoomDecision out;
    if (!reverb || !directRatio || reverb->confidence.level != Reliability::Trusted ||
        directRatio->confidence.level != Reliability::Trusted)
        return out;
    const ReverbBand t = reverb->verdict.band;
    const bool longDecay = t == ReverbBand::Boomy || t == ReverbBand::VeryBoomy;
    switch (directRatio->verdict.band)
    {
    case DrBand::Low:
        out.advice = longDecay ? RoomAdvice::ChangeRoomAndMoveCloser : RoomAdvice::MoveCloserCheckDirection;
        break;
    case DrBand::Mid:
        out.advice = longDecay ? RoomAdvice::MoveCloserThenTreat : RoomAdvice::FineNotPriority;
        break;
    case DrBand::High:
        out.advice = longDecay ? RoomAdvice::KeepCloseDontBackOff : RoomAdvice::Good;
        break;
    }
    out.priority = out.advice == RoomAdvice::ChangeRoomAndMoveCloser ||
                   out.advice == RoomAdvice::MoveCloserCheckDirection || out.advice == RoomAdvice::MoveCloserThenTreat;
    return out;
}

EvidenceLevel evidenceOf(const std::optional<Severity>& severity)
{
    if (!severity)
        return EvidenceLevel::NotMeasured;
    switch (*severity)
    {
    case Severity::Light:
        return EvidenceLevel::None;
    case Severity::Moderate:
        return EvidenceLevel::Weak;
    case Severity::Heavy:
        return EvidenceLevel::Strong;
    }
    return EvidenceLevel::NotMeasured;
}

DistanceDecision decideDistance(const DistanceEvidence& e)
{
    DistanceDecision out;
    DistanceMeasures& m = out.result.measures;
    DistanceCase& kind = out.result.verdict.kind;
    const std::initializer_list<EvidenceLevel> close{e.plosive, e.sibilance, e.click};
    m.closeEvidence = static_cast<int>(std::count_if(close.begin(), close.end(), has));
    m.farEvidence = (has(e.lowDirectRatio) ? 1 : 0) + (has(e.lowSnr) ? 1 : 0);
    const bool closeStrong =
        std::any_of(close.begin(), close.end(), [](EvidenceLevel l) { return l == EvidenceLevel::Strong; });
    const bool far = has(e.lowDirectRatio);
    // SNR 低不单独成立，但能把弱的 D/R 证据抬成可信
    const bool farStrong = e.lowDirectRatio == EvidenceLevel::Strong || (far && has(e.lowSnr));
    const bool measured =
        std::any_of(close.begin(), close.end(), [](EvidenceLevel l) { return l != EvidenceLevel::NotMeasured; }) ||
        e.lowDirectRatio != EvidenceLevel::NotMeasured;

    if (!measured)
        kind = DistanceCase::Unknown;
    else if (m.closeEvidence > 0 && far)
    {
        kind = DistanceCase::Conflicting;
        out.advice = DistanceAdvice::FixRoomOrDirectionFirst;
    }
    else if (m.closeEvidence > 0)
    {
        kind = DistanceCase::TooClose;
        const bool clickPersists = e.click == EvidenceLevel::Strong && e.clickStillHeavyAfterBackingOff;
        out.advice = clickPersists ? DistanceAdvice::DrinkWater : DistanceAdvice::BackOffALittle;
        if (!closeStrong)
            out.result.confidence = retest(RetestReason::Ambiguous);
    }
    else if (far)
    {
        kind = DistanceCase::TooFar;
        out.advice = DistanceAdvice::MoveCloser;
        if (!farStrong)
            out.result.confidence = retest(RetestReason::Ambiguous);
    }
    else
        kind = DistanceCase::Fine;
    return out;
}

bool directionIsPriority(const DirectionAnalysis& direction)
{
    return direction.result.verdict.kind == DirectionCase::MaybeReversed &&
           direction.confirmedAdvantageDb >= thr::direction::priority_db;
}

DirectionPrompt decideDirectionPrompt(const DirectionPromptInput& in)
{
    if (in.mic == MicType::Headset)
        return DirectionPrompt::None;
    if (in.directionTested)
        return in.directionStale ? DirectionPrompt::Stale : DirectionPrompt::None;
    if (in.room == RoomAdvice::MoveCloserCheckDirection)
        return DirectionPrompt::MainButton;
    if (in.distance == DistanceCase::Conflicting)
        return DirectionPrompt::ActionWithButton;
    // 这一行对增益偏低和底噪差都只看 USB、电容与不确定：动圈本来就要较大增益，底噪也随之抬高
    if (in.gainLowOrNoiseBad && in.mic != MicType::Dynamic)
        return DirectionPrompt::ActionWithButton;
    return DirectionPrompt::CardButton;
}
} // namespace miciri::core
