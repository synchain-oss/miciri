// SPDX-License-Identifier: GPL-3.0-or-later
//
// 档位到色调的固定映射。只做映射，不读阈值：档位由各分析模块依 thresholds.json 得出。
#include "result/Results.h"

namespace miciri::core
{
Tone worse(Tone a, Tone b)
{
    return static_cast<std::uint8_t>(a) >= static_cast<std::uint8_t>(b) ? a : b;
}

Tone toneOf(const NoSignalVerdict& v)
{
    switch (v.state)
    {
    case SignalState::Waiting:
        return Tone::None;
    case SignalState::Ok:
        return Tone::Green;
    case SignalState::VeryLowNoise:
        return Tone::Amber; // 通路是通的，主按钮可点
    case SignalState::NoCallback:
    case SignalState::AllZeroFromStart:
    case SignalState::AllZeroAfterSignal:
        return Tone::Red; // 主按钮禁用
    }
    return Tone::None;
}

Tone toneOf(const ChannelsVerdict& v)
{
    switch (v.kind)
    {
    case ChannelCase::Undetermined:
        return Tone::None;
    case ChannelCase::TrueStereo:
        return Tone::Green;
    case ChannelCase::OneSilent:
    case ChannelCase::OneDominant:
    case ChannelCase::AntiPhase:
    case ChannelCase::Identical:
        return Tone::Amber; // 不拦测试，不进“最该改”
    }
    return Tone::None;
}

Tone toneOf(const GainVerdict& v)
{
    if (v.digitalClip)
        return Tone::Red;
    Tone t = Tone::None;
    switch (v.band)
    {
    case GainBand::TooQuiet:
        t = Tone::Red;
        break;
    case GainBand::Quiet:
        t = Tone::Amber;
        break;
    case GainBand::Good:
        t = Tone::Green;
        break;
    case GainBand::Loud:
        t = Tone::Amber;
        break;
    case GainBand::TooLoud:
        t = Tone::Red;
        break;
    }
    // 模拟平顶只是辅助证据：最多把绿抬到琥珀（“可能削波”），不会单独变红
    return v.maybeFlatTop ? worse(t, Tone::Amber) : t;
}

Tone toneOf(const NoiseVerdict& v)
{
    switch (v.band)
    {
    case NoiseBand::Bad:
        return Tone::Red;
    case NoiseBand::Fair:
        return Tone::Amber;
    case NoiseBand::Good:
        return Tone::Green;
    }
    return Tone::None;
}

Tone toneOf(const HumVerdict& v)
{
    if (v.kind == HumKind::None)
        return Tone::Green;
    // 这是卡片的色调：轻微、hum、buzz 都是琥珀，只有不轻微的啸叫是红。
    // 电平管上的标记另有规则：检出就红，不看这里。
    return v.kind == HumKind::Whistle && !v.slight ? Tone::Red : Tone::Amber;
}

Tone toneOf(const ReverbVerdict& v)
{
    if (v.gated)
        return Tone::None; // 整组作废：灰色“无法判断”，不能显示成健康的颜色
    switch (v.band)
    {
    case ReverbBand::Dry:
    case ReverbBand::Normal:
        return Tone::Green;
    case ReverbBand::Boomy:
        return Tone::Amber;
    case ReverbBand::VeryBoomy:
        return Tone::Red;
    }
    return Tone::None;
}

Tone toneOf(const DirectRatioVerdict& v)
{
    switch (v.band)
    {
    case DrBand::Low:
        return Tone::Red;
    case DrBand::Mid:
        return Tone::Amber;
    case DrBand::High:
        return Tone::Green;
    }
    return Tone::None;
}

Tone toneOf(const DirectionVerdict& v)
{
    switch (v.kind)
    {
    case DirectionCase::Best:
    case DirectionCase::SmallDifference:
        return Tone::Green;
    case DirectionCase::MaybeReversed:
        return Tone::Red;
    case DirectionCase::Uncertain:
        return Tone::None;
    }
    return Tone::None;
}

Tone toneOf(Severity s)
{
    switch (s)
    {
    case Severity::Light:
        return Tone::Green;
    case Severity::Moderate:
        return Tone::Amber;
    case Severity::Heavy:
        return Tone::Red;
    }
    return Tone::None;
}
} // namespace miciri::core
