// SPDX-License-Identifier: GPL-3.0-or-later
//
// 结果与桥共用的枚举，一处定义标识符与线上名字（界面看到的字符串）。MICIRI_DEFINE_ENUM(名, 列表) 生成：
//   enum class 名、k<名>Names（按取值顺序排列的线上名字）、toString(名)。
// 取值必须从 0 连续排列；新增取值只能追加在末尾，已有的不改名、不调换顺序，否则要升桥契约版本（见
// docs/contracts/BRIDGE.md）。 线上名字对应 web/shared/bridge-api.js 里同名的 ENUM_* 数组，由 tests/contracts 对拍。
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#define MICIRI_X_ID(id, wire) id,
#define MICIRI_X_NAME(id, wire) std::string_view{wire},
#define MICIRI_X_ONE(id, wire) +1

#define MICIRI_DEFINE_ENUM(Name, LIST)                                                                         \
    enum class Name : std::uint8_t                                                                             \
    {                                                                                                          \
        LIST(MICIRI_X_ID)                                                                                      \
    };                                                                                                         \
    inline constexpr std::array<std::string_view, (0 LIST(MICIRI_X_ONE))> k##Name##Names{LIST(MICIRI_X_NAME)}; \
    constexpr std::string_view toString(Name v)                                                                \
    {                                                                                                          \
        return k##Name##Names[static_cast<std::size_t>(v)];                                                    \
    }

namespace miciri::core
{
// 下面的宏列表按“一行一个取值”手工排版，不交给 clang-format：它会把长字符串字面量拆成两段
// clang-format off
// 无信号诊断（接通步）
#define MICIRI_ENUM_SignalState(X)                 \
    X(Waiting, "waiting")                          \
    X(Ok, "ok")                                    \
    X(NoCallback, "no-callback")                   \
    X(AllZeroFromStart, "all-zero-from-start")     \
    X(AllZeroAfterSignal, "all-zero-after-signal") \
    X(VeryLowNoise, "very-low-noise")
MICIRI_DEFINE_ENUM(SignalState, MICIRI_ENUM_SignalState)

// 声道布局与声道检查
#define MICIRI_ENUM_ChannelLayout(X) X(Mono, "mono") X(Stereo, "stereo")
MICIRI_DEFINE_ENUM(ChannelLayout, MICIRI_ENUM_ChannelLayout)
#define MICIRI_ENUM_ChannelStatus(X)  \
    X(Signal, "signal")               \
    X(FloorOnly, "floor-only")        \
    X(Silent, "silent")               \
    X(SameAsLeft, "same-as-left")     \
    X(Unknown, "unknown")             \
    X(InvertedFromLeft, "inverted-from-left")
MICIRI_DEFINE_ENUM(ChannelStatus, MICIRI_ENUM_ChannelStatus)
#define MICIRI_ENUM_ChannelCase(X)  \
    X(Undetermined, "undetermined") \
    X(TrueStereo, "true-stereo")    \
    X(OneSilent, "one-silent")      \
    X(OneDominant, "one-dominant")  \
    X(AntiPhase, "antiphase")       \
    X(Identical, "identical")
MICIRI_DEFINE_ENUM(ChannelCase, MICIRI_ENUM_ChannelCase)
// 界面上的提示：在 ChannelCase 之上加“宿主没有单声道轨”这一变体，由 Session 按宿主能力换算
#define MICIRI_ENUM_ChannelPrompt(X)    \
    X(None, "none")                     \
    X(OneSilent, "one-silent")          \
    X(OneSilentHost, "one-silent-host") \
    X(OneDominant, "one-dominant")      \
    X(AntiPhase, "antiphase")           \
    X(Identical, "identical")
MICIRI_DEFINE_ENUM(ChannelPrompt, MICIRI_ENUM_ChannelPrompt)
#define MICIRI_ENUM_AnalysedChannel(X) X(Left, "left") X(Right, "right")
MICIRI_DEFINE_ENUM(AnalysedChannel, MICIRI_ENUM_AnalysedChannel)

// 步骤与状态
#define MICIRI_ENUM_StepId(X) \
    X(Connect, "connect")     \
    X(Gain, "gain")           \
    X(Quiet, "quiet")         \
    X(Room, "room")           \
    X(Position, "position")   \
    X(Direction, "direction")
MICIRI_DEFINE_ENUM(StepId, MICIRI_ENUM_StepId)
#define MICIRI_ENUM_StepStatus(X) \
    X(Idle, "idle") X(Running, "running") X(Done, "done") X(Invalid, "invalid") X(Stale, "stale")
MICIRI_DEFINE_ENUM(StepStatus, MICIRI_ENUM_StepStatus)

// 色调、置信度与再测原因
#define MICIRI_ENUM_Tone(X) X(None, "none") X(Green, "green") X(Amber, "amber") X(Red, "red")
MICIRI_DEFINE_ENUM(Tone, MICIRI_ENUM_Tone)
#define MICIRI_ENUM_Reliability(X) X(Trusted, "trusted") X(Retest, "retest")
MICIRI_DEFINE_ENUM(Reliability, MICIRI_ENUM_Reliability)
#define MICIRI_ENUM_RetestReason(X)              \
    X(None, "none")                              \
    X(TooShort, "too-short")                     \
    X(Overflow, "overflow")                      \
    X(CallbackGap, "callback-gap")               \
    X(RateChanged, "rate-changed")               \
    X(Clipped, "clipped")                        \
    X(LowPnr, "low-pnr")                         \
    X(GateDetected, "gate-detected")             \
    X(Unstable, "unstable")                      \
    X(Ambiguous, "ambiguous")                    \
    X(FewClaps, "few-claps")                     \
    X(DeviceProcessing, "device-processing")     \
    X(VoiceDetected, "voice-detected")           \
    X(OfflineRender, "offline-render")           \
    X(LayoutChanged, "layout-changed")
MICIRI_DEFINE_ENUM(RetestReason, MICIRI_ENUM_RetestReason)

// 各项结果的档位
#define MICIRI_ENUM_GainBand(X) \
    X(TooQuiet, "too-quiet") X(Quiet, "quiet") X(Good, "good") X(Loud, "loud") X(TooLoud, "too-loud")
MICIRI_DEFINE_ENUM(GainBand, MICIRI_ENUM_GainBand)
#define MICIRI_ENUM_NoiseBand(X) X(Bad, "bad") X(Fair, "fair") X(Good, "good")
MICIRI_DEFINE_ENUM(NoiseBand, MICIRI_ENUM_NoiseBand)
#define MICIRI_ENUM_HumKind(X) X(None, "none") X(Hum, "hum") X(Buzz, "buzz") X(Whistle, "whistle")
MICIRI_DEFINE_ENUM(HumKind, MICIRI_ENUM_HumKind)
#define MICIRI_ENUM_ReverbBand(X) X(Dry, "dry") X(Normal, "normal") X(Boomy, "boomy") X(VeryBoomy, "very-boomy")
MICIRI_DEFINE_ENUM(ReverbBand, MICIRI_ENUM_ReverbBand)
#define MICIRI_ENUM_DrBand(X) X(Low, "low") X(Mid, "mid") X(High, "high")
MICIRI_DEFINE_ENUM(DrBand, MICIRI_ENUM_DrBand)
// 喷麦、齿音、口水音共用的轻重三档
#define MICIRI_ENUM_Severity(X) X(Light, "light") X(Moderate, "moderate") X(Heavy, "heavy")
MICIRI_DEFINE_ENUM(Severity, MICIRI_ENUM_Severity)
#define MICIRI_ENUM_DirectionCase(X)           \
    X(Best, "best")                            \
    X(MaybeReversed, "maybe-reversed")         \
    X(SmallDifference, "small-difference")     \
    X(Uncertain, "uncertain")
MICIRI_DEFINE_ENUM(DirectionCase, MICIRI_ENUM_DirectionCase)
#define MICIRI_ENUM_DistanceCase(X) \
    X(Unknown, "unknown") X(Fine, "fine") X(TooClose, "too-close") X(TooFar, "too-far") X(Conflicting, "conflicting")
MICIRI_DEFINE_ENUM(DistanceCase, MICIRI_ENUM_DistanceCase)

// 用户在界面上选的话筒类型（影响“增益偏低”要不要算问题、耳机麦要不要做方向测试）
#define MICIRI_ENUM_MicType(X) \
    X(Unknown, "unknown") X(Dynamic, "dynamic") X(Condenser, "condenser") X(Usb, "usb") X(Headset, "headset")
MICIRI_DEFINE_ENUM(MicType, MICIRI_ENUM_MicType)

// 用户说“我动过了”的三种：对应的结果按“过期规则”标过期
#define MICIRI_ENUM_ChangeKind(X) X(Gain, "gain") X(Position, "position") X(Room, "room")
MICIRI_DEFINE_ENUM(ChangeKind, MICIRI_ENUM_ChangeKind)

// 流程页用（追加）：一步之内的阶段、拍手逐下的判定、测试中的提示
#define MICIRI_ENUM_StepPhase(X)   \
    X(None, "none")                \
    X(SoftClap, "soft-clap")       \
    X(Plosive, "plosive")          \
    X(Sibilant, "sibilant")        \
    X(Natural, "natural")          \
    X(Direction, "direction")      \
    X(Confirm, "confirm")
MICIRI_DEFINE_ENUM(StepPhase, MICIRI_ENUM_StepPhase)
#define MICIRI_ENUM_ClapState(X) X(Valid, "valid") X(TooSoft, "too-soft") X(Noisy, "noisy") X(Truncated, "truncated")
MICIRI_DEFINE_ENUM(ClapState, MICIRI_ENUM_ClapState)
#define MICIRI_ENUM_FlowNotice(X)    \
    X(None, "none")                  \
    X(Success, "success")            \
    X(Spoiled, "spoiled")            \
    X(ClapClipped, "clap-clipped")   \
    X(TooQuiet, "too-quiet")         \
    X(Gated, "gated")                \
    X(Redo, "redo")
MICIRI_DEFINE_ENUM(FlowNotice, MICIRI_ENUM_FlowNotice)

// 追加（建议与“最该改”进桥）：原来定义在 room/Room.h 与 decide/Decide.h，取值与顺序不变
// 一下拍手在一个频带里的情况，按判断顺序：先看有没有噪声参考与可拟合的衰减，再看门限，再看峰噪比与 R²；
// 有效的几下在汇总时还可能因偏离中位数被去掉。no-noise-reference：拍手前的噪声窗被录音开头截得短于 min_noise_ms；
// short-decay：衰减太短或不到 -25 dB
#define MICIRI_ENUM_ClapBandStatus(X)                X(Valid, "valid")                                X(NoNoiseReference, "no-noise-reference")        X(ShortDecay, "short-decay")                     X(Gated, "gated")                                X(LowPnr, "low-pnr")                             X(PoorFit, "poor-fit")                           X(Outlier, "outlier")
MICIRI_DEFINE_ENUM(ClapBandStatus, MICIRI_ENUM_ClapBandStatus)
// 决策表：T20（长 = 偏混或很混；干和正常都算正常）乘 D/R（低、中、高）。
// unknown：T20 或 D/R 缺，或要再测；good：正常 + 高；fine-not-priority：正常 + 中，可以录，不进“最该改”；
// move-closer-check-direction：正常 + 低，离太远或方向错；change-room-and-move-closer：长 + 低；
// keep-close-dont-back-off：长 + 高，近距离压住了房间，别退后；move-closer-then-treat：长 + 中，先靠近再测，还不行再加吸声
#define MICIRI_ENUM_RoomAdvice(X)                                      X(Unknown, "unknown")                                              X(Good, "good")                                                    X(FineNotPriority, "fine-not-priority")                            X(MoveCloserCheckDirection, "move-closer-check-direction")         X(ChangeRoomAndMoveCloser, "change-room-and-move-closer")          X(KeepCloseDontBackOff, "keep-close-dont-back-off")                X(MoveCloserThenTreat, "move-closer-then-treat")
MICIRI_DEFINE_ENUM(RoomAdvice, MICIRI_ENUM_RoomAdvice)
// 距离标尺的建议。drink-water：退后后口水音仍重，口干也会加重；fix-room-or-direction-first：两边都有证据，矛盾
#define MICIRI_ENUM_DistanceAdvice(X)                                  X(None, "none")                                                    X(BackOffALittle, "back-off-a-little")                             X(DrinkWater, "drink-water")                                       X(MoveCloser, "move-closer")                                       X(FixRoomOrDirectionFirst, "fix-room-or-direction-first")
MICIRI_DEFINE_ENUM(DistanceAdvice, MICIRI_ENUM_DistanceAdvice)
// 方向测试的提示放在哪。none：耳机麦，或测过且没过期；stale：挪过位置且测过；main-button：T20 正常 + D/R 低；
// action-with-button：动作里一条，加次要按钮；card-button：其余，方向卡上一个小按钮
#define MICIRI_ENUM_DirectionPrompt(X)                 X(None, "none")                                    X(Stale, "stale")                                  X(MainButton, "main-button")                       X(ActionWithButton, "action-with-button")          X(CardButton, "card-button")
MICIRI_DEFINE_ENUM(DirectionPrompt, MICIRI_ENUM_DirectionPrompt)
// “最该改”的候选项。no-signal 与 digital-clip 固定在前，其余按 影响 × 置信度 ÷ 修改代价 排序
#define MICIRI_ENUM_FixItem(X)              X(NoSignal, "no-signal")                X(DigitalClip, "digital-clip")          X(Direction, "direction")               X(Room, "room")                         X(Distance, "distance")                 X(Hum, "hum")                           X(Gain, "gain")                         X(Noise, "noise")                       X(Plosive, "plosive")
MICIRI_DEFINE_ENUM(FixItem, MICIRI_ENUM_FixItem)
// clang-format on
} // namespace miciri::core
