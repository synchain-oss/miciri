// SPDX-License-Identifier: GPL-3.0-or-later
//
// 桥契约的 C++ 一侧：消息名、状态枚举和快照字段，和 web/shared/bridge-api.js 是同一份契约，tests/contracts 逐项对拍。
// 不依赖 JUCE（纯标准库加 core 的枚举），所以契约测试在只建 core 的构建里也能编。
// 枚举的线上名字来自 core/result/Enums.h（一处定义）；其余名字在这里写一次，JS 侧写一次，对拍保证相同。
// 版本与扩展规则见 docs/contracts/BRIDGE.md：同一版本内只追加，改名、删除、调换顺序要升 kApiVersion。
#pragma once

#include <array>
#include <cstddef>
#include <string_view>
#include <vector>

#include "result/Enums.h"

namespace miciri::bridge
{
inline constexpr int kApiVersion = 1;

template<class... S>
constexpr std::array<std::string_view, sizeof...(S)> names(S... s)
{
    return {std::string_view(s)...};
}

// 表名与 bridge-api.js 里的 export 名逐字相同
inline constexpr auto kFunctions =
    names("requestInitialState", "setLang", "setUiScale", "commitUiScale", "setGuideSeen", "setMicType", "startStep",
          "cancelStep", "reportChange", "dismissChannelPrompt", "openExternal", "showLicense", "clearClipLatch");
inline constexpr auto kEvents = names("miciri.state", "miciri.meters", "miciri.error");
inline constexpr auto kReasons =
    names("bad-lang", "bad-scale", "bad-step", "bad-arg", "no-pending", "scale-locked", "not-ready", "busy");

inline constexpr auto kSnapshotRoot =
    names("apiVersion", "lang", "uiScale", "guideSeen", "micType", "device", "connect", "steps", "results");
inline constexpr auto kSnapshotDevice = names("sampleRate", "bufferSize", "layout", "hostName");
inline constexpr auto kSnapshotConnect =
    names("signalState", "callbackAgeSec", "allZero", "noiseDbfs", "channelStatusL", "channelStatusR", "channelPrompt",
          "channelPromptDismissed", "analysedChannel");
inline constexpr auto kSnapshotStep = names("status", "progressSec", "durationSec", "invalidReason");

inline constexpr auto kResultSections =
    names("noSignal", "channels", "gain", "noise", "hum", "deviceProcessing", "reverb", "directRatio", "plosive",
          "sibilance", "click", "direction", "distance");
inline constexpr auto kResultCommon = names("tone", "reliability", "retestReason", "stale");
inline constexpr auto kResultNoSignal = names("zeroSeconds", "maxFrameDbfs", "p10FrameDbfs", "riseDb", "state");
inline constexpr auto kResultChannels =
    names("floorDbfsL", "floorDbfsR", "activeDbfsL", "activeDbfsR", "deltaDb", "weakRiseDb", "sumOverFloorDb",
          "diffOverFloorDb", "usedFallback", "kind", "statusL", "statusR", "analysedChannel");
inline constexpr auto kResultGain =
    names("samplePeakDbfsL", "samplePeakDbfsR", "truePeakDbfsL", "truePeakDbfsR", "loudnessLkfs", "digitalClipRunsL",
          "digitalClipRunsR", "flatPlateauPeaksL", "flatPlateauPeaksR", "band", "digitalClip", "maybeFlatTop");
inline constexpr auto kResultNoise = names("noiseDbfs", "noiseDbfsA", "snrDb", "noisePeakDbfs", "band");
inline constexpr auto kResultHum = names("fundamentalHz", "overFloorDb", "energySharePct", "highestHarmonicHz",
                                         "levelDbfs", "peakDbfs", "kind", "slight");
inline constexpr auto kResultDeviceProcessing = names("longestZeroMs", "zeroRuns", "detected");
inline constexpr auto kResultReverb =
    names("t20Sec", "tMidSec", "bandPnrDb", "bandR2", "repeatMaxOverMin", "band", "gated");
inline constexpr auto kResultDirectRatio = names("drDb", "lowerBoundOnly", "band");
inline constexpr auto kResultPlosive = names("worstRatioDb", "events", "severity", "lowSensitivity");
inline constexpr auto kResultSibilance =
    names("midHighDb", "highDb", "effectiveBandwidthHz", "events", "severity", "highBandDisabled");
inline constexpr auto kResultClick =
    names("perMinute", "loudestRelDb", "loudestDbfs", "analysedSeconds", "count", "severity");
inline constexpr auto kResultDirection =
    names("levelDb", "brightnessDb", "currentIndex", "bestOtherAdvantageDb", "kind");
inline constexpr auto kResultDistance = names("closeEvidence", "farEvidence", "kind");

inline constexpr auto kMeterFields =
    names("levelDbfsL", "levelDbfsR", "peakDbfsL", "peakDbfsR", "clipLatchL", "clipLatchR");

// 对拍用的登记表：JS 的 export 名 -> 该表的内容
struct Table
{
    std::string_view name;
    const std::string_view* data;
    std::size_t size;
};

template<std::size_t N>
constexpr Table table(std::string_view name, const std::array<std::string_view, N>& items)
{
    return {name, items.data(), N};
}

inline std::vector<Table> contractTables()
{
    namespace c = miciri::core;
    return {
        table("BRIDGE_FUNCTIONS", kFunctions),
        table("BRIDGE_EVENTS", kEvents),
        table("BRIDGE_REASONS", kReasons),
        table("SNAPSHOT_ROOT", kSnapshotRoot),
        table("SNAPSHOT_DEVICE", kSnapshotDevice),
        table("SNAPSHOT_CONNECT", kSnapshotConnect),
        table("SNAPSHOT_STEP", kSnapshotStep),
        table("RESULT_SECTIONS", kResultSections),
        table("RESULT_COMMON", kResultCommon),
        table("RESULT_NO_SIGNAL", kResultNoSignal),
        table("RESULT_CHANNELS", kResultChannels),
        table("RESULT_GAIN", kResultGain),
        table("RESULT_NOISE", kResultNoise),
        table("RESULT_HUM", kResultHum),
        table("RESULT_DEVICE_PROCESSING", kResultDeviceProcessing),
        table("RESULT_REVERB", kResultReverb),
        table("RESULT_DIRECT_RATIO", kResultDirectRatio),
        table("RESULT_PLOSIVE", kResultPlosive),
        table("RESULT_SIBILANCE", kResultSibilance),
        table("RESULT_CLICK", kResultClick),
        table("RESULT_DIRECTION", kResultDirection),
        table("RESULT_DISTANCE", kResultDistance),
        table("METER_FIELDS", kMeterFields),
        table("ENUM_SIGNAL_STATE", c::kSignalStateNames),
        table("ENUM_CHANNEL_LAYOUT", c::kChannelLayoutNames),
        table("ENUM_CHANNEL_STATUS", c::kChannelStatusNames),
        table("ENUM_CHANNEL_CASE", c::kChannelCaseNames),
        table("ENUM_CHANNEL_PROMPT", c::kChannelPromptNames),
        table("ENUM_ANALYSED_CHANNEL", c::kAnalysedChannelNames),
        table("ENUM_STEP_ID", c::kStepIdNames),
        table("ENUM_STEP_STATUS", c::kStepStatusNames),
        table("ENUM_TONE", c::kToneNames),
        table("ENUM_RELIABILITY", c::kReliabilityNames),
        table("ENUM_RETEST_REASON", c::kRetestReasonNames),
        table("ENUM_GAIN_BAND", c::kGainBandNames),
        table("ENUM_NOISE_BAND", c::kNoiseBandNames),
        table("ENUM_HUM_KIND", c::kHumKindNames),
        table("ENUM_REVERB_BAND", c::kReverbBandNames),
        table("ENUM_DR_BAND", c::kDrBandNames),
        table("ENUM_SEVERITY", c::kSeverityNames),
        table("ENUM_DIRECTION_CASE", c::kDirectionCaseNames),
        table("ENUM_DISTANCE_CASE", c::kDistanceCaseNames),
        table("ENUM_MIC_TYPE", c::kMicTypeNames),
        table("ENUM_CHANGE_KIND", c::kChangeKindNames),
    };
}
} // namespace miciri::bridge
