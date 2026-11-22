// SPDX-License-Identifier: GPL-3.0-or-later
//
// 方向的测量与判断、房间建议的每一档、距离标尺（用合成的读数）、方向测试的提示方式。
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
core::DistanceReading at(double value, bool lowerBound = false)
{
    return {true, value, lowerBound};
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

TEST_CASE("room advice reads T20 only")
{
    using core::ReverbBand;
    using core::RoomAdvice;
    struct Case
    {
        ReverbBand t20;
        RoomAdvice advice;
        bool priority;
    };
    const Case cases[] = {
        {ReverbBand::Dry, RoomAdvice::Good, false},
        {ReverbBand::Normal, RoomAdvice::Good, false},
        {ReverbBand::Boomy, RoomAdvice::MoveCloserThenTreat, true},
        {ReverbBand::VeryBoomy, RoomAdvice::ChangeRoomAndMoveCloser, true},
    };
    for (const Case& c : cases)
    {
        INFO("T20 " << toString(c.t20));
        const core::RoomDecision d = core::decideRoom(reverbOf(c.t20));
        CHECK(d.advice == c.advice);
        CHECK(d.priority == c.priority);
    }
    CHECK(core::decideRoom(std::nullopt).advice == RoomAdvice::Unknown);
    const core::RoomDecision retest = core::decideRoom(reverbOf(ReverbBand::VeryBoomy, false));
    CHECK(retest.advice == RoomAdvice::Unknown);
    CHECK_FALSE(retest.priority);
}

TEST_CASE("distance ruler: the first test only says maybe too far")
{
    using core::DistanceCase;
    core::DistanceEvidence e;
    CHECK(core::decideDistance(e).verdict.kind == DistanceCase::Unknown);
    e.snrBad = true; // 没有 T20：不提示
    CHECK(core::decideDistance(e).verdict.kind == DistanceCase::Unknown);
    e.tMidSec = 0.3;
    const core::DistanceResult hint = core::decideDistance(e);
    CHECK(hint.verdict.kind == DistanceCase::MaybeTooFar);
    CHECK(hint.confidence.level == core::Reliability::Trusted);
    CHECK(core::distanceAdviceOf(hint) == core::DistanceAdvice::MoveCloser);
    CHECK(hint.measures.empiricalLimitM == within(core::empiricalDistanceLimitM(0.3), 1e-12));
    e.snrBad = false; // SNR 不差：只给经验上限，不提示
    const core::DistanceResult quiet = core::decideDistance(e);
    CHECK(quiet.verdict.kind == DistanceCase::Unknown);
    CHECK(quiet.measures.empiricalLimitM > 0.0);
    CHECK(core::distanceAdviceOf(quiet) == core::DistanceAdvice::None);
    // 第一次测试没有上次：三项都是没比
    CHECK(quiet.verdict.drSign == core::DistanceSign::NotCompared);
    CHECK(quiet.verdict.plosiveSign == core::DistanceSign::NotCompared);
    CHECK(quiet.verdict.sibilanceSign == core::DistanceSign::NotCompared);
}

TEST_CASE("the empirical distance limit follows the critical distance and shrinks with T20")
{
    namespace d = thr::distance;
    const double t = 0.4;
    const double critical = d::critical_distance_coef * std::sqrt(d::source_directivity * d::room_volume_m3 / t);
    CHECK(core::empiricalDistanceLimitM(t) == within(d::limit_critical_frac * critical, 1e-12));
    // T20 变成四倍，上限减半
    CHECK(core::empiricalDistanceLimitM(4.0 * t) == within(0.5 * core::empiricalDistanceLimitM(t), 1e-12));
    CHECK(core::empiricalDistanceLimitM(0.0) == 0.0);
    CHECK(core::empiricalDistanceLimitM(-1.0) == 0.0);
}

TEST_CASE("distance ruler: a retest compares D/R, plosives and sibilance with last time")
{
    using core::DistanceCase;
    using core::DistanceSign;
    namespace d = thr::distance;
    const double dr = d::dr_resolution_db;
    const double pl = d::plosive_resolution_db;
    const double sb = d::sibilance_resolution_db;
    core::DistanceEvidence e;
    e.drBefore = at(10.0);
    e.plosiveBefore = at(5.0);
    e.sibilanceBefore = at(-40.0);
    e.snrBad = true; // 有了前后对比，弱提示让位
    e.tMidSec = 0.3;

    SECTION("everything moved the same way")
    {
        e.drNow = at(10.0 + dr + 1.0);
        e.plosiveNow = at(5.0 + pl + 1.0);
        e.sibilanceNow = at(-40.0 + sb + 1.0);
        const core::DistanceResult r = core::decideDistance(e);
        CHECK(r.verdict.kind == DistanceCase::Closer);
        CHECK(r.measures.closeEvidence == 3);
        CHECK(r.measures.farEvidence == 0);
        CHECK(r.measures.drDeltaDb == within(dr + 1.0, 1e-12));
        CHECK(r.measures.sibilanceDeltaDb == within(sb + 1.0, 1e-12));
        CHECK(core::distanceAdviceOf(r) == core::DistanceAdvice::None);

        e.drNow = at(10.0 - dr - 1.0);
        e.plosiveNow = at(5.0 - pl - 1.0);
        e.sibilanceNow = at(-40.0 - sb - 1.0);
        CHECK(core::decideDistance(e).verdict.kind == DistanceCase::Farther);
        CHECK(core::decideDistance(e).measures.farEvidence == 3);
    }
    SECTION("changes within the resolution are no change; one sign is enough")
    {
        e.drNow = at(10.0 + 0.5 * dr);
        e.plosiveNow = at(5.0 - 0.5 * pl);
        e.sibilanceNow = at(-40.0 + 0.5 * sb);
        const core::DistanceResult same = core::decideDistance(e);
        CHECK(same.verdict.kind == DistanceCase::NoChange);
        CHECK(same.verdict.drSign == DistanceSign::NoChange);
        CHECK(same.measures.drDeltaDb == within(0.5 * dr, 1e-12));
        e.drNow = at(10.0 + dr + 1.0);
        CHECK(core::decideDistance(e).verdict.kind == DistanceCase::Closer);
    }
    SECTION("signs pointing both ways are conflicting")
    {
        e.drNow = at(10.0 - dr - 1.0);
        e.sibilanceNow = at(-40.0 + sb + 1.0);
        const core::DistanceResult r = core::decideDistance(e);
        CHECK(r.verdict.kind == DistanceCase::Conflicting);
        CHECK(r.measures.closeEvidence == 1);
        CHECK(r.measures.farEvidence == 1);
        CHECK(r.verdict.plosiveSign == DistanceSign::NotCompared); // 这次没有喷麦读数
        CHECK(r.confidence.level == core::Reliability::Trusted);
    }
    SECTION("a gain change voids sibilance only; a room change voids all three")
    {
        e.drNow = at(10.0);
        e.sibilanceNow = at(-40.0 + sb + 10.0);
        e.gainChanged = true;
        core::DistanceResult r = core::decideDistance(e);
        CHECK(r.verdict.sibilanceSign == DistanceSign::GainChanged);
        CHECK(r.measures.sibilanceDeltaDb == 0.0);
        CHECK(r.verdict.drSign == DistanceSign::NoChange);
        CHECK(r.verdict.kind == DistanceCase::NoChange);
        e.roomChanged = true;
        r = core::decideDistance(e);
        CHECK(r.verdict.drSign == DistanceSign::RoomChanged);
        CHECK(r.verdict.sibilanceSign == DistanceSign::RoomChanged);
        // 缺读数的项仍是 not-compared：没有可比的两次，谈不上被换房间作废（比如第一次位置步没有上次）
        CHECK(r.verdict.plosiveSign == DistanceSign::NotCompared);
        CHECK(r.measures.drDeltaDb == 0.0);
        CHECK(r.verdict.kind == DistanceCase::MaybeTooFar); // 比不了，回到第一次的弱提示
    }
    SECTION("a lower bound only rules out one direction")
    {
        // 上次只知道下限：真实的变化只会更小，“更近”说不出，“更远”照样成立
        e.drBefore = at(10.0, true);
        e.drNow = at(10.0 + dr + 1.0);
        CHECK(core::decideDistance(e).verdict.drSign == DistanceSign::NotCompared);
        CHECK(core::decideDistance(e).measures.drDeltaDb == 0.0);
        e.drNow = at(10.0 - dr - 1.0);
        CHECK(core::decideDistance(e).verdict.drSign == DistanceSign::Farther);
        e.drNow = at(10.0);
        CHECK(core::decideDistance(e).verdict.drSign == DistanceSign::NotCompared);
        // 这次只知道下限：真实的变化只会更大
        e.drBefore = at(10.0);
        e.drNow = at(10.0 + dr + 1.0, true);
        CHECK(core::decideDistance(e).verdict.drSign == DistanceSign::Closer);
        e.drNow = at(10.0 - dr - 1.0, true);
        CHECK(core::decideDistance(e).verdict.drSign == DistanceSign::NotCompared);
    }
}

TEST_CASE("where the direction test is offered")
{
    using core::DirectionPrompt;
    using core::MicType;
    core::DirectionPromptInput in;
    CHECK(core::decideDirectionPrompt(in) == DirectionPrompt::CardButton);
    // T20 干或正常 + 可能离远了：主按钮；房间偏混或没有 T20 时不是
    in.room = core::RoomAdvice::Good;
    in.distance = core::DistanceCase::MaybeTooFar;
    CHECK(core::decideDirectionPrompt(in) == DirectionPrompt::MainButton);
    in.room = core::RoomAdvice::MoveCloserThenTreat;
    CHECK(core::decideDirectionPrompt(in) == DirectionPrompt::CardButton);
    in.room = core::RoomAdvice::Unknown;
    CHECK(core::decideDirectionPrompt(in) == DirectionPrompt::CardButton);
    in.room = core::RoomAdvice::Good;
    in.distance = core::DistanceCase::Conflicting;
    CHECK(core::decideDirectionPrompt(in) == DirectionPrompt::ActionWithButton);
    in.distance = core::DistanceCase::Closer;
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
    CHECK(thr::distance::dr_resolution_db > 0.0);
    CHECK(thr::distance::plosive_resolution_db > 0.0);
    CHECK(thr::distance::sibilance_resolution_db > 0.0);
    CHECK((thr::distance::limit_critical_frac > 0.0 && thr::distance::limit_critical_frac <= 1.0));
}
