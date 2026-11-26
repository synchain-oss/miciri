// SPDX-License-Identifier: GPL-3.0-or-later
//
// 处理器的实现。直通的做法是不碰缓冲：宿主给的是原地缓冲，输入已经在里面，原样返回就是逐位相同。
#include "MiciriProcessor.h"

#include <algorithm>

#include "BridgeApi.h"
#include "HostProbe.h"
#include "config/Thresholds.h"
#ifdef MICIRI_HOST_PROBE
#include "HostProbeRunner.h"
#endif

namespace miciri
{

MiciriProcessor::MiciriProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      session_(&probe::noteAnalysis) // 发布构建里是空函数；宿主测试构建里每次分析给探针计一次数
{
    // 验证宿主（pluginval）下不开始测试。FL Studio 与 REAPER 的轨道可能没有单声道选项，声道提示换一种说法。
    const juce::PluginHostType host;
    session_.setHostTraits({host.isPluginval(), host.isFruityLoops() || host.isReaper()});
#ifdef MICIRI_HOST_PROBE
    (void)probe::counters(); // 先把计数器建好，音频线程不会是第一个碰它的
    probe_ = ProbeRunner::create(*this, probeSamples_);
#endif
}

MiciriProcessor::~MiciriProcessor() = default;

void MiciriProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sampleRate_.store(sampleRate, std::memory_order_relaxed);
    bufferSize_.store(samplesPerBlock, std::memory_order_relaxed);
    numChannels_.store(std::max(1, getMainBusNumInputChannels()), std::memory_order_relaxed);
    // 采样率变了：进行中的一步作废、旧结果标过期；声道数变了：按过期规则处理（见 Session.h）
    session_.prepare(sampleRate, std::max(1, getMainBusNumInputChannels()));
    // 电平表的真峰值与增益步同一个插值倍数（采样率不同倍数不同），读数才对得上
    const int oversample =
        static_cast<int>(sampleRate <= thr::gain::true_peak_rate_split_hz ? thr::gain::true_peak_oversample_le_96k
                                                                          : thr::gain::true_peak_oversample_gt_96k);
    for (auto& m : meters_)
        m.prepare(oversample);
#ifdef MICIRI_HOST_PROBE
    probe::counters().notePrepare(sampleRate, samplesPerBlock, getMainBusNumInputChannels(),
                                  getMainBusNumOutputChannels());
    if (probe_ != nullptr)
        probe_->prepared();
#endif
}

void MiciriProcessor::releaseResources()
{
#ifdef MICIRI_HOST_PROBE
    if (probe_ != nullptr)
        probe_->released();
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
// 测试进行中才把样本推进采集环（不在测试时 tap 只做一次原子读）；宿主报离线渲染时 tap 不收样本。
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
    const bool offline = isNonRealtime();
    nonRealtime_.store(offline, std::memory_order_relaxed);
#ifdef MICIRI_HOST_PROBE
    probe::counters().noteBlock(offline,
                                probe::isAllZero(buffer.getArrayOfReadPointers(), buffer.getNumChannels(), samples));
    probeSamples_.fetch_add(static_cast<std::uint64_t>(samples), std::memory_order_relaxed);
#endif
    session_.tap().process(buffer.getArrayOfReadPointers(), buffer.getNumChannels(), samples, offline);
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
    return readMeters(juce::Time::getMillisecondCounter());
}

plugin::MeterReadout MiciriProcessor::readMeters(std::uint32_t nowMs)
{
    plugin::MeterReadout out;
    for (int ch = 0; ch < kMeterChannels; ++ch)
    {
        double peak = 0.0;
        double meanSquare = 0.0;
        meters_[ch].take(peak, meanSquare);
        out.peakDbfs[ch] = plugin::toDbfs(peak);
        out.levelDbfs[ch] = plugin::toDbfs(std::sqrt(meanSquare));
        out.clipLatch[ch] = lamps_[ch].update(meters_[ch].clipBlocks.load(std::memory_order_relaxed), nowMs);
    }
    return out;
}

void MiciriProcessor::resetMeters()
{
    for (auto& m : meters_)
    {
        double peak = 0.0;
        double meanSquare = 0.0;
        m.take(peak, meanSquare);
    }
    clearClipLatch();
}

void MiciriProcessor::clearClipLatch()
{
    for (int ch = 0; ch < kMeterChannels; ++ch)
        lamps_[ch].clear(meters_[ch].clipBlocks.load(std::memory_order_relaxed));
}

bool MiciriProcessor::setMicType(int index)
{
    if (index < 0 || index >= static_cast<int>(miciri::core::kMicTypeNames.size()))
        return false;
    micType_.store(index, std::memory_order_relaxed);
    return true;
}
} // namespace miciri
