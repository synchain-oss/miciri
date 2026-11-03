// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cmath>

#include "config/Thresholds.h"
#include "level/Dsp.h"
#include "level/Level.h"

namespace miciri::core
{
namespace
{
// 样本是否全部落在 16 bit 的量化格点上
bool onSixteenBitGrid(const std::vector<double>& samples)
{
    constexpr double scale = 32768.0;
    constexpr double gridTolerance = 1e-6;
    return std::all_of(samples.begin(), samples.end(), [&](double x) {
        const double scaled = x * scale;
        return std::fabs(scaled - std::round(scaled)) < gridTolerance;
    });
}

// 没有外部底噪读数时，用非零 100 ms 帧电平的 P10 估
double estimateFloorDbfs(const std::vector<double>& samples, double sampleRate)
{
    dsp::FrameMeanSquare meter(static_cast<std::size_t>(std::llround(thr::no_signal::frame_ms * 1e-3 * sampleRate)));
    for (double x : samples)
        meter.add(dsp::finiteOrZero(x));
    std::vector<double> levels;
    for (double p : meter.frames())
        if (p > 0.0)
            levels.push_back(dsp::powerToDb(p));
    return levels.empty() ? dsp::powerToDb(0.0) : dsp::percentile(levels, thr::no_signal::frame_percentile);
}
} // namespace

DeviceProcessingResult analyseDeviceProcessing(const std::vector<double>& samples, double sampleRate,
                                               const DeviceProcessingContext& ctx)
{
    DeviceProcessingResult result;
    const std::size_t n = samples.size();
    // 先乘后除：44.1 kHz 上 50 ms 恰为 2205 个样本，先乘 1e-3 会因舍入变成 2206
    const auto minRun =
        static_cast<std::size_t>(std::ceil(thr::device_processing::zero_run_ms * sampleRate / 1000.0 - 1e-9));
    const auto firstNonZero = std::find_if(samples.begin(), samples.end(), [](double x) { return x != 0.0; });
    if (firstNonZero == samples.end() || minRun == 0)
        return result; // 整段全零走无信号诊断，不走这里

    std::size_t begin = 0;
    std::size_t end = n;
    if (thr::device_processing::interior_only)
    {
        begin = static_cast<std::size_t>(firstNonZero - samples.begin());
        end = n -
              static_cast<std::size_t>(
                  std::find_if(samples.rbegin(), samples.rend(), [](double x) { return x != 0.0; }) - samples.rbegin());
    }

    std::vector<EvidenceMark> marks;
    std::size_t longest = 0;
    for (std::size_t i = begin; i < end;)
    {
        if (samples[i] != 0.0)
        {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < end && samples[j] == 0.0)
            ++j;
        if (j - i >= minRun)
        {
            ++result.measures.zeroRuns;
            longest = std::max(longest, j - i);
            marks.push_back({EvidenceKind::ZeroRun, static_cast<double>(i) / sampleRate,
                             static_cast<double>(j) / sampleRate, static_cast<double>(j - i) / sampleRate * 1e3, -1});
        }
        i = j;
    }
    result.measures.longestZeroMs = static_cast<double>(longest) / sampleRate * 1e3;

    if (result.measures.zeroRuns == 0)
        return result;
    // 16 bit 且底噪低到会被量化成零：零段是量化的结果，不是设备处理
    const double floorDbfs = ctx.noiseFloorDbfs ? *ctx.noiseFloorDbfs : estimateFloorDbfs(samples, sampleRate);
    if (floorDbfs < thr::device_processing::false_alarm_floor_16bit_dbfs && onSixteenBitGrid(samples))
        return result;
    result.verdict.detected = true;
    result.evidence.marks = std::move(marks);
    return result;
}
} // namespace miciri::core
