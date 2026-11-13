// SPDX-License-Identifier: GPL-3.0-or-later
//
// 电流声：已知基频与谐波的合成信号在各个采样率下的检出与估计，“轻微”的口径，buzz 与啸叫（频率上限、
// 绝对电平下限），反例，分块无关。
// hum 与噪声的比按分析带内的噪声功率算（高采样率会先抽取，带内噪声只剩 1/抽取倍数）。
#include <catch2/catch_test_macros.hpp>

#include "config/Thresholds.h"
#include "core/level/Synth.h"
#include "hum/Hum.h"
#include "level/Dsp.h"

using namespace miciri;
using namespace miciri::testing;

namespace
{
constexpr double kSigma = 0.001; // 噪声 RMS（全带）
constexpr double kSeconds = 8.0;

std::vector<double> noiseAt(double fs, std::uint64_t seed)
{
    return whiteNoise(static_cast<std::size_t>(kSeconds * fs), kSigma, seed);
}

// ratio：信号功率 / 分析带内的噪声功率
std::vector<double> withNoise(double fs, std::vector<double> tone, double ratio, std::uint64_t seed)
{
    (void)ratio;
    mix(tone, noiseAt(fs, seed));
    return tone;
}

double bandPower(double fs, double ratio)
{
    return ratio * kSigma * kSigma / decimation(fs);
}
} // namespace

TEST_CASE("mains hum of a known frequency is found at every rate")
{
    for (double fs : kRates)
    {
        for (double f0 : {49.8, 60.2})
        {
            INFO("fs " << fs << " f0 " << f0);
            const core::HumResult r = core::analyseHum(
                withNoise(fs, humSignal(fs, kSeconds, f0, kHumProfileDb, bandPower(fs, 1.0)), 1.0, 11), fs);
            CHECK(r.verdict.kind == core::HumKind::Hum);
            CHECK(r.measures.fundamentalHz == within(f0, 0.05));
            CHECK(r.measures.highestHarmonicHz == within(8.0 * f0, 1.0));
            CHECK(r.measures.overFloorDb > 20.0);
            CHECK(r.measures.energySharePct == within(50.0, 5.0)); // 信号与噪声同功率：一半
            CHECK_FALSE(r.verdict.slight);
            CHECK(r.confidence.level == core::Reliability::Trusted);
            REQUIRE(r.evidence.marks.size() >= 3);
            CHECK(r.evidence.marks.front().kind == core::EvidenceKind::HumPeak);
            CHECK(r.evidence.marks.front().value == within(f0, 1.0));
        }
    }
}

TEST_CASE("hum that barely stands out of the floor is still reported, and marked slight")
{
    for (double fs : {48000.0, 192000.0})
    {
        INFO("fs " << fs);
        const core::HumResult r = core::analyseHum(
            withNoise(fs, humSignal(fs, kSeconds, 50.0, kHumProfileDb, bandPower(fs, 0.001)), 0.001, 12), fs);
        CHECK(r.verdict.kind == core::HumKind::Hum);
        CHECK(r.measures.fundamentalHz == within(50.0, 0.05));
        CHECK(r.measures.overFloorDb < thr::hum::slight_below_over_floor_db);
        CHECK(r.verdict.slight);
    }
}

TEST_CASE("how much of the noise the hum is does not decide slight; how far it stands out does")
{
    // 同一个电流声，旁边加一个 1730 Hz 的强单音（不在 50 的整数倍附近，也低于啸叫的下限频率）：
    // 占噪声能量的比例从约一半掉到 1% 以下，但谐波高出本底的 dB 不变，所以仍不是轻微
    for (double fs : {48000.0, 96000.0})
    {
        INFO("fs " << fs);
        std::vector<double> x = humSignal(fs, kSeconds, 50.0, kHumProfileDb, bandPower(fs, 1.0));
        mix(x, sine(fs, 1730.0, std::sqrt(2.0 * 200.0 * bandPower(fs, 1.0)), kSeconds));
        const core::HumResult r = core::analyseHum(withNoise(fs, x, 1.0, 18), fs);
        CHECK(r.verdict.kind == core::HumKind::Hum);
        CHECK(r.measures.energySharePct < 1.0);
        CHECK(r.measures.overFloorDb >= thr::hum::slight_below_over_floor_db);
        CHECK_FALSE(r.verdict.slight);
    }
}

TEST_CASE("a harmonic series reaching past 1 kHz is buzz")
{
    std::vector<double> profile;
    for (int k = 1; k <= 60; ++k)
        profile.push_back(-3.0 * std::log2(static_cast<double>(k)));
    for (double fs : {48000.0, 96000.0})
    {
        INFO("fs " << fs);
        const core::HumResult r =
            core::analyseHum(withNoise(fs, humSignal(fs, kSeconds, 50.0, profile, bandPower(fs, 1.0)), 1.0, 13), fs);
        CHECK(r.verdict.kind == core::HumKind::Buzz);
        CHECK(r.measures.highestHarmonicHz > 1000.0);
    }
}

TEST_CASE("a stable narrow tone above 2 kHz is a whistle, not hum")
{
    for (double fs : kRates)
    {
        INFO("fs " << fs);
        const core::HumResult r = core::analyseHum(
            withNoise(fs, sine(fs, 4100.0, std::sqrt(2.0 * bandPower(fs, 1.0)), kSeconds), 1.0, 14), fs);
        CHECK(r.verdict.kind == core::HumKind::Whistle);
        CHECK(r.measures.fundamentalHz == within(4100.0, 1.0));
        CHECK(r.measures.overFloorDb > 15.0);
    }
}

TEST_CASE("hum that stands far out of a very quiet floor but is itself very quiet is slight")
{
    // 极安静的底噪（-120 dBFS）里，谐波都高出本底很多；只按电流声自己的电平分轻微与否
    constexpr double quietSigma = 1e-6;
    for (double fs : {48000.0, 96000.0})
    {
        INFO("fs " << fs);
        for (double humDbfs : {thr::hum::slight_below_dbfs - 20.0, thr::hum::slight_below_dbfs + 10.0})
        {
            INFO("hum " << humDbfs << " dBFS");
            std::vector<double> x = humSignal(fs, kSeconds, 50.0, kHumProfileDb, core::dsp::dbToPower(humDbfs));
            mix(x, whiteNoise(x.size(), quietSigma, 24));
            const core::HumResult r = core::analyseHum(x, fs);
            CHECK(r.verdict.kind == core::HumKind::Hum);
            CHECK(r.measures.overFloorDb >= thr::hum::slight_below_over_floor_db);
            CHECK(r.measures.levelDbfs == within(humDbfs, 1.0));
            CHECK(r.verdict.slight == (humDbfs < thr::hum::slight_below_dbfs));
        }
    }
}

TEST_CASE("a stable tone above the whistle band is left alone")
{
    // 16 kHz 的稳定强单音：真实录音里这一带常有接口与时钟的杂散，多数听不见
    for (double fs : {44100.0, 48000.0, 96000.0})
    {
        INFO("fs " << fs);
        const core::HumResult r = core::analyseHum(
            withNoise(fs, sine(fs, 16000.0, std::sqrt(2.0 * bandPower(fs, 1.0)), kSeconds), 1.0, 19), fs);
        CHECK(r.verdict.kind == core::HumKind::None);
    }
}

TEST_CASE("a whistle has to be loud enough in its own right, not only stand out of a very quiet floor")
{
    // 很安静的底噪（-100 dBFS）里的 4.1 kHz 单音：两种电平都远高出本底，只按单音自己的功率分
    constexpr double quietSigma = 1e-5;
    for (double fs : {48000.0, 96000.0})
    {
        INFO("fs " << fs);
        for (double toneDbfs : {thr::hum::whistle_min_dbfs - 10.0, thr::hum::whistle_min_dbfs + 10.0})
        {
            INFO("tone " << toneDbfs << " dBFS");
            std::vector<double> x = sine(fs, 4100.0, std::sqrt(2.0 * core::dsp::dbToPower(toneDbfs)), kSeconds);
            mix(x, whiteNoise(x.size(), quietSigma, 23));
            const core::HumResult r = core::analyseHum(x, fs);
            CHECK((r.verdict.kind == core::HumKind::Whistle) == (toneDbfs > thr::hum::whistle_min_dbfs));
        }
    }
}

TEST_CASE("things that are not mains hum are left alone")
{
    for (double fs : {48000.0, 96000.0})
    {
        for (std::uint64_t seed : {1, 2, 3})
            CHECK(core::analyseHum(noiseAt(fs, seed), fs).verdict.kind == core::HumKind::None);

        // 55 Hz 加两个弱谐波：不是 50 或 60 的整数倍
        INFO("fs " << fs);
        CHECK(core::analyseHum(withNoise(fs, humSignal(fs, kSeconds, 55.0, {0, -6, -6}, bandPower(fs, 10.0)), 10.0, 15),
                               fs)
                  .verdict.kind == core::HumKind::None);

        // 稳定的三音和弦（各带十次谐波）：只数前十次谐波，不该被当成电流声
        std::vector<double> chord(static_cast<std::size_t>(kSeconds * fs), 0.0);
        for (double f : {220.0, 277.18, 329.63})
            mix(chord, humSignal(fs, kSeconds, f, std::vector<double>(10, -3.0), bandPower(fs, 0.3)));
        const core::HumResult r = core::analyseHum(withNoise(fs, chord, 0.9, 16), fs);
        CHECK(r.verdict.kind != core::HumKind::Hum);
        CHECK(r.verdict.kind != core::HumKind::Buzz);
    }
}

TEST_CASE("one second of audio is too short for a Welch estimate")
{
    const core::HumResult r = core::analyseHum(noiseAt(48000.0, 1), 48000.0);
    CHECK(core::analyseHum(std::vector<double>(48000, 0.0), 48000.0).confidence.reason == core::RetestReason::TooShort);
    CHECK(r.verdict.kind == core::HumKind::None);
}

TEST_CASE("hum does not depend on how the audio is cut into blocks, and survives NaN")
{
    for (double fs : {44100.0, 96000.0})
    {
        INFO("fs " << fs);
        std::vector<double> x = withNoise(fs, humSignal(fs, 6.0, 50.0, kHumProfileDb, bandPower(fs, 1.0)), 1.0, 17);
        x.resize(static_cast<std::size_t>(6.0 * fs));
        const core::HumResult whole = core::analyseHum(x, fs);
        core::HumAnalyser chunked(fs);
        feedInRandomBlocks(chunked, x, 31);
        const core::HumResult parts = chunked.finish();
        REQUIRE(whole.verdict.kind == core::HumKind::Hum);
        CHECK(parts.verdict.kind == whole.verdict.kind);
        CHECK(parts.measures.fundamentalHz == whole.measures.fundamentalHz);
        CHECK(parts.measures.overFloorDb == whole.measures.overFloorDb);
        CHECK(parts.measures.energySharePct == whole.measures.energySharePct);
        CHECK(parts.measures.levelDbfs == whole.measures.levelDbfs);
        CHECK(parts.evidence.marks.size() == whole.evidence.marks.size());

        x[1000] = std::nan("");
        x[2000] = HUGE_VAL;
        CHECK(core::analyseHum(x, fs).verdict.kind == core::HumKind::Hum);
    }
}

TEST_CASE("one or two stable mains harmonics are not enough, three are")
{
    for (double fs : {48000.0, 96000.0})
    {
        INFO("fs " << fs);
        const double power = bandPower(fs, 10.0);
        CHECK(core::analyseHum(withNoise(fs, humSignal(fs, kSeconds, 50.0, {0}, power), 10.0, 20), fs).verdict.kind ==
              core::HumKind::None);
        CHECK(
            core::analyseHum(withNoise(fs, humSignal(fs, kSeconds, 50.0, {0, -3}, power), 10.0, 21), fs).verdict.kind ==
            core::HumKind::None);
        const core::HumResult three =
            core::analyseHum(withNoise(fs, humSignal(fs, kSeconds, 50.0, {0, -3, -6}, power), 10.0, 22), fs);
        CHECK(three.verdict.kind == core::HumKind::Hum);
        CHECK(three.measures.fundamentalHz == within(50.0, 0.1));
    }
}
