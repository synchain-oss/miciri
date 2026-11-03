// SPDX-License-Identifier: GPL-3.0-or-later
//
// 增益：峰值、真峰值、数字削波、模拟平顶、响度。真值都是解析的（已知幅度的正弦、已知削波电平）。
#include <catch2/catch_test_macros.hpp>

#include <map>

#include "core/level/Synth.h"
#include "level/Dsp.h"
#include "level/Level.h"

using namespace miciri;
using namespace miciri::testing;

namespace
{
double dbOf(double amplitude)
{
    return 20.0 * std::log10(amplitude);
}

std::vector<double> hardClipped(std::vector<double> x, double level)
{
    for (double& v : x)
        v = std::clamp(v, -level, level);
    return x;
}
} // namespace

TEST_CASE("peaks of a known sine, at every sample rate")
{
    for (double fs : kRates)
    {
        INFO("fs " << fs);
        const core::GainResult r = core::analyseGain(sine(fs, 1000.0, 0.5, 2.0), nullptr, fs);
        CHECK(r.measures.truePeakDbfsL == within(dbOf(0.5), 0.05));
        CHECK(r.measures.samplePeakDbfsL == within(dbOf(0.5), 0.03));
        CHECK(r.measures.truePeakDbfsL >= r.measures.samplePeakDbfsL - 1e-9);
        CHECK(r.measures.truePeakDbfsR == r.measures.truePeakDbfsL); // 单声道：右路与左路相同
        CHECK(r.verdict.band == core::GainBand::Good);
        CHECK_FALSE(r.verdict.digitalClip);
        CHECK(core::isConsistent(r.confidence));
    }
}

TEST_CASE("true peak finds the inter-sample peak a quarter-rate sine hides from the samples")
{
    for (double fs : kRates)
    {
        // fs/4、相位 45 度：样本落在 ±0.7071，真峰值是 1.0（0 dBFS）
        const std::vector<double> x = sine(fs, fs / 4.0, 1.0, 0.5, kPi / 4.0);
        const core::GainResult r = core::analyseGain(x, nullptr, fs);
        INFO("fs " << fs);
        CHECK(r.measures.samplePeakDbfsL == within(-3.0103, 0.01));
        CHECK(r.measures.truePeakDbfsL == within(0.0, 0.05));
    }
}

TEST_CASE("digital clipping counts runs, one per clipped half cycle")
{
    for (double fs : kRates)
    {
        INFO("fs " << fs);
        // 100 Hz、幅度 1.5 的正弦在 1.0 处硬削：2 秒 200 个周期，每周期正负各一段
        const std::vector<double> left = hardClipped(sine(fs, 100.0, 1.5, 2.0), 1.0);
        const std::vector<double> right = sine(fs, 100.0, 0.1, 2.0);
        const core::GainResult r = core::analyseGain(left, &right, fs);
        CHECK(r.measures.digitalClipRunsL == 400);
        CHECK(r.measures.digitalClipRunsR == 0);
        CHECK(r.verdict.digitalClip);
        CHECK(r.verdict.band == core::GainBand::TooLoud);
        CHECK(r.measures.truePeakDbfsR == within(dbOf(0.1), 0.05));
        REQUIRE_FALSE(r.evidence.marks.empty());
        CHECK(r.evidence.marks.front().kind == core::EvidenceKind::DigitalClip);
        CHECK(r.evidence.marks.front().channel == 0);
    }
}

TEST_CASE("a smooth waveform that merely touches full scale is not clipping, at any rate")
{
    for (double fs : kRates)
    {
        INFO("fs " << fs);
        CHECK(core::analyseGain(sine(fs, 440.0, 1.0, 1.0, 1.0), nullptr, fs).measures.digitalClipRunsL == 0);
        CHECK(core::analyseGain(sine(fs, 440.0, 0.9985, 1.0), nullptr, fs).measures.digitalClipRunsL == 0);
    }
}

TEST_CASE("the shortest clipped run scales with the sample rate")
{
    // max(3, round(62.5 us * fs))
    const std::map<double, std::size_t> minimumRun{{44100.0, 3}, {48000.0, 3},   {88200.0, 6},
                                                   {96000.0, 6}, {176400.0, 11}, {192000.0, 12}};
    for (const auto& [fs, minRun] : minimumRun)
    {
        INFO("fs " << fs);
        for (std::size_t run : {minRun - 1, minRun})
        {
            std::vector<double> x(static_cast<std::size_t>(fs / 2.0), 0.1);
            for (std::size_t i = 0; i < run; ++i)
                x[1000 + i] = 1.0;
            CHECK(core::analyseGain(x, nullptr, fs).measures.digitalClipRunsL == (run >= minRun ? 1 : 0));
        }
    }
}

TEST_CASE("analog flat tops are found by their repeated plateau, not by low frequency or clean peaks")
{
    for (double fs : {44100.0, 48000.0, 96000.0, 192000.0})
    {
        INFO("fs " << fs);
        // 220 Hz、幅度 0.9 的正弦在 0.5 处硬削，电平远低于数字削波阈值：一秒 220 个周期，正负各一个平台
        const core::GainResult flat = core::analyseGain(hardClipped(sine(fs, 220.0, 0.9, 1.0), 0.5), nullptr, fs);
        CHECK(flat.measures.flatPlateauPeaksL >= 200);
        CHECK(flat.verdict.maybeFlatTop);
        CHECK_FALSE(flat.verdict.digitalClip);

        // 规格原样的判据会把 50 到 100 Hz 的正弦（含很响的 50 Hz 电流声）全判成削波
        for (double hz : {50.0, 80.0, 100.0})
            CHECK_FALSE(core::analyseGain(sine(fs, hz, 0.5, 2.0), nullptr, fs).verdict.maybeFlatTop);
        CHECK_FALSE(core::analyseGain(sine(fs, 220.0, 0.9, 1.0), nullptr, fs).verdict.maybeFlatTop);
    }
}

TEST_CASE("a flat top under a noise floor needs the floor reading to be seen")
{
    const double fs = 48000.0;
    std::vector<double> x = hardClipped(sine(fs, 220.0, 0.9, 1.0), 0.5);
    mix(x, whiteNoise(x.size(), 0.001, 3));
    core::GainContext context;
    context.noiseFloorRms = 0.001;
    CHECK(core::analyseGain(x, nullptr, fs, context).verdict.maybeFlatTop);
}

TEST_CASE("K-weighted loudness of a full-scale sine is -3.01 LKFS at every rate, and silence is gated out")
{
    for (double fs : kRates)
    {
        INFO("fs " << fs);
        const std::vector<double> x = sine(fs, 997.0, 1.0, 3.0);
        const core::GainResult mono = core::analyseGain(x, nullptr, fs);
        CHECK(mono.measures.loudnessLkfs == within(-3.01, 0.06));
        const core::GainResult stereo = core::analyseGain(x, &x, fs); // 两路功率相加：+3.01
        CHECK(stereo.measures.loudnessLkfs == within(-3.01 + 3.0103, 0.06));

        std::vector<double> padded(static_cast<std::size_t>(2.0 * fs), 0.0);
        const std::vector<double> quiet = sine(fs, 997.0, 0.1, 3.0);
        const double alone = core::analyseGain(quiet, nullptr, fs).measures.loudnessLkfs;
        padded.insert(padded.end(), quiet.begin(), quiet.end());
        CHECK(core::analyseGain(padded, nullptr, fs).measures.loudnessLkfs == within(alone, 0.3));
    }
}

TEST_CASE("true peak bands follow the thresholds")
{
    const double fs = 48000.0;
    const std::vector<std::pair<double, core::GainBand>> cases{{-24.0, core::GainBand::TooQuiet},
                                                               {-15.0, core::GainBand::Quiet},
                                                               {-9.0, core::GainBand::Good},
                                                               {-4.5, core::GainBand::Loud},
                                                               {-1.5, core::GainBand::TooLoud}};
    for (const auto& [peakDb, band] : cases)
    {
        INFO("peak " << peakDb);
        CHECK(core::analyseGain(sine(fs, 1000.0, fromDb(peakDb), 1.0), nullptr, fs).verdict.band == band);
    }
}

TEST_CASE("gain does not depend on how the audio is cut into blocks, and survives NaN and Inf")
{
    const double fs = 96000.0;
    std::vector<double> left = hardClipped(sine(fs, 220.0, 0.9, 1.5), 0.5);
    mix(left, whiteNoise(left.size(), 0.001, 5));
    const std::vector<double> right = voice(fs, 1.5, 0.1, 9);
    core::GainContext context;
    context.noiseFloorRms = 0.001;
    const core::GainResult whole = core::analyseGain(left, &right, fs, context);

    core::GainAnalyser chunked(fs, true, context);
    Rng rng(11);
    for (std::size_t pos = 0; pos < left.size();)
    {
        const std::size_t length = std::min(left.size() - pos, 1 + static_cast<std::size_t>(rng.uniform() * 4000.0));
        chunked.push(left.data() + pos, right.data() + pos, length);
        pos += length;
    }
    const core::GainResult parts = chunked.finish();
    CHECK(parts.measures.truePeakDbfsL == whole.measures.truePeakDbfsL);
    CHECK(parts.measures.loudnessLkfs == whole.measures.loudnessLkfs);
    CHECK(parts.measures.flatPlateauPeaksL == whole.measures.flatPlateauPeaksL);
    CHECK(parts.measures.digitalClipRunsR == whole.measures.digitalClipRunsR);

    std::vector<double> dirty = left;
    dirty[100] = std::nan("");
    dirty[200] = HUGE_VAL;
    dirty[300] = -HUGE_VAL;
    const core::GainResult r = core::analyseGain(dirty, nullptr, fs, context);
    CHECK(std::isfinite(r.measures.loudnessLkfs));
    CHECK(std::isfinite(r.measures.truePeakDbfsL));
}

TEST_CASE("a flat top under a loud noise floor is only seen when the floor reading widens the plateau tolerance")
{
    const double fs = 48000.0;
    for (double noiseRms : {0.002, 0.004})
    {
        INFO("noise rms " << noiseRms);
        std::vector<double> x = hardClipped(sine(fs, 220.0, 0.9, 1.0), 0.5);
        mix(x, whiteNoise(x.size(), noiseRms, 3));
        // 没有底噪读数时只用相对容差：宁可漏报，也不把噪声顶出来的参差当成平台
        CHECK_FALSE(core::analyseGain(x, nullptr, fs).verdict.maybeFlatTop);
        core::GainContext context;
        context.noiseFloorRms = noiseRms;
        CHECK(core::analyseGain(x, nullptr, fs, context).verdict.maybeFlatTop);
    }
}
