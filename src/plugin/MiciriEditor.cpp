// SPDX-License-Identifier: GPL-3.0-or-later
//
// 编辑器的实现。快照与事件的字段名只用 BridgeApi.h 里已有的；枚举值取自 core 的线上名字表。
// 步骤、结果、档位边界、实时信息与接通诊断由 Session 写成 JSON（字段与顺序按契约，见 session/Session.h），
// 这里解析后并进快照，只补上墙钟一侧的 callbackAgeSec 与宿主报的离线渲染。
#include "MiciriEditor.h"

#include <cmath>

#include "BridgeBase.h"
#include "UiDefaultsStore.h"
#include "config/Thresholds.h"
#ifdef MICIRI_HOST_PROBE
#include "HostProbe.h"
#endif

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

constexpr const char* kBadStep = "bad-step";
constexpr const char* kBusy = "busy";
constexpr const char* kDisabled = "disabled";
static_assert(bridge::listed(bridge::kReasons, kBadStep) && bridge::listed(bridge::kReasons, kBusy) &&
                  bridge::listed(bridge::kReasons, kDisabled),
              "错误原因必须出现在 BridgeApi.h 的 kReasons 里");

// 参数是某个枚举的线上名字：返回它的下标，不认识返回 -1
template<std::size_t N>
int wireIndex(const juce::Array<juce::var>& args, const std::array<std::string_view, N>& names)
{
    if (args.size() < 1 || !args[0].isString())
        return -1;
    const auto wanted = args[0].toString().toStdString();
    for (std::size_t i = 0; i < N; ++i)
        if (names[i] == wanted)
            return static_cast<int>(i);
    return -1;
}
} // namespace

MiciriEditor::MiciriEditor(MiciriProcessor& processor, Config config)
    : webview::WebViewHost(processor, std::move(config)), processor_(processor),
      hostName_(juce::PluginHostType().getHostDescription()), guideSeen_(uidefaults::guideSeenGlobal()),
      lastBeat_(processor.heartbeat()), lastBeatChangeMs_(wallClockMs()), lastDevice_(processor.deviceInfo())
{
#ifdef MICIRI_HOST_PROBE
    probe::counters().noteEditorOpened();
#endif
}

MiciriEditor::~MiciriEditor()
{
#ifdef MICIRI_HOST_PROBE
    probe::counters().noteEditorClosed();
#endif
}

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
    const auto view = processor_.session().view();
    const auto json = session::snapshotJson(view, static_cast<core::MicType>(processor_.micType()));
    const auto parsed = juce::JSON::parse(juce::String::fromUTF8(json.data(), static_cast<int>(json.size())));
    auto part = [&parsed](const char* key) {
        const auto v = parsed.getProperty(key, {});
        return v.isObject() ? v : makeObject({});
    };

    const auto deviceObj = makeObject({{"sampleRate", device.sampleRate},
                                       {"bufferSize", device.bufferSize},
                                       {"layout", wireName(core::toString(layoutIndex))},
                                       {"hostName", hostName_},
                                       {"offlineRender", processor_.offlineRendering() || view.offlineRender}});
    // 接通诊断来自 Session；“没有回调”只有墙钟看得出，盖过 Session 给的状态
    const auto connectObj = part("connect");
    if (auto* c = connectObj.getDynamicObject())
    {
        c->setProperty("callbackAgeSec", roundTenth(ageSec)); // 覆盖 Session 的占位，键序不变
        if (noCallback)
            c->setProperty("signalState", wireName(core::toString(core::SignalState::NoCallback)));
    }

    // “没有回调”也是无信号：固定在“最该改”的最前
    const auto fixes = part("fixes");
    if (auto* list = fixes.getArray(); list != nullptr && noCallback)
    {
        const auto noSignal = wireName(core::toString(core::FixItem::NoSignal));
        if (!list->contains(noSignal))
            list->insert(0, noSignal);
    }

    return makeObject({{"apiVersion", bridge::kApiVersion},
                       {"lang", lang()},
                       {"uiScale", bridge::scaleForJson(uiScale())},
                       {"guideSeen", guideSeen_},
                       {"micType", wireName(core::kMicTypeNames[static_cast<std::size_t>(processor_.micType())])},
                       {"device", deviceObj},
                       {"connect", connectObj},
                       {"steps", part("steps")},
                       {"results", part("results")},
                       {"bands", part("bands")},
                       {"live", part("live")},
                       {"fixes", fixes},
                       {"advice", part("advice")},
                       {"previous", part("previous")}});
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
    const auto version = processor_.session().version();
    ++ticksSinceState_;
    const bool sessionChanged = version != lastSessionVersion_ && ticksSinceState_ >= kSessionStateTicks;
    if (deviceChanged || sessionChanged || ticksSinceState_ >= kStateEveryTicks)
    {
        lastDevice_ = device;
        lastSessionVersion_ = version;
        ticksSinceState_ = 0;
        emitState();
    }
}

// showLicense 要等对应模块落地，之前回 not-ready，不改任何状态。
// cancelStep 不等工作线程：只让它停下、撤销采集，立即返回；它还在收尾分析时，startStep 回 busy，界面稍后重试。
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
        if (!processor_.setMicType(wireIndex(args, core::kMicTypeNames)))
        {
            complete(bridge::failResponse(bridge::kBadArg));
            return;
        }
        complete(bridge::okResponse());
        emitState();
        return;
    }
    if (name == "startStep")
    {
        const int index = wireIndex(args, core::kStepIdNames);
        if (index < 0)
        {
            complete(bridge::failResponse(kBadStep));
            return;
        }
        const auto outcome =
            processor_.session().startStep(static_cast<core::StepId>(index), processor_.offlineRendering());
        if (outcome == session::Session::Start::Busy)
            complete(bridge::failResponse(kBusy));
        else if (outcome == session::Session::Start::Disabled || outcome == session::Session::Start::Failed)
            complete(bridge::failResponse(kDisabled));
        else
            complete(bridge::okResponse());
        emitState();
        return;
    }
    if (name == "cancelStep")
    {
        processor_.session().cancelStep();
        complete(bridge::okResponse());
        emitState();
        return;
    }
    if (name == "reportChange")
    {
        const int index = wireIndex(args, core::kChangeKindNames);
        if (index < 0)
        {
            complete(bridge::failResponse(bridge::kBadArg));
            return;
        }
        processor_.session().reportChange(static_cast<core::ChangeKind>(index));
        complete(bridge::okResponse());
        emitState();
        return;
    }
    if (name == "dismissChannelPrompt")
    {
        processor_.session().dismissChannelPrompt();
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
