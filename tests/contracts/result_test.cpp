// SPDX-License-Identifier: GPL-3.0-or-later
//
// Result 契约：枚举的线上名字合法且互不相同；measures 只含可直接锁 golden 的纯数值；
// 档位到色调的映射符合规格（数字削波红、模拟平顶最多琥珀、设备处理不参与色调等）。
#include <algorithm>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include "result/Results.h"

using namespace miciri::core;

static bool validWire(std::string_view s)
{
    if (s.empty() || s.front() < 'a' || s.front() > 'z')
        return false;
    return std::all_of(s.begin(), s.end(),
                       [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; });
}

template<class Names>
static void checkNames(const Names& names)
{
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        CHECK(validWire(names[i]));
        for (std::size_t j = i + 1; j < names.size(); ++j)
            CHECK(names[i] != names[j]);
    }
}

#define MEASURES_ARE_PLAIN(T) static_assert(isMeasures<T>, #T " must stay plain numbers")
MEASURES_ARE_PLAIN(NoSignalMeasures);
MEASURES_ARE_PLAIN(ChannelsMeasures);
MEASURES_ARE_PLAIN(GainMeasures);
MEASURES_ARE_PLAIN(NoiseMeasures);
MEASURES_ARE_PLAIN(HumMeasures);
MEASURES_ARE_PLAIN(DeviceProcessingMeasures);
MEASURES_ARE_PLAIN(ReverbMeasures);
MEASURES_ARE_PLAIN(DirectRatioMeasures);
MEASURES_ARE_PLAIN(PlosiveMeasures);
MEASURES_ARE_PLAIN(SibilanceMeasures);
MEASURES_ARE_PLAIN(ClickMeasures);
MEASURES_ARE_PLAIN(DirectionMeasures);
MEASURES_ARE_PLAIN(DistanceMeasures);
static_assert(!isMeasures<Evidence>, "evidence is variable length and must not be mistaken for measures");

TEST_CASE("result contract: names, plain measures and tone mapping")
{
    checkNames(kSignalStateNames);
    checkNames(kChannelLayoutNames);
    checkNames(kChannelStatusNames);
    checkNames(kChannelCaseNames);
    checkNames(kChannelPromptNames);
    checkNames(kAnalysedChannelNames);
    checkNames(kStepIdNames);
    checkNames(kStepStatusNames);
    checkNames(kToneNames);
    checkNames(kReliabilityNames);
    checkNames(kRetestReasonNames);
    checkNames(kGainBandNames);
    checkNames(kNoiseBandNames);
    checkNames(kHumKindNames);
    checkNames(kReverbBandNames);
    checkNames(kDrBandNames);
    checkNames(kSeverityNames);
    checkNames(kDirectionCaseNames);
    checkNames(kDistanceCaseNames);
    checkNames(kMicTypeNames);
    checkNames(kChangeKindNames);

    CHECK(toString(SignalState::AllZeroAfterSignal) == "all-zero-after-signal");
    CHECK(toString(ChannelPrompt::OneSilentHost) == "one-silent-host");

    // 置信度：可信不带原因，再测必须带原因
    CHECK((trusted().level == Reliability::Trusted && trusted().reason == RetestReason::None));
    CHECK(retest(RetestReason::Overflow).level == Reliability::Retest);
    CHECK(retest(RetestReason::Overflow).reason == RetestReason::Overflow);
    CHECK(retest(RetestReason::None).reason == RetestReason::Ambiguous);
    CHECK(isConsistent(trusted()));
    CHECK(isConsistent(retest(RetestReason::None)));
    CHECK(!isConsistent(Confidence{Reliability::Retest, RetestReason::None}));
    CHECK(!isConsistent(Confidence{Reliability::Trusted, RetestReason::Overflow}));

    // 无信号：没收到音频三种是红（主按钮禁用），底噪极低是琥珀（主按钮可点）
    for (auto s : {SignalState::NoCallback, SignalState::AllZeroFromStart, SignalState::AllZeroAfterSignal})
        CHECK(toneOf(NoSignalVerdict{s}) == Tone::Red);
    CHECK(toneOf(NoSignalVerdict{SignalState::VeryLowNoise}) == Tone::Amber);
    CHECK(toneOf(NoSignalVerdict{SignalState::Ok}) == Tone::Green);

    // 声道检查：五种提示都是琥珀，真立体声绿，没判出来无色调
    for (auto k : {ChannelCase::OneSilent, ChannelCase::OneDominant, ChannelCase::AntiPhase, ChannelCase::Identical})
    {
        ChannelsVerdict v;
        v.kind = k;
        CHECK(toneOf(v) == Tone::Amber);
    }
    CHECK(toneOf(ChannelsVerdict{}) == Tone::None);

    // 增益：数字削波与模拟平顶分开。平顶只把绿抬到琥珀，不会单独变红，也不会压低已有的红
    GainVerdict g;
    g.band = GainBand::Good;
    CHECK(toneOf(g) == Tone::Green);
    g.maybeFlatTop = true;
    CHECK(toneOf(g) == Tone::Amber);
    g.band = GainBand::TooLoud;
    CHECK(toneOf(g) == Tone::Red);
    GainVerdict clipped;
    clipped.digitalClip = true;
    CHECK(toneOf(clipped) == Tone::Red);
    for (auto b : {GainBand::TooQuiet, GainBand::Quiet, GainBand::Good, GainBand::Loud, GainBand::TooLoud})
    {
        GainVerdict flat;
        flat.band = b;
        flat.maybeFlatTop = true;
        CHECK(toneOf(flat) == worse(toneOf(GainVerdict{b, false, false}), Tone::Amber));
    }

    // 电流声：卡片上 hum 和 buzz 琥珀，啸叫红，标了轻微的一律琥珀
    HumVerdict h;
    CHECK(toneOf(h) == Tone::Green);
    h.kind = HumKind::Hum;
    CHECK(toneOf(h) == Tone::Amber);
    h.kind = HumKind::Buzz;
    CHECK(toneOf(h) == Tone::Amber);
    h.kind = HumKind::Whistle;
    CHECK(toneOf(h) == Tone::Red);
    h.slight = true;
    CHECK(toneOf(h) == Tone::Amber);

    CHECK(toneOf(NoiseVerdict{NoiseBand::Bad}) == Tone::Red);
    CHECK(toneOf(ReverbVerdict{ReverbBand::Boomy, false}) == Tone::Amber);
    CHECK(toneOf(ReverbVerdict{ReverbBand::VeryBoomy, false}) == Tone::Red);
    // 整组作废不能显示成任何档位的颜色
    for (auto b : {ReverbBand::Dry, ReverbBand::Normal, ReverbBand::Boomy, ReverbBand::VeryBoomy})
        CHECK(toneOf(ReverbVerdict{b, true}) == Tone::None);
    CHECK(toneOf(DirectRatioVerdict{DrBand::Low}) == Tone::Red);
    CHECK(toneOf(DirectRatioVerdict{DrBand::High}) == Tone::Green);
    CHECK(toneOf(Severity::Moderate) == Tone::Amber);
    CHECK(toneOf(DirectionVerdict{DirectionCase::Best}) == Tone::Green);
    CHECK(toneOf(DirectionVerdict{DirectionCase::SmallDifference}) == Tone::Green);
    CHECK(toneOf(DirectionVerdict{DirectionCase::MaybeReversed}) == Tone::Red);
    CHECK(toneOf(DirectionVerdict{DirectionCase::Uncertain}) == Tone::None);

    CHECK(worse(Tone::None, Tone::Green) == Tone::Green);
    CHECK(worse(Tone::Red, Tone::Amber) == Tone::Red);

    // 默认构造的结果是空证据、可信，measures 全零
    GainResult r;
    CHECK((r.evidence.marks.empty() && r.confidence.level == Reliability::Trusted));
    CHECK((r.measures.digitalClipRunsL == 0 && r.measures.flatPlateauPeaksR == 0));
}
