// SPDX-License-Identifier: GPL-3.0-or-later
//
// 处理器：逐位直通（含 NaN、Inf、非规格化数、负零）、电平（真峰值与均方根）与削波灯、布局声明、心跳。
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "plugin/MiciriProcessor.h"

namespace
{
using miciri::MiciriProcessor;
namespace pl = miciri::plugin;

// 确定性的线性同余序列；样本直接取随机比特，随机地覆盖 NaN、Inf、非规格化数。
std::uint32_t next(std::uint32_t& s)
{
    s = s * 1664525u + 1013904223u;
    return s;
}

template<class Sample>
Sample fromBits(std::uint64_t bits)
{
    Sample v;
    std::memcpy(&v, &bits, sizeof(Sample));
    return v;
}

// 每个声道开头放一组特殊值，后面是随机比特。
template<class Sample>
void fillWithSpecials(juce::AudioBuffer<Sample>& buffer, std::uint32_t seed)
{
    constexpr bool isDouble = sizeof(Sample) == 8;
    const std::uint64_t specials[] = {
        isDouble ? 0x7ff8000000000000ull : 0x7fc00000ull, // 安静 NaN
        isDouble ? 0x7ff0000000000001ull : 0x7fa00000ull, // 信号 NaN
        isDouble ? 0xfff8000000000001ull : 0xffc00001ull, // 负 NaN
        isDouble ? 0x7ff0000000000000ull : 0x7f800000ull, // +Inf
        isDouble ? 0xfff0000000000000ull : 0xff800000ull, // -Inf
        isDouble ? 0x8000000000000000ull : 0x80000000ull, // -0.0
        0x1ull, // 最小非规格化数
    };
    std::uint32_t s = seed;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* d = buffer.getWritePointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto hi = static_cast<std::uint64_t>(next(s));
            const auto lo = static_cast<std::uint64_t>(next(s));
            d[i] = fromBits<Sample>(isDouble ? (hi << 32) | lo : hi);
        }
        const int n = std::min<int>(buffer.getNumSamples(), static_cast<int>(sizeof(specials) / sizeof(specials[0])));
        for (int i = 0; i < n; ++i)
            d[i] = fromBits<Sample>(specials[i]);
    }
}

template<class Sample>
bool bitIdentical(const juce::AudioBuffer<Sample>& a, const juce::AudioBuffer<Sample>& b)
{
    if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples())
        return false;
    for (int ch = 0; ch < a.getNumChannels(); ++ch)
        if (std::memcmp(a.getReadPointer(ch), b.getReadPointer(ch),
                        sizeof(Sample) * static_cast<std::size_t>(a.getNumSamples())) != 0)
            return false;
    return true;
}

void configure(MiciriProcessor& p, int channels, int blockSize)
{
    const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add(set);
    layout.outputBuses.add(set);
    REQUIRE(p.setBusesLayout(layout));
    p.prepareToPlay(48000.0, blockSize);
}

template<class Sample>
void constantBlock(juce::AudioBuffer<Sample>& b, Sample value)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.getWritePointer(ch)[i] = value;
}
} // namespace

TEST_CASE("the comparison helper detects a single flipped bit", "[plugin]")
{
    juce::AudioBuffer<float> a(2, 16);
    fillWithSpecials(a, 1);
    juce::AudioBuffer<float> b;
    b.makeCopyOf(a);
    REQUIRE(bitIdentical(a, b));
    std::uint32_t bits;
    std::memcpy(&bits, b.getReadPointer(1) + 9, sizeof bits);
    bits ^= 1u;
    std::memcpy(b.getWritePointer(1) + 9, &bits, sizeof bits);
    CHECK_FALSE(bitIdentical(a, b));
}

TEST_CASE("processBlock leaves every bit of the buffer unchanged", "[plugin]")
{
    const int channels = GENERATE(1, 2);
    const int blockSize = GENERATE(1, 7, 64, 512, 4096);
    MiciriProcessor p;
    configure(p, channels, blockSize);
    juce::MidiBuffer midi;

    SECTION("float")
    {
        juce::AudioBuffer<float> buffer(channels, blockSize);
        fillWithSpecials(buffer, 0x1234u + static_cast<std::uint32_t>(blockSize));
        juce::AudioBuffer<float> before;
        before.makeCopyOf(buffer);
        p.processBlock(buffer, midi);
        CHECK(bitIdentical(buffer, before));
        p.processBlockBypassed(buffer, midi);
        CHECK(bitIdentical(buffer, before));
    }
    SECTION("double")
    {
        juce::AudioBuffer<double> buffer(channels, blockSize);
        fillWithSpecials(buffer, 0x4321u + static_cast<std::uint32_t>(blockSize));
        juce::AudioBuffer<double> before;
        before.makeCopyOf(buffer);
        p.processBlock(buffer, midi);
        CHECK(bitIdentical(buffer, before));
        p.processBlockBypassed(buffer, midi);
        CHECK(bitIdentical(buffer, before));
    }
}

TEST_CASE("an empty block changes nothing: no heartbeat, no meter movement", "[plugin]")
{
    MiciriProcessor p;
    configure(p, 2, 512);
    juce::MidiBuffer midi;

    // 先喂一块有内容的，让电平和心跳有值；之后的空块不能动它们。
    juce::AudioBuffer<float> loud(2, 512);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            loud.getWritePointer(ch)[i] = 0.5f;
    p.processBlock(loud, midi);
    const auto beatBefore = p.heartbeat();

    juce::AudioBuffer<float> empty(2, 0);
    p.processBlock(empty, midi);
    p.processBlockBypassed(empty, midi);
    juce::AudioBuffer<double> emptyDouble(2, 0);
    p.processBlock(emptyDouble, midi);
    CHECK(p.heartbeat() == beatBefore);

    const auto m = p.readMeters();
    CHECK(m.levelDbfs[0] == Catch::Approx(-6.0206).margin(0.001)); // 空块不清零也不稀释均方根
    CHECK(m.peakDbfs[0] == Catch::Approx(-6.0206).margin(0.001));
}

TEST_CASE("the heartbeat advances once per callback, bypassed or not", "[plugin]")
{
    MiciriProcessor p;
    configure(p, 2, 64);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer(2, 64);
    buffer.clear();
    CHECK(p.heartbeat() == 0);
    p.processBlock(buffer, midi);
    p.processBlock(buffer, midi);
    p.processBlockBypassed(buffer, midi);
    CHECK(p.heartbeat() == 3);
}

TEST_CASE("NaN and Inf never poison the meters", "[plugin]")
{
    MiciriProcessor p;
    configure(p, 2, 256);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer(2, 256);

    SECTION("NaN only: finite readings, no clip latch")
    {
        constantBlock(buffer, std::numeric_limits<float>::quiet_NaN());
        p.processBlock(buffer, midi);
        const auto m = p.readMeters();
        for (int ch = 0; ch < 2; ++ch)
        {
            CHECK(std::isfinite(m.levelDbfs[ch]));
            CHECK(std::isfinite(m.peakDbfs[ch]));
            CHECK(m.peakDbfs[ch] == pl::kMeterFloorDbfs);
            CHECK_FALSE(m.clipLatch[ch]);
        }
    }

    SECTION("Inf: peak clamps to the ceiling, the clip latch trips, the level stays finite")
    {
        constantBlock(buffer, std::numeric_limits<float>::infinity());
        p.processBlock(buffer, midi);
        const auto m = p.readMeters();
        for (int ch = 0; ch < 2; ++ch)
        {
            CHECK(m.peakDbfs[ch] == pl::kMeterCeilDbfs);
            CHECK(std::isfinite(m.levelDbfs[ch]));
            CHECK(m.clipLatch[ch]);
        }
    }

    SECTION("a normal block right after a poisoned one reads correctly")
    {
        constantBlock(buffer, std::numeric_limits<float>::quiet_NaN());
        p.processBlock(buffer, midi);
        constantBlock(buffer, std::numeric_limits<float>::infinity());
        p.processBlock(buffer, midi);
        (void)p.readMeters();
        p.clearClipLatch();

        // 先读掉一块 0.5：插值历史里的 NaN 与 Inf 已经当 0，从 0 到 0.5 的台阶本来就有真峰值的过冲，读数要等历史稳定
        constantBlock(buffer, 0.5f);
        p.processBlock(buffer, midi);
        (void)p.readMeters();
        p.processBlock(buffer, midi);
        const auto m = p.readMeters();
        const double expected = 20.0 * std::log10(0.5);
        for (int ch = 0; ch < 2; ++ch)
        {
            CHECK(m.peakDbfs[ch] == Catch::Approx(expected).margin(1e-6));
            CHECK(m.levelDbfs[ch] == Catch::Approx(expected).margin(1e-6));
            CHECK_FALSE(m.clipLatch[ch]);
        }
    }
}

TEST_CASE("the peak resets on read", "[plugin]")
{
    MiciriProcessor p;
    configure(p, 2, 128);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer(2, 128);

    constantBlock(buffer, 1.0f);
    p.processBlock(buffer, midi);
    constantBlock(buffer, 0.1f);
    p.processBlock(buffer, midi);

    auto m = p.readMeters(0);
    // 峰值保留了整个读取间隔里的最大值；真峰值不低于样本峰值，从 1.0 落到 0.1 的台阶只会略有过冲
    CHECK(m.peakDbfs[0] >= -1e-6);
    CHECK(m.peakDbfs[0] < 1.0);
    // 均方根是整个间隔的：两块各 128 个样本，(1.0² + 0.1²) / 2
    CHECK(m.levelDbfs[0] == Catch::Approx(20.0 * std::log10(std::sqrt(0.505))).margin(1e-6));

    m = p.readMeters(33);
    CHECK(m.peakDbfs[0] == pl::kMeterFloorDbfs); // 峰值读后清零
    CHECK(m.levelDbfs[0] == pl::kMeterFloorDbfs); // 没有新样本，均方根也不沿用旧值
}

TEST_CASE("the meter reads the true peak between samples", "[plugin]")
{
    // 四分之一采样率的正弦、相位 45 度：样本全是 ±0.707 A，连续波形的峰是 A，样本峰值会少报约 3 dB
    constexpr double amplitude = 0.9;
    MiciriProcessor p;
    configure(p, 2, 256);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer(2, 256);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 256; ++i)
            buffer.getWritePointer(ch)[i] = static_cast<float>(
                amplitude * std::sin(std::acos(-1.0) * 0.5 * static_cast<double>(i) + std::acos(-1.0) / 4.0));
    p.processBlock(buffer, midi);
    const auto m = p.readMeters(0);
    const double samplePeakDb = 20.0 * std::log10(amplitude * std::sqrt(0.5));
    const double truePeakDb = 20.0 * std::log10(amplitude);
    for (int ch = 0; ch < 2; ++ch)
    {
        CHECK(m.peakDbfs[ch] > samplePeakDb + 2.0); // 不是样本峰值
        CHECK(m.peakDbfs[ch] == Catch::Approx(truePeakDb).margin(0.1));
    }
}

TEST_CASE("the clip lamp goes out five seconds after the last clip and clears at once on click", "[plugin]")
{
    MiciriProcessor p;
    configure(p, 2, 128);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer(2, 128);
    const auto clipOnce = [&] {
        constantBlock(buffer, 1.0f);
        p.processBlock(buffer, midi);
        constantBlock(buffer, 0.1f);
        p.processBlock(buffer, midi);
    };
    // 起点贴着 32 位毫秒计数的回绕点，顺带验证回绕
    const std::uint32_t t0 = 0xFFFFFC00u;

    SECTION("lit by a clip, still lit just before the hold ends, out at the hold, and quiet blocks do not relight it")
    {
        CHECK_FALSE(p.readMeters(t0).clipLatch[0]);
        clipOnce();
        CHECK(p.readMeters(t0 + 33).clipLatch[0]);
        CHECK(p.readMeters(t0 + 33).clipLatch[1]);
        CHECK(p.readMeters(t0 + 33 + pl::kClipLampHoldMs - 1).clipLatch[0]);
        const auto out = p.readMeters(t0 + 33 + pl::kClipLampHoldMs);
        CHECK_FALSE(out.clipLatch[0]);
        CHECK_FALSE(out.clipLatch[1]);
        constantBlock(buffer, 0.1f);
        p.processBlock(buffer, midi);
        CHECK_FALSE(p.readMeters(t0 + 33 + pl::kClipLampHoldMs + 33).clipLatch[0]);
    }

    SECTION("a new clip restarts the five seconds")
    {
        clipOnce();
        CHECK(p.readMeters(t0).clipLatch[0]);
        clipOnce();
        CHECK(p.readMeters(t0 + 3000).clipLatch[0]);
        CHECK(p.readMeters(t0 + 3000 + pl::kClipLampHoldMs - 1).clipLatch[0]); // 距第一次已超过 5 秒，仍亮
        CHECK_FALSE(p.readMeters(t0 + 3000 + pl::kClipLampHoldMs).clipLatch[0]);
    }

    SECTION("clicking clears at once, the clip already counted does not relight it, a later clip does")
    {
        clipOnce();
        CHECK(p.readMeters(t0).clipLatch[0]);
        p.clearClipLatch();
        CHECK_FALSE(p.readMeters(t0 + 33).clipLatch[0]);
        CHECK_FALSE(p.readMeters(t0 + 66).clipLatch[1]);
        clipOnce();
        CHECK(p.readMeters(t0 + 99).clipLatch[0]);
    }

    SECTION("only the channel that clipped lights")
    {
        constantBlock(buffer, 0.1f);
        for (int i = 0; i < 128; ++i)
            buffer.getWritePointer(1)[i] = 1.0f;
        p.processBlock(buffer, midi);
        const auto m = p.readMeters(t0);
        CHECK_FALSE(m.clipLatch[0]);
        CHECK(m.clipLatch[1]);
    }
}

TEST_CASE("only mono and stereo are declared, with matching input and output", "[plugin]")
{
    MiciriProcessor p;
    using Layout = juce::AudioProcessor::BusesLayout;
    const auto layoutOf = [](const juce::AudioChannelSet& in, const juce::AudioChannelSet& out) {
        Layout l;
        l.inputBuses.add(in);
        l.outputBuses.add(out);
        return l;
    };
    const auto mono = juce::AudioChannelSet::mono();
    const auto stereo = juce::AudioChannelSet::stereo();
    CHECK(p.isBusesLayoutSupported(layoutOf(mono, mono)));
    CHECK(p.isBusesLayoutSupported(layoutOf(stereo, stereo)));
    CHECK_FALSE(p.isBusesLayoutSupported(layoutOf(mono, stereo)));
    CHECK_FALSE(p.isBusesLayoutSupported(layoutOf(stereo, mono)));
    CHECK_FALSE(p.isBusesLayoutSupported(
        layoutOf(juce::AudioChannelSet::create5point1(), juce::AudioChannelSet::create5point1())));
    CHECK_FALSE(
        p.isBusesLayoutSupported(layoutOf(juce::AudioChannelSet::disabled(), juce::AudioChannelSet::disabled())));
}

TEST_CASE("zero latency, no parameters, no MIDI, no state", "[plugin]")
{
    MiciriProcessor p;
    CHECK(p.getLatencySamples() == 0);
    CHECK(p.getParameters().isEmpty());
    CHECK_FALSE(p.acceptsMidi());
    CHECK_FALSE(p.producesMidi());
    CHECK(p.getTailLengthSeconds() == 0.0);
    CHECK(p.supportsDoublePrecisionProcessing());

    juce::MemoryBlock state;
    p.getStateInformation(state);
    CHECK(state.getSize() == 0);
    const char junk[] = "not a state";
    p.setStateInformation(junk, static_cast<int>(sizeof junk)); // 忽略，不崩
}

TEST_CASE("the device snapshot follows prepareToPlay", "[plugin]")
{
    MiciriProcessor p;
    configure(p, 1, 256);
    const auto d = p.deviceInfo();
    CHECK(d.sampleRate == 48000.0);
    CHECK(d.bufferSize == 256);
    CHECK(d.numChannels == 1);
}

TEST_CASE("mic type accepts only known indices", "[plugin]")
{
    MiciriProcessor p;
    CHECK(p.micType() == 0);
    CHECK_FALSE(p.setMicType(-1));
    CHECK_FALSE(p.setMicType(1000));
    CHECK(p.micType() == 0);
    CHECK(p.setMicType(1));
    CHECK(p.micType() == 1);
}
