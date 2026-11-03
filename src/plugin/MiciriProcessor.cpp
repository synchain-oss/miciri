// SPDX-License-Identifier: GPL-3.0-or-later
//
// 处理器的实现。直通的做法是不碰缓冲：宿主给的是原地缓冲，输入已经在里面，原样返回就是逐位相同。
#include "MiciriProcessor.h"

#include <algorithm>

#include "BridgeApi.h"
#include "config/Thresholds.h"

namespace miciri
{
MiciriProcessor::MiciriProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

MiciriProcessor::~MiciriProcessor() = default;

void MiciriProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sampleRate_.store(sampleRate, std::memory_order_relaxed);
    bufferSize_.store(samplesPerBlock, std::memory_order_relaxed);
    numChannels_.store(std::max(1, getMainBusNumInputChannels()), std::memory_order_relaxed);
}

// 只接受输入输出相同的单声道或立体声；禁用总线、其他声道数一律拒绝。
bool MiciriProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

// 音频线程：不加锁、不分配、不读墙钟、不抛异常；只看样本，不写样本。
template<class Sample>
void MiciriProcessor::observe(juce::AudioBuffer<Sample>& buffer) noexcept
{
    heartbeat_.fetch_add(1, std::memory_order_relaxed);
    const int channels = std::min(buffer.getNumChannels(), kMeterChannels);
    const int samples = buffer.getNumSamples();
    for (int ch = 0; ch < channels; ++ch)
        meters_[ch].accumulate(buffer.getReadPointer(ch), samples, thr::gain::clip_digital_threshold);
}

void MiciriProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    observe(buffer);
}

void MiciriProcessor::processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer&)
{
    observe(buffer);
}

void MiciriProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    processBlock(buffer, midi);
}

void MiciriProcessor::processBlockBypassed(juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midi)
{
    processBlock(buffer, midi);
}

MiciriProcessor::DeviceInfo MiciriProcessor::deviceInfo() const
{
    return {sampleRate_.load(std::memory_order_relaxed), bufferSize_.load(std::memory_order_relaxed),
            numChannels_.load(std::memory_order_relaxed)};
}

plugin::MeterReadout MiciriProcessor::readMeters()
{
    plugin::MeterReadout out;
    for (int ch = 0; ch < kMeterChannels; ++ch)
    {
        auto& m = meters_[ch];
        out.peakDbfs[ch] = plugin::toDbfs(m.peak.exchange(0.0, std::memory_order_relaxed));
        out.levelDbfs[ch] = plugin::toDbfs(std::sqrt(m.meanSquare.load(std::memory_order_relaxed)));
        out.clipLatch[ch] = m.clipLatch.load(std::memory_order_relaxed);
    }
    return out;
}

void MiciriProcessor::clearClipLatch()
{
    for (auto& m : meters_)
        m.clipLatch.store(false, std::memory_order_relaxed);
}

bool MiciriProcessor::setMicType(int index)
{
    if (index < 0 || index >= static_cast<int>(miciri::core::kMicTypeNames.size()))
        return false;
    micType_.store(index, std::memory_order_relaxed);
    return true;
}
} // namespace miciri
