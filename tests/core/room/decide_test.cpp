// SPDX-License-Identifier: GPL-3.0-or-later
//
// 方向的测量与判断、决策表的每一格、距离标尺（用合成的证据）、方向测试的提示方式。
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

#include "config/Thresholds.h"
#include "core/level/Synth.h"
#include "decide/Decide.h"
#include "direction/Direction.h"

using namespace miciri;
using namespace miciri::testing;
using core::DirectionCase;
using core::EvidenceLevel;

namespace
{
std::array<core::DirectionTake, 4> takesAt(std::array<double, 4> levels, std::array<double, 4> brightness = {})
{
    std::array<core::DirectionTake, 4> t{};
    for (std::size_t i = 0; i < 4; ++i)
        t[i] = {levels[i], brightness[i], true};
    return t;
}

core::ReverbResult reverbOf(core::ReverbBand band, bool trusted = true)
{
    core::ReverbResult r;
    r.verdict.band = band;
    if (!trusted)
        r.confidence = core::retest(core::RetestReason::Unstable);
    return r;
}
core::DirectRatioResult drOf(core::DrBand band, bool trusted = true)
{
    core::DirectRatioResult r;
    r.verdict.band = band;
    if (!trusted)
        r.confidence = core::retest(core::RetestReason::Clipped);
    return r;
}
} // namespace

TEST_CASE("direction level is the median of active K-weighted frames, brightness is a band ratio")
{
    for (double fs : {44100.0, 48000.0, 96000.0})
    {
        INFO("fs " << fs);
        const std::vector<double> loud = voice(fs, 4.0, 0.1, 3);
        std::vector<double> quiet = loud;
        for (double& v : quiet)
            v *= fromDb(-6.0);
        const core::DirectionTake a = core::measureDirectionTake(loud, fs);
        const core::DirectionTake b = core::measureDirectionTake(quiet, fs);
        REQUIRE(a.active);
        CHECK(a.levelDb - b.levelDb == within(6.0, 0.01));
        CHECK(a.brightnessDb == within(b.brightnessDb, 0.01));
        std::vector<double> airy = loud;
        mix(airy, sine(fs, 6000.0, 0.05, 4.0));
        CHECK(core::measureDirectionTake(airy, fs).brightnessDb > a.brightnessDb + 3.0);
    }
    CHECK_FALSE(core::measureDirectionTake(std::vector<double>(48000, 0.0), 48000.0).active);
    CHECK_FALSE(core::measureDirectionTake({}, 48000.0).active);
    CHECK_FALSE(core::measureDirectionTake(std::vector<double>(1000, 0.1), 48000.0).active); // 不到一帧
    std::vector<double> broken = voice(48000.0, 4.0, 0.1, 3);
    broken[1000] = std::numeric_limits<double>::quiet_NaN();
    broken[2000] = std::numeric_limits<double>::infinity();
    const core::DirectionTake t = core::measureDirectionTake(broken, 48000.0);
    CHECK((t.active && std::isfinite(t.levelDb) && std::isfinite(t.brightnessDb)));
    CHECK(core::measureDirectionTake(voice(22050.0, 4.0, 0.1, 3), 22050.0).active);
}

TEST_CASE("direction verdicts")
{
    // 心形话筒的四个方向（正面、左、右、背面）：侧面低几 dB，背面低得多；背面的亮度反而最高
    const auto takes = takesAt({-30.0, -32.5, -35.5, -45.0}, {-9.0, -13.0, -13.0, -7.0});
    SECTION("the current direction is the loudest")
    {
        CHECK(core::decideDirection(takes, 0).result.verdict.kind == DirectionCase::Best);
    }
    SECTION("a side is uncertain when the louder direction is less than the repeat trigger")
    {
        CHECK(core::decideDirection(takes, 1).result.verdict.kind == DirectionCase::Uncertain);
        const core::DirectionAnalysis right = core::decideDirection(takes, 2);
        CHECK(right.result.verdict.kind == DirectionCase::Uncertain);
        CHECK_FALSE(right.needsRepeat);
    }
    SECTION("back needs the pair repeated before it is called reversed")
    {
        const core::DirectionAnalysis first = core::decideDirection(takes, 3);
        CHECK(first.result.verdict.kind == DirectionCase::Uncertain);
        CHECK(first.needsRepeat);
        CHECK(first.repeatIndex == 0);
        CHECK(first.result.measures.bestOtherAdvantageDb == within(15.0, 1e-9));
        // 亮度不参与：背面更亮也照样判
        const core::DirectionAnalysis confirmed = core::decideDirection(takes, 3, core::DirectionRepeat{-44.0, -31.0});
        CHECK(confirmed.result.verdict.kind == DirectionCase::MaybeReversed);
        CHECK(core::directionIsPriority(confirmed));
        const core::DirectionAnalysis mild = core::decideDirection(takes, 3, core::DirectionRepeat{-40.0, -32.0});
        CHECK(mild.result.verdict.kind == DirectionCase::MaybeReversed);
        CHECK_FALSE(core::directionIsPriority(mild)); // 重测只高 8 dB，不到进“最该改”的门槛
        const core::DirectionAnalysis notAgain = core::decideDirection(takes, 3, core::DirectionRepeat{-35.0, -31.0});
        CHECK(notAgain.result.verdict.kind == DirectionCase::Uncertain);
        CHECK_FALSE(core::directionIsPriority(notAgain));
    }
    SECTION("four directions within the small difference")
    {
        CHECK(core::decideDirection(takesAt({-30.0, -31.0, -32.5, -31.5}), 2).result.verdict.kind ==
              DirectionCase::SmallDifference);
    }
    SECTION("an unknown current direction is not guessed")
    {
        for (int index : {-1, 4})
        {
            const core::DirectionAnalysis d = core::decideDirection(takes, index);
            CHECK(d.result.verdict.kind == DirectionCase::Uncertain);
            CHECK(d.result.confidence.reason == core::RetestReason::Ambiguous);
            CHECK(d.result.measures.levelDb[3] == within(-45.0, 1e-9));
        }
    }
    SECTION("a missing take asks for a retest")
    {
        auto partial = takes;
        partial[2].active = false;
        const core::DirectionAnalysis d = core::decideDirection(partial, 0);
        CHECK(d.result.verdict.kind == DirectionCase::Uncertain);
        CHECK(d.result.confidence.level == core::Reliability::Retest);
    }
}

TEST_CASE("decision table: every cell")
{
    using core::DrBand;
    using core::ReverbBand;
    using core::RoomAdvice;
    struct Cell
    {
        ReverbBand t20;
        DrBand dr;
        RoomAdvice advice;
        bool priority;
    };
    const Cell cells[] = {
        {ReverbBand::Boomy, DrBand::Low, RoomAdvice::ChangeRoomAndMoveCloser, true},
        {ReverbBand::Normal, DrBand::Low, RoomAdvice::MoveCloserCheckDirection, true},
        {ReverbBand::VeryBoomy, DrBand::High, RoomAdvice::KeepCloseDontBackOff, false},
        {ReverbBand::Normal, DrBand::High, RoomAdvice::Good, false},
        {ReverbBand::Normal, DrBand::Mid, RoomAdvice::FineNotPriority, false},
        {ReverbBand::Boomy, DrBand::Mid, RoomAdvice::MoveCloserThenTreat, true},
        // 干也算正常
        {ReverbBand::Dry, DrBand::Low, RoomAdvice::MoveCloserCheckDirection, true},
        {ReverbBand::VeryBoomy, DrBand::Low, RoomAdvice::ChangeRoomAndMoveCloser, true},
    };
    for (const Cell& c : cells)
    {
        INFO("T20 " << toString(c.t20) << " D/R " << toString(c.dr));
        const core::RoomDecision d = core::decideRoom(reverbOf(c.t20), drOf(c.dr));
        CHECK(d.advice == c.advice);
        CHECK(d.priority == c.priority);
    }
    CHECK(core::decideRoom(std::nullopt, drOf(DrBand::Low)).advice == RoomAdvice::Unknown);
    CHECK(core::decideRoom(reverbOf(ReverbBand::Normal), std::nullopt).advice == RoomAdvice::Unknown);
    CHECK(core::decideRoom(reverbOf(ReverbBand::Normal, false), drOf(DrBand::Low)).advice == RoomAdvice::Unknown);
    CHECK(core::decideRoom(reverbOf(ReverbBand::Normal), drOf(DrBand::Low, false)).advice == RoomAdvice::Unknown);
}

TEST_CASE("distance ruler from synthetic evidence")
{
    using core::DistanceAdvice;
    using core::DistanceCase;
    const auto decide = [](core::DistanceEvidence e) { return core::decideDistance(e); };
    core::DistanceEvidence none;
    none.plosive = none.sibilance = none.click = none.lowDirectRatio = none.lowSnr = EvidenceLevel::None;

    CHECK(decide({}).result.verdict.kind == DistanceCase::Unknown);
    CHECK(decide(none).result.verdict.kind == DistanceCase::Fine);

    auto closeStrong = none;
    closeStrong.click = EvidenceLevel::Strong;
    const core::DistanceDecision tooClose = decide(closeStrong);
    CHECK(tooClose.result.verdict.kind == DistanceCase::TooClose);
    CHECK(tooClose.advice == DistanceAdvice::BackOffALittle);
    CHECK(tooClose.result.confidence.level == core::Reliability::Trusted);
    CHECK(tooClose.result.measures.closeEvidence == 1);

    closeStrong.clickStillHeavyAfterBackingOff = true;
    CHECK(decide(closeStrong).advice == DistanceAdvice::DrinkWater);

    auto closeWeak = none;
    closeWeak.plosive = EvidenceLevel::Weak;
    closeWeak.clickStillHeavyAfterBackingOff = true; // 口水音不重时这条不起作用
    const core::DistanceDecision weak = decide(closeWeak);
    CHECK(weak.result.verdict.kind == DistanceCase::TooClose);
    CHECK(weak.advice == DistanceAdvice::BackOffALittle);
    CHECK(weak.result.confidence.level == core::Reliability::Retest);

    auto far = none;
    far.lowDirectRatio = EvidenceLevel::Strong;
    CHECK(decide(far).result.verdict.kind == DistanceCase::TooFar);
    CHECK(decide(far).advice == DistanceAdvice::MoveCloser);
    far.lowDirectRatio = EvidenceLevel::Weak;
    CHECK(decide(far).result.confidence.level == core::Reliability::Retest);
    far.lowSnr = EvidenceLevel::Weak; // 次要证据把弱的 D/R 证据抬成可信
    CHECK(decide(far).result.confidence.level == core::Reliability::Trusted);
    CHECK(decide(far).result.measures.farEvidence == 2);

    auto snrOnly = none;
    snrOnly.lowSnr = EvidenceLevel::Strong; // SNR 低单独不算太远
    CHECK(decide(snrOnly).result.verdict.kind == DistanceCase::Fine);
    snrOnly.sibilance = EvidenceLevel::Strong;
    CHECK(decide(snrOnly).result.verdict.kind == DistanceCase::TooClose);

    auto both = none;
    both.sibilance = EvidenceLevel::Weak;
    both.lowDirectRatio = EvidenceLevel::Strong;
    CHECK(decide(both).result.verdict.kind == DistanceCase::Conflicting);
    CHECK(decide(both).advice == DistanceAdvice::FixRoomOrDirectionFirst);

    CHECK(core::evidenceOf(std::nullopt) == EvidenceLevel::NotMeasured);
    CHECK(core::evidenceOf(core::Severity::Light) == EvidenceLevel::None);
    CHECK(core::evidenceOf(core::Severity::Moderate) == EvidenceLevel::Weak);
    CHECK(core::evidenceOf(core::Severity::Heavy) == EvidenceLevel::Strong);
}

TEST_CASE("where the direction test is offered")
{
    using core::DirectionPrompt;
    using core::MicType;
    core::DirectionPromptInput in;
    CHECK(core::decideDirectionPrompt(in) == DirectionPrompt::CardButton);
    in.room = core::RoomAdvice::MoveCloserCheckDirection;
    CHECK(core::decideDirectionPrompt(in) == DirectionPrompt::MainButton);
    in.room = core::RoomAdvice::Good;
    in.distance = core::DistanceCase::Conflicting;
    CHECK(core::decideDirectionPrompt(in) == DirectionPrompt::ActionWithButton);
    in.distance = core::DistanceCase::Fine;
    in.gainLowOrNoiseBad = true;
    for (MicType m : {MicType::Usb, MicType::Condenser, MicType::Unknown})
    {
        in.mic = m;
        CHECK(core::decideDirectionPrompt(in) == DirectionPrompt::ActionWithButton);
    }
    in.mic = MicType::Dynamic; // 动圈本来就要较大增益
    CHECK(core::decideDirectionPrompt(in) == DirectionPrompt::CardButton);
    in.directionTested = true;
    CHECK(core::decideDirectionPrompt(in) == DirectionPrompt::None);
    in.directionStale = true;
    CHECK(core::decideDirectionPrompt(in) == DirectionPrompt::Stale);
    in.mic = MicType::Headset;
    CHECK(core::decideDirectionPrompt(in) == DirectionPrompt::None);
}

TEST_CASE("room thresholds keep their order")
{
    CHECK(thr::reverb::repeat_min_kept >= 3.0);
    CHECK((thr::reverb::repeat_outlier_frac > 0.0 && thr::reverb::repeat_outlier_frac < 1.0));
    CHECK(thr::reverb::gate_margin_db > thr::reverb::gate_tail_drop_db);
    CHECK(thr::clap::noise_window_ms[0] < thr::clap::noise_window_ms[1]);
    CHECK(thr::clap::onset_gap_ms < thr::clap::onset_lookback_ms); // 否则起音窗口为空，过滤悄悄失效
    CHECK(-thr::clap::noise_window_ms[0] <= thr::clap::segment_before_s * 1000.0);
    CHECK(thr::reverb::gate_search_from_ms >= thr::direct_ratio::direct_window_end_ms);
}
