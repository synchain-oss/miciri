// SPDX-License-Identifier: GPL-3.0-or-later
//
// 带探针的处理器（本目标定义 MICIRI_HOST_PROBE）：音频块按“离线与否、是否全 0”计数，音频仍然逐位直通；
// 环境变量给了路径时，在 prepareToPlay、releaseResources、析构和定时器上写 JSON；没设就什么都不写。
// 默认（OFF）的构建也编这个目标，所以处理器里那几行探针接线有测试守着。
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>

#include "HostProbeTestSupport.h"
#include "plugin/HostProbe.h"
#include "plugin/MiciriProcessor.h"

namespace
{
namespace fs = std::filesystem;
namespace probe = miciri::probe;
using miciri::MiciriProcessor;
using namespace miciri::probe_test;

void configure(MiciriProcessor& p, int blockSize)
{
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add(juce::AudioChannelSet::stereo());
    layout.outputBuses.add(juce::AudioChannelSet::stereo());
    REQUIRE(p.setBusesLayout(layout));
    p.prepareToPlay(48000.0, blockSize);
}

void fill(juce::AudioBuffer<float>& b, float value)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.getWritePointer(ch)[i] = value;
}
} // namespace

TEST_CASE("with the probe on, blocks are counted by realtime-ness and silence, and the audio stays bit-identical",
          "[plugin][probe]")
{
    EnvCleanup cleanup; // 本用例不写文件：确保变量没设
    MiciriProcessor p;
    configure(p, 64);
    juce::MidiBuffer midi;
    const auto before = probe::counters().snapshot(); // 进程内共用一份计数，断言用差值

    juce::AudioBuffer<float> silent(2, 64);
    silent.clear();
    juce::AudioBuffer<float> loud(2, 64);
    fill(loud, 0.5f);
    juce::AudioBuffer<double> silentDouble(2, 64);
    silentDouble.clear();

    // 实时：2 块全 0（含一次旁路），1 块有内容。
    p.processBlock(silent, midi);
    p.processBlockBypassed(silent, midi);
    p.processBlock(loud, midi);
    CHECK_FALSE(probe::counters().snapshot().lastIsNonRealtime);

    // 离线：1 块双精度全 0，1 块有内容，1 块只有一个非规格化数（处理线程开着清零模式也必须算有内容）。
    p.setNonRealtime(true);
    p.processBlock(silentDouble, midi);
    p.processBlock(loud, midi);
    juce::AudioBuffer<float> denormal(2, 64);
    denormal.clear();
    const std::uint32_t denormBits = 1u;
    std::memcpy(denormal.getWritePointer(1) + 63, &denormBits, sizeof denormBits);
    juce::AudioBuffer<float> denormalBefore;
    denormalBefore.makeCopyOf(denormal);
    p.processBlock(denormal, midi);
    CHECK(probe::counters().snapshot().lastIsNonRealtime);

    // 0 样本的调用不是一次有内容的回调，不计数。
    juce::AudioBuffer<float> empty(2, 0);
    p.processBlock(empty, midi);

    const auto after = probe::counters().snapshot();
    CHECK(after.blocksTotal - before.blocksTotal == 6);
    CHECK(after.blocksRealtime - before.blocksRealtime == 3);
    CHECK(after.blocksNonRealtime - before.blocksNonRealtime == 3);
    CHECK(after.blocksAllZero - before.blocksAllZero == 3);
    CHECK(after.blocksNonZero - before.blocksNonZero == 3);

    // 逐位直通：NaN、Inf、非规格化数的随机比特也原样出来。
    juce::AudioBuffer<float> noisy(2, 64);
    std::uint32_t s = 99u;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 64; ++i)
        {
            s = s * 1664525u + 1013904223u;
            std::memcpy(noisy.getWritePointer(ch) + i, &s, sizeof s);
        }
    juce::AudioBuffer<float> noisyBefore;
    noisyBefore.makeCopyOf(noisy);
    p.processBlock(noisy, midi);
    for (int ch = 0; ch < 2; ++ch)
    {
        CHECK(std::memcmp(noisy.getReadPointer(ch), noisyBefore.getReadPointer(ch), 64 * sizeof(float)) == 0);
        CHECK(std::memcmp(denormal.getReadPointer(ch), denormalBefore.getReadPointer(ch), 64 * sizeof(float)) == 0);
    }
}

TEST_CASE("with the probe on, the analysis hook feeds analysisCount", "[plugin][probe]")
{
    const auto before = probe::counters().snapshot().analysisCount;
    probe::noteAnalysis();
    CHECK(probe::counters().snapshot().analysisCount == before + 1);
}

// 离线渲染时探针的分析计数必须保持 0：宿主报了离线就不开始测试、正在测也不收样本；
// 宿主没报（REAPER 关掉“通知插件离线渲染”时离线渲染也报实时），块来得明显快于实时，这一步同样作废、不做分析。
TEST_CASE("an offline render never reaches the analysis, whether or not the host announces it", "[plugin][probe]")
{
    using miciri::core::StepId;
    using Start = miciri::session::Session::Start;
    EnvCleanup cleanup;
    MiciriProcessor p;
    configure(p, 512);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> loud(2, 512);
    fill(loud, 0.25f);
    const auto before = probe::counters().snapshot().analysisCount;
    auto waitIdle = [&p] {
        for (int i = 0; i < 2000 && p.session().running(); ++i)
            juce::Thread::sleep(5);
        return !p.session().running();
    };
    auto gain = [&p] { return p.session().view().steps[static_cast<std::size_t>(StepId::Gain)]; };

    p.setNonRealtime(true);
    p.processBlock(loud, midi);
    CHECK(p.session().startStep(StepId::Gain, p.offlineRendering()) == Start::Disabled);

    p.setNonRealtime(false);
    p.processBlock(loud, midi);
    REQUIRE(p.session().startStep(StepId::Gain, p.offlineRendering()) == Start::Ok);
    p.setNonRealtime(true);
    for (int i = 0; i < 50; ++i)
        p.processBlock(loud, midi);
    REQUIRE(waitIdle());
    CHECK(gain().invalidReason == miciri::core::RetestReason::OfflineRender);

    p.setNonRealtime(false);
    p.processBlock(loud, midi); // 处理器按最近一块判断宿主是否在离线渲染
    REQUIRE(p.session().startStep(StepId::Gain, p.offlineRendering()) == Start::Ok);
    for (int i = 0; i < 2000; ++i) // 约 21 秒的音频，几毫秒内送完
        p.processBlock(loud, midi);
    REQUIRE(waitIdle());
    CHECK(gain().status == miciri::core::StepStatus::Invalid);
    CHECK(gain().invalidReason == miciri::core::RetestReason::OfflineRender);
    CHECK(probe::counters().snapshot().analysisCount == before);
}

TEST_CASE("without the environment variable nothing is written", "[plugin][probe]")
{
    EnvCleanup cleanup;
    TempDir dir;
    {
        MiciriProcessor p;
        configure(p, 64);
        juce::AudioBuffer<float> b(2, 64);
        b.clear();
        juce::MidiBuffer midi;
        p.processBlock(b, midi);
        p.releaseResources();
    }
    CHECK(fs::is_empty(dir.path()));
}

TEST_CASE("with the environment variable the JSON is written on prepare, release and destruction", "[plugin][probe]")
{
    EnvCleanup cleanup;
    TempDir dir;
    const fs::path target = dir.path() / "probe.json";
    setEnv(target);
    juce::ScopedJuceInitialiser_GUI gui; // 定时器需要消息管理器

    juce::AudioBuffer<float> loud(2, 128);
    fill(loud, 0.25f);
    juce::MidiBuffer midi;

    {
        MiciriProcessor p;
        CHECK_FALSE(fs::exists(target)); // 构造时不写

        configure(p, 128);
        REQUIRE(fs::exists(target)); // prepareToPlay 写了一次
        const auto afterPrepare = parseFile(target);
        REQUIRE(afterPrepare.isObject());
        CHECK(intField(afterPrepare, "schema") == 1);
        CHECK(intField(afterPrepare, "maxBlockSize") == 128);
        CHECK(intField(afterPrepare, "numInputChannels") == 2);
        CHECK(intField(afterPrepare, "numOutputChannels") == 2);
        CHECK(static_cast<double>(afterPrepare.getProperty("sampleRate", 0)) == 48000.0);
        CHECK(intField(afterPrepare, "prepareCount") >= 1);

        p.setNonRealtime(true);
        const auto blocksBefore = probe::counters().snapshot().blocksNonRealtime;
        for (int i = 0; i < 4; ++i)
            p.processBlock(loud, midi);
        probe::noteAnalysis();
        p.releaseResources(); // releaseResources 写了一次
        const auto afterRelease = parseFile(target);
        REQUIRE(afterRelease.isObject());
        CHECK(static_cast<std::uint64_t>(intField(afterRelease, "blocksNonRealtime")) == blocksBefore + 4);
        CHECK(static_cast<std::uint64_t>(intField(afterRelease, "analysisCount")) ==
              probe::counters().snapshot().analysisCount);
        CHECK(static_cast<bool>(afterRelease.getProperty("lastIsNonRealtime", false)));

        p.processBlock(loud, midi); // 析构时的最后一次要带上这一块
    }

    const auto last = parseFile(target);
    REQUIRE(last.isObject());
    const auto s = probe::counters().snapshot();
    CHECK(static_cast<std::uint64_t>(intField(last, "blocksTotal")) == s.blocksTotal);
    CHECK(static_cast<std::uint64_t>(intField(last, "blocksNonRealtime")) == s.blocksNonRealtime);
    CHECK(static_cast<std::uint64_t>(intField(last, "blocksNonZero")) == s.blocksNonZero);
    CHECK(static_cast<std::uint64_t>(intField(last, "blocksAllZero")) == s.blocksAllZero);
    CHECK(static_cast<std::uint64_t>(intField(last, "analysisCount")) == s.analysisCount);
}

TEST_CASE("the timer on the message thread writes the JSON by itself", "[plugin][probe]")
{
    EnvCleanup cleanup;
    TempDir dir;
    const fs::path target = dir.path() / "timer.json";
    setEnv(target);
    juce::ScopedJuceInitialiser_GUI gui;

    MiciriProcessor p;
    REQUIRE_FALSE(fs::exists(target));
    juce::AudioBuffer<float> loud(2, 32);
    fill(loud, 0.1f);
    juce::MidiBuffer midi;
    for (int i = 0; i < 3; ++i)
        p.processBlock(loud, midi);

    // 不调 prepare、release，也不销毁：只靠定时器。
    juce::MessageManager::getInstance()->runDispatchLoopUntil(probe::kWriteIntervalMs * 4);
    REQUIRE(fs::exists(target));
    const auto v = parseFile(target);
    REQUIRE(v.isObject());
    CHECK(static_cast<std::uint64_t>(intField(v, "blocksTotal")) == probe::counters().snapshot().blocksTotal);
}
