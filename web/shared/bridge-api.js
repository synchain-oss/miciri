// SPDX-License-Identifier: GPL-3.0-or-later
//
// 桥契约：界面与 C++ 之间的消息名、状态枚举和快照字段，只有名字，不含传输与逻辑（传输层在 bridge.js）。
// 与 src/plugin/BridgeApi.h 是同一份契约，tests/contracts 逐项对拍，任何一边多一项、少一项或顺序不同都判红。
// 载荷语义、单位和版本规则见 docs/contracts/BRIDGE.md。
//
// 文法（对拍测试不执行本文件，只按这个文法读）：每个 export 都是 `export const 名字 = [ "字符串", ... ];`
// 或 `export const 名字 = 整数;`；字符串只含字母、数字、点、减号；不写表达式、展开、模板串；注释只用整行 // 。
// 扩展方式：同一版本内只追加（函数、事件、字段、枚举值只加在末尾）；改名、删除、调换顺序要升 BRIDGE_API_VERSION。

export const BRIDGE_API_VERSION = 1;

// 界面调原生
export const BRIDGE_FUNCTIONS = [
    "requestInitialState",
    "setLang",
    "setUiScale",
    "commitUiScale",
    "setGuideSeen",
    "setMicType",
    "startStep",
    "cancelStep",
    "reportChange",
    "dismissChannelPrompt",
    "openExternal",
    "showLicense",
    "clearClipLatch",
];

// 原生推给界面（纯下行，界面只订阅）
export const BRIDGE_EVENTS = ["miciri.state", "miciri.meters", "miciri.error"];

// 调用回执的失败原因
export const BRIDGE_REASONS = [
    "bad-lang",
    "bad-scale",
    "bad-step",
    "bad-arg",
    "no-pending",
    "scale-locked",
    "not-ready",
    "busy",
];

// 快照（miciri.state 的载荷，requestInitialState 的回执）
export const SNAPSHOT_ROOT = [
    "apiVersion",
    "lang",
    "uiScale",
    "guideSeen",
    "micType",
    "device",
    "connect",
    "steps",
    "results",
    "bands",
    "live",
];
export const SNAPSHOT_DEVICE = ["sampleRate", "bufferSize", "layout", "hostName", "offlineRender"];
export const SNAPSHOT_CONNECT = [
    "signalState",
    "callbackAgeSec",
    "allZero",
    "noiseDbfs",
    "channelStatusL",
    "channelStatusR",
    "channelPrompt",
    "channelPromptDismissed",
    "analysedChannel",
];
// steps.<StepId>.<字段>；invalidReason 取 ENUM_RETEST_REASON
export const SNAPSHOT_STEP = ["status", "progressSec", "durationSec", "invalidReason"];

// results.<区段>.<字段>：每个区段 = 该结果的连续量 + 档位 + RESULT_COMMON；字段名即 Result 结构的 camelCase 名
export const RESULT_SECTIONS = [
    "noSignal",
    "channels",
    "gain",
    "noise",
    "hum",
    "deviceProcessing",
    "reverb",
    "directRatio",
    "plosive",
    "sibilance",
    "click",
    "direction",
    "distance",
];
export const RESULT_COMMON = ["tone", "reliability", "retestReason", "stale"];
export const RESULT_NO_SIGNAL = ["zeroSeconds", "maxFrameDbfs", "p10FrameDbfs", "riseDb", "state"];
export const RESULT_CHANNELS = [
    "floorDbfsL",
    "floorDbfsR",
    "activeDbfsL",
    "activeDbfsR",
    "deltaDb",
    "weakRiseDb",
    "sumOverFloorDb",
    "diffOverFloorDb",
    "usedFallback",
    "kind",
    "statusL",
    "statusR",
    "analysedChannel",
];
export const RESULT_GAIN = [
    "samplePeakDbfsL",
    "samplePeakDbfsR",
    "truePeakDbfsL",
    "truePeakDbfsR",
    "loudnessLkfs",
    "digitalClipRunsL",
    "digitalClipRunsR",
    "flatPlateauPeaksL",
    "flatPlateauPeaksR",
    "band",
    "digitalClip",
    "maybeFlatTop",
];
export const RESULT_NOISE = ["noiseDbfs", "noiseDbfsA", "snrDb", "noisePeakDbfs", "band"];
export const RESULT_HUM = [
    "fundamentalHz",
    "overFloorDb",
    "energySharePct",
    "highestHarmonicHz",
    "levelDbfs",
    "peakDbfs",
    "kind",
    "slight",
];
export const RESULT_DEVICE_PROCESSING = ["longestZeroMs", "zeroRuns", "detected"];
export const RESULT_REVERB = ["t20Sec", "tMidSec", "bandPnrDb", "bandR2", "repeatMaxOverMin", "band", "gated"];
export const RESULT_DIRECT_RATIO = ["drDb", "lowerBoundOnly", "band"];
export const RESULT_PLOSIVE = ["worstRatioDb", "events", "severity", "lowSensitivity"];
export const RESULT_SIBILANCE = [
    "midHighDb",
    "highDb",
    "effectiveBandwidthHz",
    "events",
    "severity",
    "highBandDisabled",
];
export const RESULT_CLICK = ["perMinute", "loudestRelDb", "loudestDbfs", "analysedSeconds", "count", "severity"];
export const RESULT_DIRECTION = ["levelDb", "brightnessDb", "currentIndex", "bestOtherAdvantageDb", "kind"];
export const RESULT_DISTANCE = ["closeEvidence", "farEvidence", "kind"];

// miciri.meters 的载荷；单声道只用 L 一路
export const METER_FIELDS = ["levelDbfsL", "levelDbfsR", "peakDbfsL", "peakDbfsR", "clipLatchL", "clipLatchR"];

// 枚举的线上取值，顺序即 C++ 枚举的取值顺序
export const ENUM_SIGNAL_STATE = [
    "waiting",
    "ok",
    "no-callback",
    "all-zero-from-start",
    "all-zero-after-signal",
    "very-low-noise",
];
export const ENUM_CHANNEL_LAYOUT = ["mono", "stereo"];
export const ENUM_CHANNEL_STATUS = ["signal", "floor-only", "silent", "same-as-left", "unknown", "inverted-from-left"];
export const ENUM_CHANNEL_CASE = [
    "undetermined",
    "true-stereo",
    "one-silent",
    "one-dominant",
    "antiphase",
    "identical",
];
export const ENUM_CHANNEL_PROMPT = ["none", "one-silent", "one-silent-host", "one-dominant", "antiphase", "identical"];
export const ENUM_ANALYSED_CHANNEL = ["left", "right"];
export const ENUM_STEP_ID = ["connect", "gain", "quiet", "room", "position", "direction"];
export const ENUM_STEP_STATUS = ["idle", "running", "done", "invalid", "stale"];
export const ENUM_TONE = ["none", "green", "amber", "red"];
export const ENUM_RELIABILITY = ["trusted", "retest"];
export const ENUM_RETEST_REASON = [
    "none",
    "too-short",
    "overflow",
    "callback-gap",
    "rate-changed",
    "clipped",
    "low-pnr",
    "gate-detected",
    "unstable",
    "ambiguous",
    "few-claps",
    "device-processing",
    "voice-detected",
    "offline-render",
    "layout-changed",
];
export const ENUM_GAIN_BAND = ["too-quiet", "quiet", "good", "loud", "too-loud"];
export const ENUM_NOISE_BAND = ["bad", "fair", "good"];
export const ENUM_HUM_KIND = ["none", "hum", "buzz", "whistle"];
export const ENUM_REVERB_BAND = ["dry", "normal", "boomy", "very-boomy"];
export const ENUM_DR_BAND = ["low", "mid", "high"];
export const ENUM_SEVERITY = ["light", "moderate", "heavy"];
export const ENUM_DIRECTION_CASE = ["best", "maybe-reversed", "small-difference", "uncertain"];
export const ENUM_DISTANCE_CASE = ["unknown", "fine", "too-close", "too-far", "conflicting"];
export const ENUM_MIC_TYPE = ["unknown", "dynamic", "condenser", "usb", "headset"];
export const ENUM_CHANGE_KIND = ["gain", "position", "room"];

// 追加（流程页用）：档位边界，由阈值文件换算，界面不写死数字。键名是 thresholds.json 里同名键的 camelCase 形式
export const SNAPSHOT_BANDS = [
    "tpTooQuietDbfs",
    "tpQuietDbfs",
    "tpLoudDbfs",
    "tpTooLoudDbfs",
    "targetPeakDbfs",
    "snrBadBelowDb",
    "snrGoodAboveDb",
    "t20DryBelowS",
    "t20NormalBelowS",
    "t20BoomyBelowS",
    "drLowBelowDb",
    "drHighAboveDb",
    "clipAdviceDropDb",
    "t20BandsHz",
];
// 追加（流程页用）：正在进行的那一步的实时信息，同一时刻只有一步在跑，所以不按步骤分键
export const SNAPSHOT_LIVE = [
    "phase",
    "round",
    "rounds",
    "phaseElapsedSec",
    "phaseDurationSec",
    "claps",
    "clapsNeeded",
    "clapsMax",
    "directionIndex",
    "notice",
    "noticeSec",
];
export const ENUM_STEP_PHASE = ["none", "soft-clap", "plosive", "sibilant", "natural", "direction", "confirm"];
export const ENUM_CLAP_STATE = ["valid", "too-soft", "noisy", "truncated"];
export const ENUM_FLOW_NOTICE = ["none", "success", "spoiled", "clap-clipped", "too-quiet", "gated", "redo"];
