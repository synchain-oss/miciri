// SPDX-License-Identifier: GPL-3.0-or-later
//
// 无信号诊断与设备处理：各个状态的最小例子，零段长度的边界，首尾零段与 16 bit 量化零的误报防护。
#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <map>

#include "core/level/Synth.h"
#include "level/Level.h"

using namespace miciri;
using namespace miciri::testing;

namespace
{
constexpr double kFs = 48000.0;

std::vector<double> zeros(double seconds, double fs = kFs)
{
    return std::vector<double>(static_cast<std::size_t>(seconds * fs), 0.0);
}

std::vector<double> concat(std::vector<double> a, const std::vector<double>& b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}
} // namespace

TEST_CASE("no-signal: strictly all zero")
{
    const core::NoSignalResult fresh = core::analyseNoSignal(zeros(1.5), kFs);
    CHECK(fresh.verdict.state == core::SignalState::AllZeroFromStart);
    CHECK(fresh.measures.zeroSeconds == within(1.5, 1e-9));

    core::NoSignalContext seen;
    seen.signalSeenBefore = true;
    CHECK(core::analyseNoSignal(zeros(1.5), kFs, seen).verdict.state == core::SignalState::AllZeroAfterSignal);

    // 不到 1 秒还不能下结论
    CHECK(core::analyseNoSignal(zeros(0.5), kFs).verdict.state == core::SignalState::Waiting);
    // 太短的窗口也一样
    CHECK(core::analyseNoSignal(zeros(0.05), kFs).verdict.state == core::SignalState::Waiting);
}

TEST_CASE("no-signal: signal that ends in strict zeros, and a constant offset")
{
    const std::vector<double> dropped = concat(voice(kFs, 3.0, 0.05, 1), zeros(2.0));
    const core::NoSignalResult r = core::analyseNoSignal(dropped, kFs);
    CHECK(r.verdict.state == core::SignalState::AllZeroAfterSignal);
    CHECK(r.measures.zeroSeconds >= 2.0);

    // 整段常数直流和全零同样处理
    CHECK(core::analyseNoSignal(std::vector<double>(static_cast<std::size_t>(2 * kFs), 1e-3), kFs).verdict.state ==
          core::SignalState::AllZeroFromStart);
}

TEST_CASE("no-signal: a quiet but alive input is not all-zero")
{
    // 每半秒一个 ±1 LSB（16 bit）：不是严格全零，也不会被当成有信号
    std::vector<double> sparse = zeros(6.0);
    for (std::size_t i = 1; i <= 11; ++i)
        sparse[i * static_cast<std::size_t>(kFs / 2)] = (i % 2 ? 1.0 : -1.0) / 32768.0;
    const core::NoSignalResult r = core::analyseNoSignal(sparse, kFs);
    CHECK(r.verdict.state == core::SignalState::VeryLowNoise);
    CHECK(r.measures.zeroSeconds < 1.0);

    // 底噪 -90 dBFS、没人说话：整窗 6 秒才下结论
    const std::vector<double> floorOnly = whiteNoise(static_cast<std::size_t>(6 * kFs), fromDb(-90.0), 1);
    CHECK(core::analyseNoSignal(floorOnly, kFs).verdict.state == core::SignalState::VeryLowNoise);
    const std::vector<double> shortWindow(floorOnly.begin(), floorOnly.begin() + static_cast<std::ptrdiff_t>(3 * kFs));
    CHECK(core::analyseNoSignal(shortWindow, kFs).verdict.state == core::SignalState::Waiting);
}

TEST_CASE("no-signal: speech over a floor is fine, at every rate")
{
    for (double fs : kRates)
    {
        std::vector<double> x = voice(fs, 6.0, fromDb(-30.0), 3, 1.0);
        mix(x, whiteNoise(x.size(), fromDb(-75.0), 4));
        const core::NoSignalResult r = core::analyseNoSignal(x, fs);
        INFO("fs " << fs);
        CHECK(r.verdict.state == core::SignalState::Ok);
        CHECK(r.measures.riseDb > 20.0);
        CHECK(r.measures.maxFrameDbfs == within(r.measures.p10FrameDbfs + r.measures.riseDb, 1e-9));
    }
}

TEST_CASE("no-signal: NaN and Inf do not break the window")
{
    std::vector<double> x = voice(kFs, 6.0, fromDb(-30.0), 3, 1.0);
    x[10] = std::nan("");
    x[20] = HUGE_VAL;
    const core::NoSignalResult r = core::analyseNoSignal(x, kFs);
    CHECK(r.verdict.state == core::SignalState::Ok);
    CHECK(std::isfinite(r.measures.maxFrameDbfs));
}

TEST_CASE("device processing: the 50 ms boundary is ceil(50 ms * fs) samples, at every rate")
{
    const std::map<double, std::size_t> runs{{44100.0, 2205}, {48000.0, 2400},  {88200.0, 4410},
                                             {96000.0, 4800}, {176400.0, 8820}, {192000.0, 9600}};
    core::DeviceProcessingContext context;
    context.noiseFloorDbfs = -60.0;
    for (const auto& [fs, minRun] : runs)
    {
        INFO("fs " << fs);
        const std::vector<double> body(static_cast<std::size_t>(fs), 0.1);
        for (std::size_t length : {minRun - 1, minRun})
        {
            const std::vector<double> x = concat(concat(body, std::vector<double>(length, 0.0)), body);
            const core::DeviceProcessingResult r = core::analyseDeviceProcessing(x, fs, context);
            CHECK(r.verdict.detected == (length == minRun));
            CHECK(r.measures.zeroRuns == (length == minRun ? 1 : 0));
            if (length == minRun)
            {
                CHECK(r.measures.longestZeroMs == within(50.0, 0.03));
                REQUIRE(r.evidence.marks.size() == 1);
                CHECK(r.evidence.marks[0].kind == core::EvidenceKind::ZeroRun);
            }
        }
    }
}

TEST_CASE("device processing: leading and trailing zeros and an all-zero input are not evidence")
{
    core::DeviceProcessingContext context;
    context.noiseFloorDbfs = -60.0;
    const std::vector<double> body(static_cast<std::size_t>(kFs), 0.1);
    const std::vector<double> edges = concat(concat(zeros(0.3), body), zeros(4.0));
    const core::DeviceProcessingResult r = core::analyseDeviceProcessing(edges, kFs, context);
    CHECK_FALSE(r.verdict.detected);
    CHECK(r.measures.zeroRuns == 0);
    CHECK_FALSE(core::analyseDeviceProcessing(zeros(2.0), kFs, context).verdict.detected);
}

TEST_CASE("device processing: zeros that 16-bit quantisation of a very quiet floor produces are not evidence")
{
    // 每 0.2 秒一个 LSB，其余都是严格零：16 bit 的量化格点上，底噪远低于 -107 dBFS
    auto sparse = [](double lsb) {
        std::vector<double> x = zeros(2.2);
        for (std::size_t i = 0; i <= 10; ++i)
            x[i * static_cast<std::size_t>(kFs / 5)] = (i % 2 ? 1.0 : -1.0) * lsb;
        return x;
    };
    const core::DeviceProcessingResult quantised = core::analyseDeviceProcessing(sparse(1.0 / 32768.0), kFs);
    CHECK(quantised.measures.zeroRuns > 0); // 连续量照常给出
    CHECK_FALSE(quantised.verdict.detected);
    // 同样的稀疏分布但不在 16 bit 格点上（24 bit 的最小一档）：防护不适用
    CHECK(core::analyseDeviceProcessing(sparse(1.0 / 8388608.0), kFs).verdict.detected);
}

TEST_CASE("no-signal: a quiet talker who speaks most of the time is not mistaken for a dead input")
{
    // 底噪 -90 dBFS，说话只比底噪高约 10 dB（仍低于绝对阈值），并且七成时间都在说：
    // 起伏要对着 P10 量；对着中位数量，“一直在说话”就成了“没有起伏”
    for (double fs : {44100.0, 48000.0, 96000.0})
    {
        INFO("fs " << fs);
        std::vector<double> x = whiteNoise(static_cast<std::size_t>(6.0 * fs), fromDb(-90.0), 1);
        const std::vector<double> talk = whiteNoise(x.size(), fromDb(-80.0), 2);
        for (std::size_t i = 0; i < x.size(); ++i)
            if (std::fmod(static_cast<double>(i) / fs, 1.0) < 0.7)
                x[i] += talk[i];
        const core::NoSignalResult r = core::analyseNoSignal(x, fs);
        CHECK(r.measures.maxFrameDbfs < -70.0);
        CHECK(r.measures.riseDb > 9.0);
        CHECK(r.verdict.state == core::SignalState::Ok);
        // 对照：只有底噪、没人说话，才是“可能没接话筒”
        CHECK(core::analyseNoSignal(whiteNoise(x.size(), fromDb(-90.0), 1), fs).verdict.state ==
              core::SignalState::VeryLowNoise);
    }
}

TEST_CASE("device processing: NaN and Inf count as zero, so they neither split a zero run nor defeat the 16-bit guard")
{
    core::DeviceProcessingContext context;
    context.noiseFloorDbfs = -60.0;
    const std::vector<double> body(static_cast<std::size_t>(kFs), 0.1);
    std::vector<double> gap = zeros(0.06);
    gap[1000] = std::numeric_limits<double>::quiet_NaN();
    gap[2000] = std::numeric_limits<double>::infinity();
    const core::DeviceProcessingResult r = core::analyseDeviceProcessing(concat(concat(body, gap), body), kFs, context);
    CHECK(r.verdict.detected);
    CHECK(r.measures.zeroRuns == 1);
    CHECK(r.measures.longestZeroMs == within(60.0, 0.03));

    // 16 bit 量化零的防护也不被一个 Inf 破坏
    std::vector<double> sparse = zeros(2.2);
    for (std::size_t i = 0; i <= 10; ++i)
        sparse[i * static_cast<std::size_t>(kFs / 5)] = (i % 2 ? 1.0 : -1.0) / 32768.0;
    sparse[static_cast<std::size_t>(0.1 * kFs)] = std::numeric_limits<double>::infinity();
    CHECK_FALSE(core::analyseDeviceProcessing(sparse, kFs).verdict.detected);
}

TEST_CASE("device processing: the counts stay exact while the evidence marks are capped")
{
    core::DeviceProcessingContext context;
    context.noiseFloorDbfs = -60.0;
    std::vector<double> x;
    for (int i = 0; i < 100; ++i)
    {
        x = concat(x, std::vector<double>(static_cast<std::size_t>(0.1 * kFs), 0.1));
        x = concat(x, zeros(0.06));
    }
    x = concat(x, std::vector<double>(static_cast<std::size_t>(0.1 * kFs), 0.1));
    const core::DeviceProcessingResult r = core::analyseDeviceProcessing(x, kFs, context);
    CHECK(r.verdict.detected);
    CHECK(r.measures.zeroRuns == 100);
    CHECK(r.measures.longestZeroMs == within(60.0, 0.03));
    CHECK(r.evidence.marks.size() == 64);
    CHECK(r.evidence.marks.front().startSec < r.evidence.marks.back().startSec);
}
