// SPDX-License-Identifier: GPL-3.0-or-later
//
// 处理器的实现。直通的做法是不碰缓冲：宿主给的是原地缓冲，输入已经在里面，原样返回就是逐位相同。
#include "MiciriProcessor.h"

#include <algorithm>

#include "BridgeApi.h"
#include "config/Thresholds.h"
#ifdef MICIRI_HOST_PROBE
#include "HostProbe.h"
#endif

namespace miciri
{
#ifdef MICIRI_HOST_PROBE
// 测试构建专用：环境变量给了路径才存在。消息线程上每隔一个节拍写一次（内容没变不写），销毁时再写最后一次。
class ProbeRunner final : private juce::Timer
{
public:
    static std::unique_ptr<ProbeRunner> create()
    {
        auto reporter = probe::Reporter::fromEnvironment(probe::counters());
        if (reporter == nullptr)
            return nullptr;
        return std::unique_ptr<ProbeRunner>(new ProbeRunner(std::move(reporter)));
    }

    ~ProbeRunner() override
    {
        stopTimer();
        reporter_->flush();
    }

    void flush() noexcept { reporter_->flush(); }

private:
    explicit ProbeRunner(std::unique_ptr<probe::Reporter> reporter) : reporter_(std::move(reporter))
    {
        startTimer(probe::kWriteIntervalMs);
    }

    void timerCallback() override { reporter_->tick(); }

    std::unique_ptr<probe::Reporter> reporter_;
};
#endif

MiciriProcessor::MiciriProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
#ifdef MICIRI_HOST_PROBE
    (void)probe::counters(); // 先把计数器建好，音频线程不会是第一个碰它的
    probe_ = ProbeRunner::create();
#endif
}

MiciriProcessor::~MiciriProcessor() = default;

void MiciriProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sampleRate_.store(sampleRate, std::memory_order_relaxed);
    bufferSize_.store(samplesPerBlock, std::memory_order_relaxed);
    numChannels_.store(std::max(1, getMainBusNumInputChannels()), std::memory_order_relaxed);
#ifdef MICIRI_HOST_PROBE
    probe::counters().notePrepare(sampleRate, samplesPerBlock, getMainBusNumInputChannels(),
                                  getMainBusNumOutputChannels());
    if (probe_ != nullptr)
        probe_->flush();
#endif
}

void MiciriProcessor::releaseResources()
{
#ifdef MICIRI_HOST_PROBE
    if (probe_ != nullptr)
        probe_->flush();
#endif
}

// 只接受输入输出相同的单声道或立体声；禁用总线、其他声道数一律拒绝。
bool MiciriProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

// 音频线程：不加锁、不分配、不读墙钟、不抛异常；只看样本，不写样本。
// 0 样本的调用立即返回，不推进心跳也不动电平：没有样本就不是一次有内容的回调。
// ScopedNoDenormals 只改本线程的浮点状态，让电平累计不被非规格化数拖慢；它不碰缓冲，直通仍然逐位相同。
template<class Sample>
void MiciriProcessor::observe(juce::AudioBuffer<Sample>& buffer) noexcept
{
    const int samples = buffer.getNumSamples();
    if (samples <= 0)
        return;
    const juce::ScopedNoDenormals noDenormals;
    heartbeat_.fetch_add(1, std::memory_order_relaxed);
#ifdef MICIRI_HOST_PROBE
    probe::counters().noteBlock(isNonRealtime(),
                                probe::isAllZero(buffer.getArrayOfReadPointers(), buffer.getNumChannels(), samples));
#endif
    const int channels = std::min(buffer.getNumChannels(), kMeterChannels);
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
