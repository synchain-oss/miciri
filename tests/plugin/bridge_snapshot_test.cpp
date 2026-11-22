// SPDX-License-Identifier: GPL-3.0-or-later
//
// 桥快照的拼装（BridgeSnapshot.h）：编辑器与测试构建的宿主探针共用这一个函数。
//   - 带界面字段与回调年龄时，输出与抽成共用函数之前编辑器自己拼的那份逐字节相同。下面的 referenceSnapshot
//     是抽取前 MiciriEditor::buildSnapshot 的原样拷贝（只把成员换成参数），用来锁住这一点；以后要改快照，
//     先改契约与这里的参照。
//   - 键名与键序按 BridgeApi.h 的契约表；不带界面字段时恰好少 lang、uiScale、guideSeen 三个，其余逐项相同。
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "config/Thresholds.h"
#include "plugin/BridgeApi.h"
#include "plugin/BridgeSnapshot.h"
#include "plugin/MiciriProcessor.h"

namespace
{
using miciri::MiciriProcessor;
namespace core = miciri::core;
namespace session = miciri::session;
namespace bridge = miciri::bridge;
namespace thr = miciri::thr;

// ---- 参照：抽取前编辑器的拼装，逐行照搬 ----
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

juce::var referenceSnapshot(MiciriProcessor& processor_, const juce::String& hostName_, const juce::String& lang,
                            double uiScaleForJson, bool guideSeen_, double ageSec)
{
    const auto device = processor_.deviceInfo();
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
    const auto connectObj = part("connect");
    if (auto* c = connectObj.getDynamicObject())
    {
        c->setProperty("callbackAgeSec", roundTenth(ageSec));
        if (noCallback)
            c->setProperty("signalState", wireName(core::toString(core::SignalState::NoCallback)));
    }

    auto fixes = parsed.getProperty("fixes", {});
    if (!fixes.isArray())
        fixes = juce::var(juce::Array<juce::var>());
    if (auto* list = fixes.getArray(); list != nullptr && noCallback)
    {
        const auto noSignal = wireName(core::toString(core::FixItem::NoSignal));
        if (!list->contains(noSignal))
            list->insert(0, noSignal);
    }

    return makeObject({{"apiVersion", bridge::kApiVersion},
                       {"lang", lang},
                       {"uiScale", uiScaleForJson},
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
                       {"previous", part("previous")},
                       {"recommendedDistance", parsed.getProperty("recommendedDistance", {})}});
}
// ---- 参照到此 ----

void configure(MiciriProcessor& p, const juce::AudioChannelSet& set)
{
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add(set);
    layout.outputBuses.add(set);
    REQUIRE(p.setBusesLayout(layout));
    p.prepareToPlay(44100.0, 256);
}

std::string text(const juce::var& v)
{
    return juce::JSON::toString(v).toStdString();
}

template<std::size_t N>
std::vector<std::string> names(const std::array<std::string_view, N>& table)
{
    std::vector<std::string> out;
    for (const auto& n : table)
        out.emplace_back(n);
    return out;
}

std::vector<std::string> keys(const juce::var& obj)
{
    std::vector<std::string> out;
    if (const auto* o = obj.getDynamicObject())
        for (const auto& p : o->getProperties())
            out.push_back(p.name.toString().toStdString());
    return out;
}

bool isUiField(const std::string& key)
{
    return key == "lang" || key == "uiScale" || key == "guideSeen";
}
} // namespace

TEST_CASE("the shared snapshot is byte-identical to what the editor built before the extraction", "[plugin][bridge]")
{
    const juce::String host = "Test Host";
    const miciri::SnapshotUi ui{"zh", 1.25, true};
    auto same = [&](MiciriProcessor& p, double ageSec) {
        INFO("ageSec " << ageSec << ", micType " << p.micType());
        CHECK(text(miciri::buildBridgeSnapshot(p, host, &ui, ageSec)) ==
              text(referenceSnapshot(p, host, ui.lang, ui.uiScale, ui.guideSeen, ageSec)));
    };

    MiciriProcessor stereo;
    configure(stereo, juce::AudioChannelSet::stereo());
    same(stereo, 0.04); // 回调正常
    same(stereo, thr::no_signal::no_callback_s + 1.0); // 没有回调：接通诊断与“最该改”都被盖过
    for (int mic = 0; mic < static_cast<int>(core::kMicTypeNames.size()); ++mic)
    {
        REQUIRE(stereo.setMicType(mic)); // 耳机麦时推荐距离是 null
        same(stereo, 0.3);
    }
    stereo.session().reportChange(core::ChangeKind::Room);
    stereo.session().dismissChannelPrompt();
    same(stereo, 12.34);

    MiciriProcessor mono;
    configure(mono, juce::AudioChannelSet::mono());
    same(mono, 0.0);
}

TEST_CASE("snapshot keys follow the contract, and without the UI only the three UI fields are missing",
          "[plugin][bridge]")
{
    MiciriProcessor p;
    configure(p, juce::AudioChannelSet::stereo());
    REQUIRE(p.setMicType(2));
    const miciri::SnapshotUi ui{"en", 1.0, false};

    const auto editor = miciri::buildBridgeSnapshot(p, "Host", &ui, 0.5);
    CHECK(keys(editor) == names(bridge::kSnapshotRoot));
    CHECK(keys(editor.getProperty("device", {})) == names(bridge::kSnapshotDevice));
    CHECK(keys(editor.getProperty("connect", {})) == names(bridge::kSnapshotConnect));

    // 探针那份：不带界面字段、没有回调年龄。拿同样不带回调年龄的编辑器那份比，除三个界面字段外逐项相同
    const auto probe = miciri::buildBridgeSnapshot(p, "Host", nullptr, std::nullopt);
    const auto withUi = miciri::buildBridgeSnapshot(p, "Host", &ui, std::nullopt);
    std::vector<std::string> expected;
    for (const auto& k : names(bridge::kSnapshotRoot))
        if (!isUiField(k))
            expected.push_back(k);
    CHECK(keys(probe) == expected);
    for (const auto& k : expected)
    {
        INFO(k);
        CHECK(text(probe.getProperty(juce::Identifier(k), {})) == text(withUi.getProperty(juce::Identifier(k), {})));
    }
    CHECK(probe.getProperty("micType", {}).toString() == "condenser");
    CHECK(probe.getProperty("recommendedDistance", {}).isObject());
}
