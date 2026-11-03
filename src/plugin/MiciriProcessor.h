// SPDX-License-Identifier: GPL-3.0-or-later
//
// 处理器：逐位直通、零延迟、没有参数，只声明单声道与立体声。
// 音频线程只做直通（不碰样本）、心跳计数、原子电平与削波锁存；其余一概不做。
// Session 与工作线程由后续的模块挂在这里，所有权归处理器；编辑器只读，可以随时销毁重建。
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <cstdint>

#include "Meters.h"

namespace miciri
{
class MiciriProcessor final : public juce::AudioProcessor
{
public:
    MiciriProcessor();
    ~MiciriProcessor() override;

    const juce::String getName() const override { return "Miciri"; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    bool supportsDoublePrecisionProcessing() const override { return true; }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    void processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midi) override;
    // 宿主的旁路也走同一条路径：数据同样不动，心跳与电平不断。
    void processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    void processBlockBypassed(juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midi) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    // 状态格式尚未定义：不写任何内容，也忽略传入的内容。界面的缩放与语言存在全局默认值里，不进工程。
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    // 以下给消息线程用。
    struct DeviceInfo
    {
        double sampleRate = 0.0;
        int bufferSize = 0;
        int numChannels = 2;
    };
    DeviceInfo deviceInfo() const;

    // 回调计数：每个 processBlock 加一。编辑器的定时器按墙钟观察它有没有在走。
    std::uint64_t heartbeat() const noexcept { return heartbeat_.load(std::memory_order_relaxed); }

    // 读电平：峰值读后清零，均方是最近一块，削波锁存保持。
    plugin::MeterReadout readMeters();
    void clearClipLatch();

    // 话筒类型：枚举下标，对应桥契约的 ENUM_MIC_TYPE。越界返回 false 且不改。
    int micType() const noexcept { return micType_.load(std::memory_order_relaxed); }
    bool setMicType(int index);

private:
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free, "心跳计数必须无锁");
    static_assert(std::atomic<double>::is_always_lock_free, "采样率必须无锁");
    static_assert(std::atomic<int>::is_always_lock_free, "缓冲大小、声道数与话筒类型必须无锁");

    template<class Sample>
    void observe(juce::AudioBuffer<Sample>& buffer) noexcept;

    static constexpr int kMeterChannels = 2;

    plugin::ChannelMeter meters_[kMeterChannels];
    std::atomic<std::uint64_t> heartbeat_{0};
    std::atomic<double> sampleRate_{0.0};
    std::atomic<int> bufferSize_{0};
    std::atomic<int> numChannels_{2};
    std::atomic<int> micType_{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MiciriProcessor)
};
} // namespace miciri
