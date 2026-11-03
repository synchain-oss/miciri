// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cmath>

#include "config/Thresholds.h"
#include "level/Dsp.h"
#include "level/Level.h"

namespace miciri::core
{
NoSignalResult analyseNoSignal(const std::vector<double>& input, double sampleRate, const NoSignalContext& ctx)
{
    NoSignalResult result;
    const std::size_t n = input.size();
    const auto frameLength = static_cast<std::size_t>(std::llround(thr::no_signal::frame_ms * 1e-3 * sampleRate));
    if (frameLength == 0 || n < frameLength)
        return result; // Waiting

    std::vector<double> samples(n);
    for (std::size_t i = 0; i < n; ++i)
        samples[i] = dsp::finiteOrZero(input[i]);

    // 末尾连续严格零的长度；整段相同的非零常数（直流）和全零同样处理
    std::size_t trailingZeros = 0;
    while (trailingZeros < n && samples[n - 1 - trailingZeros] == 0.0)
        ++trailingZeros;
    const bool constant = std::all_of(samples.begin(), samples.end(), [&](double x) { return x == samples[0]; });
    result.measures.zeroSeconds = static_cast<double>(trailingZeros) / sampleRate;

    // 只看非零帧的电平：整帧严格零的不算
    dsp::FrameMeanSquare meter(frameLength);
    for (double x : samples)
        meter.add(x);
    std::vector<double> levels;
    for (double p : meter.frames())
        if (p > 0.0)
            levels.push_back(dsp::powerToDb(p));
    if (levels.empty())
    {
        result.measures.maxFrameDbfs = dsp::powerToDb(0.0);
        result.measures.p10FrameDbfs = result.measures.maxFrameDbfs;
    }
    else
    {
        result.measures.maxFrameDbfs = *std::max_element(levels.begin(), levels.end());
        result.measures.p10FrameDbfs = dsp::percentile(levels, thr::no_signal::frame_percentile);
        result.measures.riseDb = result.measures.maxFrameDbfs - result.measures.p10FrameDbfs;
    }

    const double zeroWindow = thr::no_signal::strict_zero_window_s * sampleRate;
    if (static_cast<double>(n) >= zeroWindow && (trailingZeros == n || constant))
    {
        result.verdict.state = ctx.signalSeenBefore ? SignalState::AllZeroAfterSignal : SignalState::AllZeroFromStart;
    }
    else if (static_cast<double>(trailingZeros) >= zeroWindow)
    {
        result.verdict.state = SignalState::AllZeroAfterSignal; // 窗内更早的部分有信号
    }
    else if (levels.empty())
    {
        result.verdict.state = SignalState::Waiting;
    }
    else
    {
        const bool lowAndFlat =
            result.measures.maxFrameDbfs < thr::no_signal::abs_dbfs && result.measures.riseDb < thr::no_signal::rise_db;
        const bool windowComplete = static_cast<double>(n) >= thr::no_signal::low_flat_window_s * sampleRate;
        if (!lowAndFlat)
            result.verdict.state = SignalState::Ok;
        else
            result.verdict.state = windowComplete ? SignalState::VeryLowNoise : SignalState::Waiting;
    }
    return result;
}
} // namespace miciri::core
