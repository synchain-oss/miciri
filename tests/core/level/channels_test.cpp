// SPDX-License-Identifier: GPL-3.0-or-later
//
// 声道检查：一路全零、一路只有底噪或串扰、两路相同（各带独立底噪）、相位相反、真立体声、没有停顿的连续信号。
// 说话前留 1.5 秒静音，让每路都读得到自己的底噪；没有停顿的情形单独测回退阈值。
#include <catch2/catch_test_macros.hpp>

#include <limits>

#include "channels/Channels.h"
#include "core/level/Synth.h"

using namespace miciri;
using namespace miciri::testing;

namespace
{
constexpr double kFs = 48000.0;
constexpr double kSeconds = 7.5;

std::size_t samplesFor(double fs)
{
    return static_cast<std::size_t>(kSeconds * fs);
}

// 说话 -25 dBFS RMS，说话前 1.5 秒静音
std::vector<double> speech(double fs, std::uint64_t seed)
{
    return voice(fs, kSeconds, fromDb(-25.0), seed, 1.5);
}

std::vector<double> withFloor(std::vector<double> x, double floorDbfs, std::uint64_t seed)
{
    mix(x, whiteNoise(x.size(), fromDb(floorDbfs), seed));
    return x;
}

std::vector<double> scaled(const std::vector<double>& x, double gain)
{
    std::vector<double> y(x);
    for (double& v : y)
        v *= gain;
    return y;
}
} // namespace

TEST_CASE("channels: one strictly silent channel")
{
    const std::vector<double> sound = withFloor(speech(kFs, 1), -75.0, 2);
    const std::vector<double> zeros(sound.size(), 0.0);

    const core::ChannelsResult rightSilent = core::analyseChannels(sound, zeros, kFs);
    CHECK(rightSilent.verdict.kind == core::ChannelCase::OneSilent);
    CHECK(rightSilent.verdict.statusL == core::ChannelStatus::Signal);
    CHECK(rightSilent.verdict.statusR == core::ChannelStatus::Silent);
    CHECK(rightSilent.verdict.analysedChannel == core::AnalysedChannel::Left);
    CHECK(rightSilent.confidence.level == core::Reliability::Trusted);

    const core::ChannelsResult leftSilent = core::analyseChannels(zeros, sound, kFs);
    CHECK(leftSilent.verdict.kind == core::ChannelCase::OneSilent);
    CHECK(leftSilent.verdict.statusL == core::ChannelStatus::Silent);
    CHECK(leftSilent.verdict.analysedChannel == core::AnalysedChannel::Right);
}

TEST_CASE("channels: one channel that only carries its floor or a faint crosstalk of the other")
{
    const std::vector<double> sound = withFloor(speech(kFs, 3), -75.0, 4);
    for (double crosstalkDb : {-200.0, -90.0, -70.0, -60.0})
    {
        INFO("crosstalk " << crosstalkDb << " dB");
        std::vector<double> weak = withFloor(scaled(speech(kFs, 3), fromDb(crosstalkDb)), -75.0, 5);
        const core::ChannelsResult r = core::analyseChannels(sound, weak, kFs);
        CHECK(r.verdict.kind == core::ChannelCase::OneDominant);
        CHECK(r.verdict.statusL == core::ChannelStatus::Signal);
        CHECK(r.verdict.statusR == core::ChannelStatus::FloorOnly);
        CHECK(r.verdict.analysedChannel == core::AnalysedChannel::Left);
        CHECK(r.measures.deltaDb > 40.0);

        const core::ChannelsResult mirrored = core::analyseChannels(weak, sound, kFs);
        CHECK(mirrored.verdict.kind == core::ChannelCase::OneDominant);
        CHECK(mirrored.verdict.statusL == core::ChannelStatus::FloorOnly);
        CHECK(mirrored.verdict.analysedChannel == core::AnalysedChannel::Right);
    }
}

TEST_CASE("channels: the same signal with independent floors is identical, at every rate")
{
    for (double fs : kRates)
    {
        for (double floorDbfs : {-95.0, -85.0, -75.0})
        {
            INFO("fs " << fs << " floor " << floorDbfs);
            const std::vector<double> base = speech(fs, 6);
            const core::ChannelsResult r =
                core::analyseChannels(withFloor(base, floorDbfs, 7), withFloor(base, floorDbfs, 8), fs);
            CHECK(r.verdict.kind == core::ChannelCase::Identical);
            CHECK(r.verdict.statusR == core::ChannelStatus::SameAsLeft);
            CHECK(r.verdict.analysedChannel == core::AnalysedChannel::Left);
            CHECK_FALSE(r.measures.usedFallback);
            CHECK(r.measures.diffOverFloorDb < 3.0);
        }
    }
}

TEST_CASE("channels: bit-identical streams are identical")
{
    const std::vector<double> x = withFloor(speech(kFs, 9), -80.0, 10);
    const core::ChannelsResult r = core::analyseChannels(x, x, kFs);
    CHECK(r.verdict.kind == core::ChannelCase::Identical);
}

TEST_CASE("channels: opposite polarity is caught before it can cancel in a mono mix")
{
    const std::vector<double> base = speech(kFs, 11);
    const core::ChannelsResult r =
        core::analyseChannels(withFloor(base, -80.0, 12), withFloor(scaled(base, -1.0), -80.0, 13), kFs);
    CHECK(r.verdict.kind == core::ChannelCase::AntiPhase);
    CHECK(r.verdict.statusR == core::ChannelStatus::InvertedFromLeft);
    CHECK(r.verdict.analysedChannel == core::AnalysedChannel::Left);
    CHECK(r.measures.sumOverFloorDb <= 3.0);
    CHECK(r.measures.diffOverFloorDb > 10.0);
}

TEST_CASE("channels: real stereo is never taken for a copy, whatever the source")
{
    const std::vector<double> a = speech(kFs, 14);
    const std::vector<double> b = speech(kFs, 15);

    SECTION("two different talkers")
    {
        const core::ChannelsResult r = core::analyseChannels(withFloor(a, -80.0, 16), withFloor(b, -80.0, 17), kFs);
        CHECK(r.verdict.kind == core::ChannelCase::TrueStereo);
        CHECK(r.verdict.statusL == core::ChannelStatus::Signal);
        CHECK(r.verdict.statusR == core::ChannelStatus::Signal);
    }
    SECTION("one source panned down by up to 40 dB")
    {
        for (double panDb : {-6.0, -12.0, -20.0, -30.0, -40.0})
        {
            INFO("pan " << panDb);
            const core::ChannelsResult r =
                core::analyseChannels(withFloor(a, -80.0, 18), withFloor(scaled(a, fromDb(panDb)), -80.0, 19), kFs);
            CHECK(r.verdict.kind == core::ChannelCase::TrueStereo);
            CHECK(r.verdict.analysedChannel == core::AnalysedChannel::Left);
        }
    }
    SECTION("a one-sample delay and a 0.1 dB gain difference are still two channels")
    {
        std::vector<double> delayed(a.size(), 0.0);
        for (std::size_t i = 1; i < a.size(); ++i)
            delayed[i] = a[i - 1];
        CHECK(core::analyseChannels(withFloor(a, -75.0, 20), withFloor(delayed, -75.0, 21), kFs).verdict.kind ==
              core::ChannelCase::TrueStereo);
        CHECK(core::analyseChannels(withFloor(a, -75.0, 22), withFloor(scaled(a, fromDb(-0.1)), -75.0, 23), kFs)
                  .verdict.kind == core::ChannelCase::TrueStereo);
    }
    SECTION("the louder channel is analysed")
    {
        const core::ChannelsResult r =
            core::analyseChannels(withFloor(scaled(a, fromDb(-9.0)), -80.0, 24), withFloor(b, -80.0, 25), kFs);
        CHECK(r.verdict.kind == core::ChannelCase::TrueStereo);
        CHECK(r.verdict.analysedChannel == core::AnalysedChannel::Right);
    }
}

TEST_CASE("channels: with no pause to read a floor from, the relative fallback decides")
{
    // 持续不断的信号：每路都是两个正弦的叠加，没有停顿
    std::vector<double> tone = sine(kFs, 220.0, 0.1, kSeconds);
    mix(tone, sine(kFs, 333.0, 0.07, kSeconds, 1.0));

    SECTION("identical")
    {
        const core::ChannelsResult r =
            core::analyseChannels(withFloor(tone, -85.0, 26), withFloor(tone, -85.0, 27), kFs);
        CHECK(r.verdict.kind == core::ChannelCase::Identical);
        CHECK(r.measures.usedFallback);
    }
    SECTION("opposite polarity")
    {
        const core::ChannelsResult r =
            core::analyseChannels(withFloor(tone, -85.0, 28), withFloor(scaled(tone, -1.0), -85.0, 29), kFs);
        CHECK(r.verdict.kind == core::ChannelCase::AntiPhase);
        CHECK(r.measures.usedFallback);
    }
    SECTION("two different tones")
    {
        std::vector<double> other = sine(kFs, 247.0, 0.1, kSeconds);
        mix(other, sine(kFs, 401.0, 0.07, kSeconds, 2.0));
        const core::ChannelsResult r =
            core::analyseChannels(withFloor(tone, -85.0, 30), withFloor(other, -85.0, 31), kFs);
        CHECK(r.verdict.kind == core::ChannelCase::TrueStereo);
    }
}

TEST_CASE("channels: too little audio, silence on both channels and NaN input")
{
    const std::vector<double> sound = withFloor(speech(kFs, 32), -75.0, 33);

    const std::vector<double> few(100, 0.25);
    const core::ChannelsResult tooShort = core::analyseChannels(few, few, kFs);
    CHECK(tooShort.verdict.kind == core::ChannelCase::Undetermined);
    CHECK(tooShort.confidence.level == core::Reliability::Retest);
    CHECK(tooShort.confidence.reason == core::RetestReason::TooShort);

    const std::vector<double> zeros(sound.size(), 0.0);
    const core::ChannelsResult silent = core::analyseChannels(zeros, zeros, kFs);
    CHECK(silent.verdict.kind == core::ChannelCase::Undetermined);
    CHECK(silent.confidence.level == core::Reliability::Retest);

    // NaN 与 Inf 按 0 处理：不污染结果，也不改判
    std::vector<double> bad = sound;
    bad[1000] = std::numeric_limits<double>::quiet_NaN();
    bad[2000] = std::numeric_limits<double>::infinity();
    const core::ChannelsResult clean = core::analyseChannels(sound, withFloor(speech(kFs, 34), -75.0, 35), kFs);
    const core::ChannelsResult dirty = core::analyseChannels(bad, withFloor(speech(kFs, 34), -75.0, 35), kFs);
    CHECK(dirty.verdict.kind == clean.verdict.kind);
    CHECK(std::isfinite(dirty.measures.activeDbfsL));
    CHECK(std::isfinite(dirty.measures.floorDbfsL));
    CHECK(std::isfinite(dirty.measures.diffOverFloorDb));
}

TEST_CASE("channels: the verdict does not change with the sample rate")
{
    for (double fs : kRates)
    {
        INFO("fs " << fs);
        const std::vector<double> a = speech(fs, 36);
        const std::vector<double> b = speech(fs, 37);
        REQUIRE(a.size() == samplesFor(fs));
        CHECK(core::analyseChannels(withFloor(a, -80.0, 38), withFloor(b, -80.0, 39), fs).verdict.kind ==
              core::ChannelCase::TrueStereo);
        CHECK(core::analyseChannels(withFloor(a, -80.0, 40), std::vector<double>(a.size(), 0.0), fs).verdict.kind ==
              core::ChannelCase::OneSilent);
    }
}
