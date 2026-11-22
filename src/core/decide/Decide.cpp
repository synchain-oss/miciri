// SPDX-License-Identifier: GPL-3.0-or-later
#include "decide/Decide.h"

#include <array>
#include <cmath>

#include "config/Thresholds.h"

namespace miciri::core
{
namespace
{
// 一项读数的“上次 → 现在”。只知道下限的一边真值只会更高：上次是下限时真实的变化只会更小，
// 现在是下限时只会更大，所以只报不受它影响的那个方向。delta 只在给出方向（含“没有明显变化”）时填
DistanceSign compare(const DistanceReading& before, const DistanceReading& now, double resolution, bool roomChanged,
                     bool gainChanged, double& delta)
{
    delta = 0.0;
    if (!before.available || !now.available)
        return DistanceSign::NotCompared;
    if (roomChanged)
        return DistanceSign::RoomChanged;
    if (gainChanged)
        return DistanceSign::GainChanged;
    const double d = now.value - before.value;
    DistanceSign sign = DistanceSign::NotCompared;
    if (d > resolution && !before.lowerBound)
        sign = DistanceSign::Closer;
    else if (d < -resolution && !now.lowerBound)
        sign = DistanceSign::Farther;
    else if (std::fabs(d) <= resolution && !before.lowerBound && !now.lowerBound)
        sign = DistanceSign::NoChange;
    if (sign != DistanceSign::NotCompared)
        delta = d;
    return sign;
}
} // namespace

RoomDecision decideRoom(const std::optional<ReverbResult>& reverb)
{
    RoomDecision out;
    if (!reverb || reverb->confidence.level != Reliability::Trusted)
        return out;
    switch (reverb->verdict.band)
    {
    case ReverbBand::Dry:
    case ReverbBand::Normal:
        out.advice = RoomAdvice::Good;
        break;
    case ReverbBand::Boomy:
        out.advice = RoomAdvice::MoveCloserThenTreat;
        out.priority = true;
        break;
    case ReverbBand::VeryBoomy:
        out.advice = RoomAdvice::ChangeRoomAndMoveCloser;
        out.priority = true;
        break;
    }
    return out;
}

double empiricalDistanceLimitM(double tMidSec)
{
    namespace d = thr::distance;
    if (!(tMidSec > 0.0))
        return 0.0;
    // 临界距离（直达声与房间声相等处）乘一个比例：扩散声场近似，房间体积与声源指向性取典型值
    return d::limit_critical_frac * d::critical_distance_coef *
           std::sqrt(d::source_directivity * d::room_volume_m3 / tMidSec);
}

DistanceResult decideDistance(const DistanceEvidence& e)
{
    namespace d = thr::distance;
    DistanceResult out;
    DistanceMeasures& m = out.measures;
    DistanceVerdict& v = out.verdict;
    m.empiricalLimitM = empiricalDistanceLimitM(e.tMidSec);
    v.drSign = compare(e.drBefore, e.drNow, d::dr_resolution_db, e.roomChanged, false, m.drDeltaDb);
    v.plosiveSign =
        compare(e.plosiveBefore, e.plosiveNow, d::plosive_resolution_db, e.roomChanged, false, m.plosiveDeltaDb);
    v.sibilanceSign = compare(e.sibilanceBefore, e.sibilanceNow, d::sibilance_resolution_db, e.roomChanged,
                              e.gainChanged, m.sibilanceDeltaDb);

    bool unchanged = false;
    for (const DistanceSign s : std::array{v.drSign, v.plosiveSign, v.sibilanceSign})
    {
        m.closeEvidence += s == DistanceSign::Closer ? 1 : 0;
        m.farEvidence += s == DistanceSign::Farther ? 1 : 0;
        unchanged = unchanged || s == DistanceSign::NoChange;
    }
    if (m.closeEvidence > 0 && m.farEvidence > 0)
        v.kind = DistanceCase::Conflicting;
    else if (m.closeEvidence > 0)
        v.kind = DistanceCase::Closer;
    else if (m.farEvidence > 0)
        v.kind = DistanceCase::Farther;
    else if (unchanged)
        v.kind = DistanceCase::NoChange;
    else if (e.snrBad && m.empiricalLimitM > 0.0)
        v.kind = DistanceCase::MaybeTooFar;
    return out;
}

DistanceAdvice distanceAdviceOf(const DistanceResult& distance)
{
    return distance.verdict.kind == DistanceCase::MaybeTooFar ? DistanceAdvice::MoveCloser : DistanceAdvice::None;
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
    // T20 不长、SNR 差：房间不是原因，离远了或方向错的可能更大
    if (in.room == RoomAdvice::Good && in.distance == DistanceCase::MaybeTooFar)
        return DirectionPrompt::MainButton;
    if (in.distance == DistanceCase::Conflicting)
        return DirectionPrompt::ActionWithButton;
    // 这一行对增益偏低和底噪差都只看 USB、电容与不确定：动圈本来就要较大增益，底噪也随之抬高
    if (in.gainLowOrNoiseBad && in.mic != MicType::Dynamic)
        return DirectionPrompt::ActionWithButton;
    return DirectionPrompt::CardButton;
}
PlosiveAdvice plosiveAdviceOf(const PlosiveResult& plosive)
{
    return plosive.verdict.overReference ? PlosiveAdvice::PopFilterOrBackOff : PlosiveAdvice::None;
}

SibilanceAdvice sibilanceAdviceOf(const SibilanceResult& sibilance)
{
    return sibilance.verdict.overReference ? SibilanceAdvice::AngleSoftenDeEss : SibilanceAdvice::None;
}

ClickAdvice clickAdviceOf(const ClickResult& now, const std::optional<ClickResult>& before)
{
    if (!now.verdict.overReference)
        return ClickAdvice::None;
    const bool stillMany = before && before->confidence.level == Reliability::Trusted && before->verdict.overReference;
    return stillMany ? ClickAdvice::DrinkWater : ClickAdvice::BackOffALittle;
}

std::optional<RecommendedDistance> recommendedDistanceOf(MicType mic)
{
    namespace r = thr::recommended_distance;
    switch (mic)
    {
    case MicType::Headset:
        return std::nullopt;
    case MicType::Dynamic:
        return RecommendedDistance{r::dynamic_range_cm[0], r::dynamic_range_cm[1], r::dynamic_range_cm[0],
                                   r::dynamic_range_cm[1]};
    case MicType::Unknown:
    case MicType::Condenser:
    case MicType::Usb:
        break;
    }
    return RecommendedDistance{r::condenser_around_cm, r::condenser_around_cm, r::condenser_range_cm[0],
                               r::condenser_range_cm[1]};
}
} // namespace miciri::core
