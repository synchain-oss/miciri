// SPDX-License-Identifier: GPL-3.0-or-later
//
// 噪声与说话电平：已知电平的正弦、事件污染下 P10 的稳健性、采样率与分块无关。
#include <catch2/catch_test_macros.hpp>

#include "core/level/Synth.h"
#include "noise/Noise.h"

using namespace miciri;
using namespace miciri::testing;

TEST_CASE("noise level of a known sine, weighted and unweighted, at every rate")
{
    for (double fs : kRates)
    {
        INFO("fs " << fs);
        const double unweighted = 20.0 * std::log10(0.01 / std::sqrt(2.0));
        // 100 Hz 的 A 加权是 -19.1 dB（标准表）
        const core::NoiseResult low = core::analyseNoise(sine(fs, 100.0, 0.01, 3.0), fs);
        CHECK(low.measures.noiseDbfs == within(unweighted, 0.05));
        CHECK(low.measures.noiseDbfsA == within(unweighted - 19.1, 0.15));
        CHECK(low.measures.noisePeakDbfs == within(unweighted, 0.1));
        // 1 kHz 处加权为 0
        const core::NoiseResult mid = core::analyseNoise(sine(fs, 1000.0, 0.01, 3.0), fs);
        CHECK(mid.measures.noiseDbfsA == within(mid.measures.noiseDbfs, 0.05));
    }
}

TEST_CASE("P10 ignores coughs, door slams and keyboard clicks")
{
    const double fs = 48000.0;
    const std::size_t n = static_cast<std::size_t>(10 * fs);
    const std::vector<double> clean = lowpassNoise(fs, n, fromDb(-60.0), 1500.0, 2);
    std::vector<double> polluted = clean;
    for (int i = 0; i < 10; ++i)
    {
        const std::vector<double> burst = whiteNoise(static_cast<std::size_t>(0.2 * fs), fromDb(-30.0), 100 + i);
        const auto start = static_cast<std::size_t>((0.5 + 0.95 * i) * fs);
        for (std::size_t k = 0; k < burst.size(); ++k)
            polluted[start + k] += burst[k];
    }
    const core::NoiseResult a = core::analyseNoise(clean, fs);
    const core::NoiseResult b = core::analyseNoise(polluted, fs);
    CHECK(b.measures.noiseDbfsA == within(a.measures.noiseDbfsA, 0.5));
    CHECK(b.measures.noiseDbfs == within(a.measures.noiseDbfs, 0.5));
    // 最响一帧不被忽略：峰值线要看得到咳嗽
    CHECK(b.measures.noisePeakDbfs > a.measures.noisePeakDbfs + 15.0);
}

TEST_CASE("the same noise reads the same at every sample rate")
{
    const core::NoiseResult reference =
        core::analyseNoise(lowpassNoise(48000.0, 6 * 48000, fromDb(-50.0), 1500.0, 7), 48000.0);
    for (double fs : kRates)
    {
        INFO("fs " << fs);
        const core::NoiseResult r =
            core::analyseNoise(lowpassNoise(fs, static_cast<std::size_t>(6 * fs), fromDb(-50.0), 1500.0, 7), fs);
        CHECK(r.measures.noiseDbfs == within(reference.measures.noiseDbfs, 0.5));
        CHECK(r.measures.noiseDbfsA == within(reference.measures.noiseDbfsA, 0.5));
    }
}

TEST_CASE("SNR is speech minus noise and picks the band from the thresholds")
{
    const double fs = 48000.0;
    const std::vector<double> noise = lowpassNoise(fs, 6 * 48000, fromDb(-60.0), 1500.0, 3);
    const double floorA = core::analyseNoise(noise, fs).measures.noiseDbfsA;
    const std::vector<std::pair<double, core::NoiseBand>> cases{
        {50.0, core::NoiseBand::Good}, {40.0, core::NoiseBand::Fair}, {20.0, core::NoiseBand::Bad}};
    for (const auto& [snr, band] : cases)
    {
        core::NoiseContext context;
        context.speechLevelDbfsA = floorA + snr;
        const core::NoiseResult r = core::analyseNoise(noise, fs, context);
        INFO("snr " << snr);
        CHECK(r.measures.snrDb == within(snr, 1e-9));
        CHECK(r.verdict.band == band);
        CHECK(core::isConsistent(r.confidence));
        CHECK(r.confidence.level == core::Reliability::Trusted);
    }
    // 没有说话电平就算不出 SNR：不假装有档
    CHECK(core::analyseNoise(noise, fs).confidence.level == core::Reliability::Retest);
}

TEST_CASE("speech level of 1 kHz bursts is their RMS, at every rate")
{
    for (double fs : kRates)
    {
        INFO("fs " << fs);
        // 0.3 秒底噪、0.3 秒 1 kHz 幅度 0.1（RMS -23.01 dBFS），交替；边界与 50 ms 帧对齐
        std::vector<double> x = whiteNoise(static_cast<std::size_t>(6 * fs), 1e-5, 5);
        const auto burst = static_cast<std::size_t>(0.3 * fs);
        for (int i = 1; i < 20; i += 2)
        {
            const std::vector<double> tone = sine(fs, 1000.0, 0.1, 0.3);
            for (std::size_t k = 0; k < burst; ++k)
                x[static_cast<std::size_t>(i) * burst + k] += tone[k];
        }
        const std::optional<double> level = core::speechLevelDbfsA(x, fs);
        REQUIRE(level.has_value());
        CHECK(*level == within(20.0 * std::log10(0.1 / std::sqrt(2.0)), 0.2));
    }
    CHECK_FALSE(core::speechLevelDbfsA({}, 48000.0).has_value());
}

TEST_CASE("too little audio is flagged for a retest")
{
    const double fs = 48000.0;
    core::NoiseContext context;
    context.speechLevelDbfsA = -20.0;
    const core::NoiseResult r = core::analyseNoise(lowpassNoise(fs, 24000, fromDb(-60.0), 1500.0, 1), fs, context);
    CHECK(r.confidence.level == core::Reliability::Retest);
    CHECK(r.confidence.reason == core::RetestReason::TooShort);
    CHECK(core::analyseNoise({}, fs, context).confidence.reason == core::RetestReason::TooShort);
}

TEST_CASE("noise does not depend on how the audio is cut into blocks, and survives NaN")
{
    for (double fs : {44100.0, 96000.0})
    {
        INFO("fs " << fs);
        std::vector<double> x = lowpassNoise(fs, static_cast<std::size_t>(4 * fs), fromDb(-55.0), 1500.0, 9);
        core::NoiseContext context;
        context.speechLevelDbfsA = -25.0;
        const core::NoiseResult whole = core::analyseNoise(x, fs, context);
        core::NoiseAnalyser chunked(fs, context);
        feedInRandomBlocks(chunked, x, 21);
        const core::NoiseResult parts = chunked.finish();
        CHECK(parts.measures.noiseDbfs == whole.measures.noiseDbfs);
        CHECK(parts.measures.noiseDbfsA == whole.measures.noiseDbfsA);
        CHECK(parts.measures.noisePeakDbfs == whole.measures.noisePeakDbfs);

        core::SpeechLevelAnalyser speech(fs);
        feedInRandomBlocks(speech, x, 22);
        CHECK(speech.finish() == core::speechLevelDbfsA(x, fs));

        x[500] = std::nan("");
        x[501] = HUGE_VAL;
        CHECK(std::isfinite(core::analyseNoise(x, fs, context).measures.noiseDbfsA));
    }
}
