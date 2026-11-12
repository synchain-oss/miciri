// SPDX-License-Identifier: GPL-3.0-or-later
#include "direction/Direction.h"

#include <algorithm>
#include <cmath>

#include "config/Thresholds.h"
#include "level/KWeighting.h"
#include "room/Butterworth.h"

namespace miciri::core
{
DirectionTake measureDirectionTake(const std::vector<double>& samples, double sampleRate)
{
    DirectionTake out;
    const auto frame = static_cast<std::size_t>(std::llround(thr::direction::frame_ms * 1e-3 * sampleRate));
    if (frame == 0 || samples.size() < frame)
        return out;
    const int order = static_cast<int>(thr::clap::filter_order);
    dsp::BiquadCascade k = dsp::makeKWeighting(sampleRate);
    dsp::BiquadCascade num = dsp::makeButterworthBandpass(order, thr::direction::brightness_num_band_hz[0],
                                                          thr::direction::brightness_num_band_hz[1], sampleRate);
    dsp::BiquadCascade den = dsp::makeButterworthBandpass(order, thr::direction::brightness_den_band_hz[0],
                                                          thr::direction::brightness_den_band_hz[1], sampleRate);
    dsp::FrameMeanSquare kFrames(frame), numFrames(frame), denFrames(frame);
    for (double v : samples)
    {
        const double x = dsp::finiteOrZero(v);
        kFrames.add(k.process(x));
        numFrames.add(num.process(x));
        denFrames.add(den.process(x));
    }
    std::vector<double> levels;
    for (double p : kFrames.frames())
        levels.push_back(dsp::powerToDb(p));
    const double top = dsp::percentile(levels, thr::direction::active_top_percentile);
    if (top <= dsp::powerToDb(dsp::kPowerFloor))
        return out; // 数字静音：没有可比的电平
    const double gate = top - thr::direction::active_within_top_db;
    std::vector<double> active;
    double numSum = 0.0, denSum = 0.0;
    for (std::size_t i = 0; i < levels.size(); ++i)
    {
        if (levels[i] < gate)
            continue;
        active.push_back(levels[i]);
        numSum += numFrames.frames()[i];
        denSum += denFrames.frames()[i];
    }
    out.active = !active.empty();
    if (out.active)
    {
        out.levelDb = dsp::median(active);
        out.brightnessDb = dsp::powerToDb(numSum) - dsp::powerToDb(denSum);
    }
    return out;
}

DirectionAnalysis decideDirection(const std::array<DirectionTake, 4>& takes, int currentIndex,
                                  const std::optional<DirectionRepeat>& repeat)
{
    DirectionAnalysis out;
    DirectionMeasures& m = out.result.measures;
    const int current = std::clamp(currentIndex, 0, 3);
    m.currentIndex = current;
    for (std::size_t i = 0; i < 4; ++i)
    {
        m.levelDb[i] = takes[i].levelDb;
        m.brightnessDb[i] = takes[i].brightnessDb;
    }
    DirectionCase& kind = out.result.verdict.kind;
    if (std::any_of(takes.begin(), takes.end(), [](const DirectionTake& t) { return !t.active; }))
    {
        kind = DirectionCase::Uncertain;
        out.result.confidence = retest(RetestReason::TooShort);
        return out;
    }

    int best = -1;
    for (int i = 0; i < 4; ++i)
        if (i != current &&
            (best < 0 || m.levelDb[static_cast<std::size_t>(i)] > m.levelDb[static_cast<std::size_t>(best)]))
            best = i;
    out.repeatIndex = best;
    m.bestOtherAdvantageDb = m.levelDb[static_cast<std::size_t>(best)] - m.levelDb[static_cast<std::size_t>(current)];
    const auto [lo, hi] = std::minmax_element(m.levelDb.begin(), m.levelDb.end());

    if (*hi - *lo < thr::direction::small_difference_db)
        kind = DirectionCase::SmallDifference;
    else if (m.bestOtherAdvantageDb <= 0.0)
        kind = DirectionCase::Best;
    else if (m.bestOtherAdvantageDb < thr::direction::repeat_trigger_db)
        kind = DirectionCase::Uncertain;
    else if (!repeat)
    {
        kind = DirectionCase::Uncertain;
        out.needsRepeat = true;
    }
    else
    {
        const double again = repeat->otherLevelDb - repeat->currentLevelDb;
        const bool confirmed = again >= thr::direction::repeat_trigger_db;
        kind = confirmed ? DirectionCase::MaybeReversed : DirectionCase::Uncertain;
        out.confirmedAdvantageDb = confirmed ? std::min(again, m.bestOtherAdvantageDb) : 0.0;
    }
    return out;
}
} // namespace miciri::core
