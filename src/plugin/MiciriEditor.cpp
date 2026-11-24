// SPDX-License-Identifier: GPL-3.0-or-later
//
// 编辑器的实现。快照与事件的字段名只用 BridgeApi.h 里已有的；枚举值取自 core 的线上名字表。
// 快照由 BridgeSnapshot.h 拼装（Session 的 JSON 加处理器一侧的字段，测试构建的宿主探针用同一份），
// 这里只补上界面字段与墙钟一侧的回调年龄。
#include "MiciriEditor.h"

#include "BridgeBase.h"
#include "BridgeSnapshot.h"
#include "UiDefaultsStore.h"
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

juce::var makeObject(std::initializer_list<std::pair<const char*, juce::var>> fields)
{
    auto* obj = new juce::DynamicObject();
    for (const auto& f : fields)
        obj->setProperty(f.first, f.second);
    return juce::var(obj);
}

constexpr const char* kBadStep = "bad-step";
constexpr const char* kBusy = "busy";
constexpr const char* kDisabled = "disabled";
constexpr const char* kNoEvidence = "no-evidence";
static_assert(bridge::listed(bridge::kReasons, kBadStep) && bridge::listed(bridge::kReasons, kBusy) &&
                  bridge::listed(bridge::kReasons, kDisabled) && bridge::listed(bridge::kReasons, kNoEvidence),
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

#ifdef MICIRI_HOST_PROBE
// 测试构建：界面启动的三种事件记进宿主探针，宿主测试据此判断页面有没有完成启动、桥通没通。
void MiciriEditor::onUiEvent(UiEvent event)
{
    switch (event)
    {
    case UiEvent::uiReady:
        probe::counters().noteUiReady();
        break;
    case UiEvent::fallbackShown:
        probe::counters().noteUiFallback();
        break;
    case UiEvent::bridgeRoundTrip:
        probe::counters().noteBridgeRoundTrip();
        break;
    }
}
#endif

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
    // 页面刚就绪时定时器还没来得及刷新，所以这里先读一次心跳，首份快照就不会带旧的回调年龄。
    const auto ageSec = observeCallbackAgeSec();
    const SnapshotUi ui{lang(), bridge::scaleForJson(uiScale()), guideSeen_};
    return buildBridgeSnapshot(processor_, hostName_, &ui, ageSec);
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
    // 证据波形：只读 Session 存好的那一份，不改状态、不推快照；包络是派生数据，不写日志
    if (name == "getEvidence")
    {
        const int index = wireIndex(args, core::kStepIdNames);
        if (index < 0)
        {
            complete(bridge::failResponse(kBadStep));
            return;
        }
        const auto pack = processor_.session().evidence(static_cast<core::StepId>(index));
        if (pack == nullptr)
        {
            complete(bridge::failResponse(kNoEvidence));
            return;
        }
        const auto json = session::evidenceJson(*pack);
        complete(bridge::okResponseWith(
            "evidence", juce::JSON::parse(juce::String::fromUTF8(json.data(), static_cast<int>(json.size())))));
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
