// SPDX-License-Identifier: GPL-3.0-or-later
//
// 编辑器的实现。快照与事件的字段名只用 BridgeApi.h 里已有的；枚举值取自 core 的线上名字表。
#include "MiciriEditor.h"

#include <cmath>

#include "BridgeBase.h"
#include "UiDefaultsStore.h"
#include "config/Thresholds.h"

namespace miciri
{
namespace
{
double wallClockMs()
{
    return juce::Time::getMillisecondCounterHiRes();
}

// 秒数保留一位小数，快照里不带多余的位数。
double roundTenth(double v)
{
    return std::round(v * 10.0) / 10.0;
}

juce::var makeObject(std::initializer_list<std::pair<const char*, juce::var>> fields)
{
    auto* obj = new juce::DynamicObject();
    for (const auto& f : fields)
        obj->setProperty(f.first, f.second);
    return juce::var(obj);
}

juce::String wireName(std::string_view v)
{
    return juce::String(v.data(), v.size());
}
} // namespace

MiciriEditor::MiciriEditor(MiciriProcessor& processor, Config config)
    : webview::WebViewHost(processor, std::move(config)), processor_(processor),
      hostName_(juce::PluginHostType().getHostDescription()), guideSeen_(uidefaults::guideSeenGlobal()),
      lastBeat_(processor.heartbeat()), lastBeatChangeMs_(wallClockMs()), lastDevice_(processor.deviceInfo())
{
}

MiciriEditor::~MiciriEditor() = default;

double MiciriEditor::observeCallbackAgeSec()
{
    const auto now = wallClockMs();
    const auto beat = processor_.heartbeat();
    if (beat != lastBeat_)
    {
        lastBeat_ = beat;
        lastBeatChangeMs_ = now;
    }
    lastAgeSec_ = (now - lastBeatChangeMs_) / 1000.0;
    return lastAgeSec_;
}

juce::var MiciriEditor::buildSnapshot()
{
    const auto device = processor_.deviceInfo();
    // 页面刚就绪时定时器还没来得及刷新，所以这里先读一次心跳，首份快照就不会带旧的回调年龄。
    const auto ageSec = observeCallbackAgeSec();
    const bool noCallback = ageSec > thr::no_signal::no_callback_s;

    const auto layoutIndex = device.numChannels >= 2 ? core::ChannelLayout::Stereo : core::ChannelLayout::Mono;
    const auto signalState = noCallback ? core::SignalState::NoCallback : core::SignalState::Waiting;

    const auto deviceObj = makeObject({{"sampleRate", device.sampleRate},
                                       {"bufferSize", device.bufferSize},
                                       {"layout", wireName(core::toString(layoutIndex))},
                                       {"hostName", hostName_}});
    const auto connectObj =
        makeObject({{"signalState", wireName(core::toString(signalState))}, {"callbackAgeSec", roundTenth(ageSec)}});

    return makeObject({{"apiVersion", bridge::kApiVersion},
                       {"lang", lang()},
                       {"uiScale", bridge::scaleForJson(uiScale())},
                       {"guideSeen", guideSeen_},
                       {"micType", wireName(core::kMicTypeNames[static_cast<std::size_t>(processor_.micType())])},
                       {"device", deviceObj},
                       {"connect", connectObj},
                       {"steps", makeObject({})},
                       {"results", makeObject({})}});
}

juce::var MiciriEditor::buildMeters()
{
    const auto m = processor_.readMeters();
    return makeObject({{"levelDbfsL", m.levelDbfs[0]},
                       {"levelDbfsR", m.levelDbfs[1]},
                       {"peakDbfsL", m.peakDbfs[0]},
                       {"peakDbfsR", m.peakDbfs[1]},
                       {"clipLatchL", m.clipLatch[0]},
                       {"clipLatchR", m.clipLatch[1]}});
}

// 每个节拍：更新回调年龄、推电平；设备信息变了立刻推状态，否则每隔 kStateEveryTicks 个节拍推一次。
void MiciriEditor::emitTick()
{
    observeCallbackAgeSec();
    emitEvent(bridge::kMetersEvent, buildMeters());

    const auto device = processor_.deviceInfo();
    const bool deviceChanged = device.sampleRate != lastDevice_.sampleRate ||
                               device.bufferSize != lastDevice_.bufferSize ||
                               device.numChannels != lastDevice_.numChannels;
    if (deviceChanged || ++ticksSinceState_ >= kStateEveryTicks)
    {
        lastDevice_ = device;
        ticksSinceState_ = 0;
        emitState();
    }
}

// 只实现不依赖 Session 的函数；步骤类函数（startStep、cancelStep、reportChange、dismissChannelPrompt、
// showLicense）要等对应模块落地，之前统一回 not-ready，不改任何状态。
void MiciriEditor::handleBridgeCall(const juce::String& name, const juce::Array<juce::var>& args,
                                    NativeCompletion complete)
{
    if (name == "setGuideSeen")
    {
        bool seen = false;
        if (args.size() < 1 || !bridge::strictBool(args[0], seen))
        {
            complete(bridge::failResponse(bridge::kBadArg));
            return;
        }
        guideSeen_ = seen;
        uidefaults::setGuideSeenGlobal(seen);
        complete(bridge::okResponse());
        emitState();
        return;
    }
    if (name == "setMicType")
    {
        int index = -1;
        if (args.size() >= 1 && args[0].isString())
        {
            const auto wanted = args[0].toString().toStdString();
            for (std::size_t i = 0; i < core::kMicTypeNames.size(); ++i)
                if (core::kMicTypeNames[i] == wanted)
                    index = static_cast<int>(i);
        }
        if (!processor_.setMicType(index))
        {
            complete(bridge::failResponse(bridge::kBadArg));
            return;
        }
        complete(bridge::okResponse());
        emitState();
        return;
    }
    if (name == "clearClipLatch")
    {
        processor_.clearClipLatch();
        complete(bridge::okResponse());
        return;
    }
    if (name == "openExternal")
    {
        const auto link = args.size() >= 1 && args[0].isString() ? args[0].toString() : juce::String();
        const juce::URL url(link);
        if (!link.startsWith("https://") || link.length() > kExternalUrlMaxChars || !url.isWellFormed())
        {
            complete(bridge::failResponse(bridge::kBadArg));
            return;
        }
        url.launchInDefaultBrowser();
        complete(bridge::okResponse());
        return;
    }
    webview::WebViewHost::handleBridgeCall(name, args, std::move(complete));
}
} // namespace miciri
