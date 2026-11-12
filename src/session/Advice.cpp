// SPDX-License-Identifier: GPL-3.0-or-later
#include "session/Advice.h"

#include <algorithm>

#include "config/Thresholds.h"
#include "session/Grading.h"

namespace miciri::session
{
using core::FixItem;

namespace
{
template<class R>
bool current(const std::optional<R>& r, const SessionView& v, Section s)
{
    return r && !v.stale[static_cast<std::size_t>(s)];
}

// 置信度：可信为 1；再测为 0，不进候选
template<class R>
double confidence(const std::optional<R>& r)
{
    return r && r->confidence.level == core::Reliability::Trusted ? 1.0 : 0.0;
}

double score(FixItem item)
{
    namespace p = thr::priority;
    switch (item)
    {
    case FixItem::Direction:
        return p::direction_impact / p::direction_cost;
    case FixItem::Room:
        return p::room_impact / p::room_cost;
    case FixItem::Distance:
        return p::distance_impact / p::distance_cost;
    case FixItem::Hum:
        return p::hum_impact / p::hum_cost;
    case FixItem::Gain:
        return p::gain_impact / p::gain_cost;
    case FixItem::Noise:
        return p::noise_impact / p::noise_cost;
    case FixItem::Plosive:
        return p::plosive_impact / p::plosive_cost;
    default:
        return 0.0; // 固定在前的两项不打分
    }
}

bool signalProblem(core::SignalState s)
{
    return s != core::SignalState::Waiting && s != core::SignalState::Ok;
}
} // namespace

Advice decideAdvice(const SessionView& v, core::MicType mic)
{
    const core::AllResults& r = v.results;
    Advice a;

    // 固定在前
    if (current(r.noSignal, v, Section::NoSignal) && signalProblem(r.noSignal->verdict.state))
        a.fixes.push_back(FixItem::NoSignal);
    if (current(r.gain, v, Section::Gain) && r.gain->verdict.digitalClip)
        a.fixes.push_back(FixItem::DigitalClip);

    // 打分的候选：(项, 置信度)
    std::vector<std::pair<FixItem, double>> scored;
    const bool roomCurrent = current(r.reverb, v, Section::Reverb) && current(r.directRatio, v, Section::DirectRatio);
    const core::RoomDecision room = roomCurrent ? core::decideRoom(r.reverb, r.directRatio) : core::RoomDecision{};
    a.room = room.advice;
    if (current(r.direction, v, Section::Direction) && v.direction && core::directionIsPriority(*v.direction))
        scored.emplace_back(FixItem::Direction, confidence(r.direction));
    if (room.priority)
        scored.emplace_back(FixItem::Room, std::min(confidence(r.reverb), confidence(r.directRatio)));
    if (current(r.distance, v, Section::Distance) && confidence(r.distance) > 0.0)
    {
        if (const auto d = decideDistanceOf(r))
        {
            a.distance = d->advice;
            const auto kind = r.distance->verdict.kind;
            if (kind == core::DistanceCase::TooClose || kind == core::DistanceCase::TooFar)
                scored.emplace_back(FixItem::Distance, confidence(r.distance));
        }
    }
    if (current(r.hum, v, Section::Hum) && r.hum->verdict.kind != core::HumKind::None && !r.hum->verdict.slight)
        scored.emplace_back(FixItem::Hum, confidence(r.hum));
    // 有数字削波时它已固定在前，增益不再单列，同一个原因不说两次
    if (current(r.gain, v, Section::Gain) && r.gain->verdict.band != core::GainBand::Good &&
        !r.gain->verdict.digitalClip)
        scored.emplace_back(FixItem::Gain, confidence(r.gain));
    if (current(r.noise, v, Section::Noise) && r.noise->verdict.band == core::NoiseBand::Bad)
        scored.emplace_back(FixItem::Noise, confidence(r.noise));
    if (kSeverityGraded.plosive && current(r.plosive, v, Section::Plosive) &&
        r.plosive->verdict.severity != core::Severity::Light)
        scored.emplace_back(FixItem::Plosive, confidence(r.plosive));

    scored.erase(std::remove_if(scored.begin(), scored.end(), [](const auto& c) { return c.second <= 0.0; }),
                 scored.end());
    // 同分时按枚举的先后，排序稳定
    std::stable_sort(scored.begin(), scored.end(), [](const auto& x, const auto& y) {
        return score(x.first) * x.second > score(y.first) * y.second;
    });
    for (const auto& c : scored)
        a.fixes.push_back(c.first);

    core::DirectionPromptInput in;
    in.mic = mic;
    in.directionTested = r.direction.has_value();
    in.directionStale = v.stale[static_cast<std::size_t>(Section::Direction)];
    in.room = a.room;
    // 和候选一样，只看当前、可信的结果
    const auto usable = [&v](const auto& res, Section s) { return current(res, v, s) && confidence(res) > 0.0; };
    in.distance = usable(r.distance, Section::Distance) ? r.distance->verdict.kind : core::DistanceCase::Unknown;
    in.gainLowOrNoiseBad = (usable(r.gain, Section::Gain) && (r.gain->verdict.band == core::GainBand::TooQuiet ||
                                                              r.gain->verdict.band == core::GainBand::Quiet)) ||
                           (usable(r.noise, Section::Noise) && r.noise->verdict.band == core::NoiseBand::Bad);
    a.directionPrompt = core::decideDirectionPrompt(in);
    return a;
}
} // namespace miciri::session
