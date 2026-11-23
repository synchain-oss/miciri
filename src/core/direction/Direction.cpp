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
    // frame_ms 长的帧每 hop_ms 一帧、互相重叠（帧长取整数个 hop）：帧不重叠时，分析起点挪几个样本，
    // 哪几帧算“有声”就跟着变，电平与亮度随之跳动
    const auto hop = static_cast<std::size_t>(std::llround(thr::direction::hop_ms * 1e-3 * sampleRate));
    const auto parts = static_cast<std::size_t>(
        std::max<long long>(1, std::llround(thr::direction::frame_ms / thr::direction::hop_ms)));
    if (hop == 0 || samples.size() < hop * parts)
        return out;
    const int order = static_cast<int>(thr::clap::filter_order);
    dsp::BiquadCascade k = dsp::makeKWeighting(sampleRate);
    dsp::BiquadCascade num = dsp::makeButterworthBandpass(order, thr::direction::brightness_num_band_hz[0],
                                                          thr::direction::brightness_num_band_hz[1], sampleRate);
    dsp::BiquadCascade den = dsp::makeButterworthBandpass(order, thr::direction::brightness_den_band_hz[0],
                                                          thr::direction::brightness_den_band_hz[1], sampleRate);
    // 每 hop 个样本的平方和边滤波边累加，不存整段滤波输出；不够一个 hop 的尾巴用不到
    const std::size_t hops = samples.size() / hop;
    std::vector<double> kHop(hops), numHop(hops), denHop(hops);
    for (std::size_t b = 0; b < hops; ++b)
    {
        double sk = 0.0, sn = 0.0, sd = 0.0;
        for (std::size_t i = b * hop; i < (b + 1) * hop; ++i)
        {
            const double x = dsp::finiteOrZero(samples[i]);
            const double yk = k.process(x), yn = num.process(x), yd = den.process(x);
            sk += yk * yk;
            sn += yn * yn;
            sd += yd * yd;
        }
        kHop[b] = sk;
        numHop[b] = sn;
        denHop[b] = sd;
    }
    const auto kFrames = dsp::overlappedMeans(kHop, hop, parts);
    const auto numFrames = dsp::overlappedMeans(numHop, hop, parts);
    const auto denFrames = dsp::overlappedMeans(denHop, hop, parts);
    std::vector<double> levels;
    for (double p : kFrames)
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
        numSum += numFrames[i];
        denSum += denFrames[i];
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
    for (std::size_t i = 0; i < 4; ++i)
    {
        m.levelDb[i] = takes[i].levelDb;
        m.brightnessDb[i] = takes[i].brightnessDb;
    }
    DirectionCase& kind = out.result.verdict.kind;
    // 当前方向不明（越界）时不猜：当作正面会给出确定的结论
    if (currentIndex < 0 || currentIndex > 3)
    {
        m.currentIndex = -1;
        kind = DirectionCase::Uncertain;
        out.result.confidence = retest(RetestReason::Ambiguous);
        return out;
    }
    const int current = currentIndex;
    m.currentIndex = current;
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
