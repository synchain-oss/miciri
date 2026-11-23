// SPDX-License-Identifier: GPL-3.0-or-later
//
// 桥快照的拼装（见 BridgeSnapshot.h）。快照的字段名只用 BridgeApi.h 里已有的；枚举值取自 core 的线上名字表。
#include "BridgeSnapshot.h"

#include <cmath>
#include <initializer_list>
#include <string_view>
#include <utility>

#include "BridgeApi.h"
#include "MiciriProcessor.h"
#include "config/Thresholds.h"

namespace miciri
{
namespace
{
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

juce::var buildBridgeSnapshot(MiciriProcessor& processor, const juce::String& hostName, const SnapshotUi* ui,
                              std::optional<double> callbackAgeSec)
{
    const auto device = processor.deviceInfo();
    const bool noCallback = callbackAgeSec && *callbackAgeSec > thr::no_signal::no_callback_s;

    const auto layoutIndex = device.numChannels >= 2 ? core::ChannelLayout::Stereo : core::ChannelLayout::Mono;
    const auto view = processor.session().view();
    const auto json = session::snapshotJson(view, static_cast<core::MicType>(processor.micType()));
    const auto parsed = juce::JSON::parse(juce::String::fromUTF8(json.data(), static_cast<int>(json.size())));
    // 推荐距离在耳机麦时是 null，原样转发，不用 part（它把非对象换成空对象）
    auto part = [&parsed](const char* key) {
        const auto v = parsed.getProperty(key, {});
        return v.isObject() ? v : makeObject({});
    };

    const auto deviceObj = makeObject({{"sampleRate", device.sampleRate},
                                       {"bufferSize", device.bufferSize},
                                       {"layout", wireName(core::toString(layoutIndex))},
                                       {"hostName", hostName},
                                       {"offlineRender", processor.offlineRendering() || view.offlineRender}});
    // 接通诊断来自 Session；“没有回调”只有墙钟看得出，盖过 Session 给的状态
    const auto connectObj = part("connect");
    if (auto* c = connectObj.getDynamicObject(); c != nullptr && callbackAgeSec)
    {
        c->setProperty("callbackAgeSec", roundTenth(*callbackAgeSec)); // 覆盖 Session 的占位，键序不变
        if (noCallback)
            c->setProperty("signalState", wireName(core::toString(core::SignalState::NoCallback)));
    }

    // “没有回调”也是无信号：固定在“最该改”的最前
    // fixes 是数组，不能走 part()（它只认对象，别的都换成空对象）
    auto fixes = parsed.getProperty("fixes", {});
    if (!fixes.isArray())
        fixes = juce::var(juce::Array<juce::var>());
    if (auto* list = fixes.getArray(); list != nullptr && noCallback)
    {
        const auto noSignal = wireName(core::toString(core::FixItem::NoSignal));
        if (!list->contains(noSignal))
            list->insert(0, noSignal);
    }

    auto* root = new juce::DynamicObject();
    root->setProperty("apiVersion", bridge::kApiVersion);
    if (ui != nullptr)
    {
        root->setProperty("lang", ui->lang);
        root->setProperty("uiScale", ui->uiScale);
        root->setProperty("guideSeen", ui->guideSeen);
    }
    root->setProperty("micType", wireName(core::kMicTypeNames[static_cast<std::size_t>(processor.micType())]));
    root->setProperty("device", deviceObj);
    root->setProperty("connect", connectObj);
    root->setProperty("steps", part("steps"));
    root->setProperty("results", part("results"));
    root->setProperty("bands", part("bands"));
    root->setProperty("live", part("live"));
    root->setProperty("fixes", fixes);
    root->setProperty("advice", part("advice"));
    root->setProperty("previous", part("previous"));
    root->setProperty("recommendedDistance", parsed.getProperty("recommendedDistance", {}));
    return juce::var(root);
}
} // namespace miciri
