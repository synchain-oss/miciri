// SPDX-License-Identifier: GPL-3.0-or-later
//
// 手动开始与结束（PhaseControl::Manual，不起线程，直接喂样本）：第一段随这一步开始，之后每段等 begin()；
// 收够最低量才能 end()，到时长上限照样自动结束；开始与结束处的点击保护不进分析；阶段重来回到待开始。
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include "config/Thresholds.h"
#include "core/level/Synth.h"
#include "core/room/ClapSynth.h"
#include "core/voice/VoiceSynth.h"
#include "hum/Hum.h"
#include "noise/Noise.h"
#include "session/Steps.h"

using namespace miciri;
using namespace miciri::testing;
using core::FlowNotice;
using core::PhaseNeed;
using core::StepId;
using core::StepPhase;

namespace
{
constexpr double kFs = 48000.0;
const double kGuard = thr::session::click_guard_s;

session::StepContext manual(double fs = kFs)
{
    session::StepContext c;
    c.sampleRate = fs;
    c.control = session::PhaseControl::Manual;
    return c;
}

std::vector<double> silence(double seconds, double fs = kFs)
{
    return std::vector<double>(static_cast<std::size_t>(std::llround(fs * seconds)), 0.0);
}

// 分成不规则的小块喂，模拟宿主的回调；until 成立就停
template<class Until>
void feedUntil(session::StepScript& s, const std::vector<double>& x, Until until, std::size_t block = 517)
{
    for (std::size_t i = 0; i < x.size() && !s.done() && !until(); i += block)
        s.feed(x.data() + i, nullptr, std::min(block, x.size() - i));
}

void feedAll(session::StepScript& s, const std::vector<double>& x, std::size_t block = 517)
{
    feedUntil(s, x, [] { return false; }, block);
}

// 安静步里一下响亮的点击（像鼠标），在 atSec 处
std::vector<double> quietWithClick(double seconds, double atSec)
{
    std::vector<double> x = whiteNoise(static_cast<std::size_t>(seconds * kFs), 1e-4, 5);
    addTick(x, kFs, atSec, 0.5);
    return x;
}

// 一口气数数：0.4 s 的元音、0.1 s 的停顿交替，开头留 leadSec 只有底噪（点击保护）
std::vector<double> counting(double seconds, double amplitude, double leadSec)
{
    std::vector<double> x(static_cast<std::size_t>(seconds * kFs), 0.0);
    int n = 0;
    for (double t = leadSec; t + 0.4 <= seconds; t += 0.5, ++n)
        addVowel(x, kFs, t, 0.4, 120.0 + 15.0 * (n % 5), amplitude, 0.03);
    mix(x, whiteNoise(x.size(), 1e-4, 8)); // 停顿里是底噪：数字零不算安静的底
    return x;
}

// 一串轻拍（ClapTrain 的直达加尾巴），间隔 1.5 s；clipped 里列出的几下（从 0 起）在直达处削波
std::vector<double> softTaps(int taps, std::vector<int> clipped = {})
{
    ClapTrain c;
    c.fs = kFs;
    c.claps = taps;
    c.leadSec = 0.6;
    c.gapSec = 1.5;
    c.direct = 0.2;
    std::vector<double> x = makeClaps(c);
    for (int i : clipped)
    {
        const auto at = static_cast<std::size_t>(clapTime(c, i) * kFs);
        for (std::size_t n = 0; n < 24; ++n)
            x[at + n] = n % 2 ? -1.0 : 1.0;
    }
    return x;
}

// 六个擦音夹在元音之间（像一遍齿音字表）
std::vector<double> sibilantList()
{
    std::vector<double> x = whiteNoise(static_cast<std::size_t>(8.0 * kFs), 3e-5, 21);
    for (int i = 0; i < 6; ++i)
    {
        const double t = 0.5 + 1.2 * i;
        addNoiseBurst(x, kFs, t, 0.15, 0.05, 100 + static_cast<std::uint64_t>(i));
        addVowel(x, kFs, t + 0.2, 0.3, 140.0, 0.3);
    }
    return x;
}
} // namespace

TEST_CASE("manual: the first segment starts with the step, the click guard is dropped, and end needs the minimum")
{
    auto s = session::makeScript(StepId::Quiet, manual(), nullptr);
    CHECK_FALSE(s->live().waiting);
    CHECK_FALSE(s->live().canEnd);
    CHECK(s->live().need == PhaseNeed::Seconds);
    const double minSec = s->live().needLeft;
    CHECK(minSec > 0.0);
    CHECK(minSec < thr::session::quiet_s);

    // 开始那一下的点击落在保护段里：丢掉，不计进度
    feedAll(*s, silence(kGuard));
    CHECK(s->progressSec() == Catch::Approx(0.0));
    feedAll(*s, silence(2.0));
    CHECK(s->live().phaseElapsedSec == Catch::Approx(2.0).margin(1e-6));
    CHECK(s->progressSec() == Catch::Approx(2.0 - kGuard).margin(1e-6)); // 最后这一小段还在延迟里
    CHECK_FALSE(s->live().canEnd);
    CHECK(s->live().needLeft == Catch::Approx(minSec - (2.0 - kGuard)).margin(1e-6));
    CHECK_FALSE(s->end()); // 没收够：什么都不改
    CHECK_FALSE(s->done());
    CHECK_FALSE(s->begin()); // 正在进行的段不能再开始

    // 收够了才能结束；结束时延迟里的那一小段（用户点“结束”的点击）不进分析
    feedUntil(*s, silence(minSec), [&] { return s->live().canEnd; }, 480);
    REQUIRE(s->live().canEnd);
    CHECK(s->live().needLeft == 0.0);
    const double analysed = s->progressSec();
    CHECK(analysed == Catch::Approx(minSec).margin(480.0 / kFs));
    REQUIRE(s->end());
    CHECK(s->done());
    // 最后一段结束了：没有可以开始或结束的段
    CHECK_FALSE(s->live().canEnd);
    CHECK_FALSE(s->live().waiting);
    CHECK(s->live().need == PhaseNeed::None);
    CHECK(s->live().needLeft == 0.0);
    CHECK(s->live().heard == 0.0);
    CHECK_FALSE(s->end());
    CHECK(s->progressSec() == Catch::Approx(analysed));
    const auto out = s->finish();
    REQUIRE(out.results.noise.has_value());
    REQUIRE(out.results.hum.has_value());
    CHECK(out.results.noise->confidence.reason != core::RetestReason::TooShort);
    CHECK(out.results.hum->confidence.reason != core::RetestReason::TooShort);
}

TEST_CASE("manual: the quiet minimum is exactly what the noise floor and the hum search need, at any rate")
{
    for (const double fs : {44100.0, 48000.0, 96000.0})
    {
        auto s = session::makeScript(StepId::Quiet, manual(fs), nullptr);
        const double minSec = s->live().needLeft;
        auto trustedLength = [fs](double seconds) {
            const auto x = whiteNoise(static_cast<std::size_t>(std::llround(seconds * fs)), 1e-3, 9);
            core::NoiseAnalyser noise(fs);
            core::HumAnalyser hum(fs);
            noise.push(x.data(), x.size());
            hum.push(x.data(), x.size());
            return noise.finish().confidence.reason != core::RetestReason::TooShort &&
                   hum.finish().confidence.reason != core::RetestReason::TooShort;
        };
        CHECK(trustedLength(minSec));
        CHECK_FALSE(trustedLength(minSec - 0.01)); // 少一块就不够：下限没有多给
    }
}

TEST_CASE("manual: a mouse click at the start or at the end of a segment is not analysed")
{
    // 对照：自动推进时开头的点击照样进分析，最响一帧就是它
    session::StepContext automatic = manual();
    automatic.control = session::PhaseControl::Auto;
    auto a = session::makeScript(StepId::Quiet, automatic, nullptr);
    feedAll(*a, quietWithClick(thr::session::quiet_s, 0.1));
    REQUIRE(a->done());
    const double withClick = a->finish().results.noise->measures.noisePeakDbfs;

    auto s = session::makeScript(StepId::Quiet, manual(), nullptr);
    std::vector<double> x = quietWithClick(6.0, 0.1); // 开始后 0.1 s 的点击
    const double endAt = 5.5;
    addTick(x, kFs, endAt - 0.1, 0.5); // 点“结束”之前 0.1 s 的点击
    x.resize(static_cast<std::size_t>(endAt * kFs));
    feedAll(*s, x, 480);
    REQUIRE(s->live().canEnd);
    REQUIRE(s->end());
    const double without = s->finish().results.noise->measures.noisePeakDbfs;
    CHECK(without < withClick - 20.0);
}

TEST_CASE("manual: a segment still ends by itself at its cap, with the delayed tail kept")
{
    auto s = session::makeScript(StepId::Gain, manual(), nullptr);
    CHECK(s->live().need == PhaseNeed::VoiceSeconds);
    feedAll(*s, silence(kGuard));
    feedAll(*s, voice(kFs, thr::session::gain_s, 0.1, 3));
    REQUIRE(s->done());
    CHECK(s->progressSec() == Catch::Approx(thr::session::gain_s)); // 到上限时延迟里的那段照常收下
    const auto out = s->finish();
    REQUIRE(out.results.gain.has_value());
    CHECK(out.speechLevelDbfsA.has_value());
}

TEST_CASE("manual: ending the gain take early keeps the reading and its confidence")
{
    auto s = session::makeScript(StepId::Gain, manual(), nullptr);
    const auto x = voice(kFs, thr::session::gain_s, fromDb(-15.0), 4, 0.5);
    feedUntil(*s, x, [&] { return s->live().canEnd; }, 480);
    REQUIRE(s->live().canEnd);
    CHECK(s->live().heard >= thr::session::gain_min_voice_s);
    // 够了之后 heard 停在够了的那一刻，canEnd 不会收回
    const double heard = s->live().heard;
    REQUIRE(s->progressSec() + 0.2 < thr::session::gain_s);
    feedAll(*s, voice(kFs, 0.2, fromDb(-15.0), 7));
    CHECK(s->live().heard == heard);
    CHECK(s->live().canEnd);
    CHECK(s->live().needLeft == 0.0);
    REQUIRE(s->end());
    CHECK(s->progressSec() < thr::session::gain_s);
    const auto out = s->finish();
    REQUIRE(out.results.gain.has_value());
    CHECK(out.results.gain->confidence.level == core::Reliability::Trusted);
    CHECK(out.speechLevelDbfsA.has_value());
}

TEST_CASE(
    "manual: the room step ends early only with enough valid claps, and still stops by itself once there are enough")
{
    ClapTrain c;
    c.fs = kFs;
    c.claps = 8;
    const auto claps = makeClaps(c);
    const auto minValid = static_cast<double>(thr::reverb::repeat_min_kept);
    {
        auto s = session::makeScript(StepId::Room, manual(), nullptr);
        CHECK(s->live().need == PhaseNeed::Claps);
        feedUntil(*s, claps, [&] { return s->live().heard >= minValid - 1.0; }, 480);
        CHECK_FALSE(s->live().canEnd);
        CHECK_FALSE(s->end());
        feedUntil(*s, claps, [&] { return s->live().canEnd; }, 480);
        REQUIRE(s->live().canEnd);
        CHECK(s->live().heard == Catch::Approx(minValid));
        REQUIRE(s->end());
        CHECK(s->done());
        const auto out = s->finish();
        REQUIRE(out.results.reverb.has_value());
        CHECK(out.results.reverb->confidence.level == core::Reliability::Trusted);
    }
    {
        auto s = session::makeScript(StepId::Room, manual(), nullptr);
        feedAll(*s, claps, 480);
        REQUIRE(s->done());
        CHECK(std::count(s->live().claps.begin(), s->live().claps.end(), core::ClapState::Valid) ==
              static_cast<long>(thr::session::room_claps_needed));
    }
}

TEST_CASE("manual: the position step waits between phases, reads each list in one go and ends it early")
{
    auto s = session::makeScript(StepId::Position, manual(), nullptr);
    CHECK(s->live().phase == StepPhase::SoftClap);
    CHECK(s->live().rounds == 1); // 几下轻拍合成一段
    CHECK(s->live().need == PhaseNeed::Claps);

    ClapTrain soft;
    soft.fs = kFs;
    soft.claps = static_cast<int>(thr::session::soft_claps);
    soft.leadSec = 0.6;
    soft.gapSec = 1.5;
    soft.direct = 0.2;
    feedUntil(*s, makeClaps(soft), [&] { return s->live().canEnd; }, 480);
    REQUIRE(s->live().canEnd);
    CHECK(s->live().heard >= thr::direct_ratio::min_clean_claps);
    REQUIRE(s->end());

    // 爆破音字表在等开始：这时的音频丢掉
    CHECK(s->live().phase == StepPhase::Plosive);
    CHECK(s->live().waiting);
    CHECK(s->live().rounds == 1);
    CHECK(s->live().need == PhaseNeed::Syllables);
    CHECK_FALSE(s->end());
    const double before = s->progressSec();
    feedAll(*s, voice(kFs, 3.0, 0.1, 5));
    CHECK(s->progressSec() == Catch::Approx(before));
    CHECK(s->live().waiting);
    REQUIRE(s->begin());
    CHECK_FALSE(s->live().waiting);
    CHECK_FALSE(s->begin());

    const std::vector<double> pops{-200, 0, -200, 0, -200, 0};
    std::vector<double> list = silence(kGuard);
    const auto words = wordList(kFs, pops);
    list.insert(list.end(), words.begin(), words.end());
    const auto tail = silence(1.5);
    list.insert(list.end(), tail.begin(), tail.end());
    feedUntil(*s, list, [&] { return s->live().canEnd; }, 480);
    REQUIRE(s->live().canEnd);
    CHECK(s->live().heard >= thr::session::word_list_min_events);
    REQUIRE(s->end());
    CHECK(s->progressSec() - before < thr::session::word_list_rounds * thr::session::word_list_round_s);

    CHECK(s->live().phase == StepPhase::Sibilant);
    REQUIRE(s->begin());
    std::vector<double> sib = silence(kGuard);
    const auto hiss = sibilantList();
    sib.insert(sib.end(), hiss.begin(), hiss.end());
    feedUntil(*s, sib, [&] { return s->live().canEnd; }, 480);
    REQUIRE(s->live().canEnd);
    REQUIRE(s->end());

    // 自然念：手动开始时念 natural_read_s 秒（Auto 仍是 click.analysis_window_s），有声秒数没够就念到上限，自动结束
    CHECK(s->live().phase == StepPhase::Natural);
    CHECK(s->live().need == PhaseNeed::VoiceSeconds);
    CHECK(s->live().phaseDurationSec == Catch::Approx(thr::session::natural_read_s).margin(1e-3));
    REQUIRE(s->begin());
    feedAll(*s, voice(kFs, kGuard + thr::session::natural_read_s + 1.0, 0.1, 6, kGuard));
    REQUIRE(s->done());
    const auto out = s->finish();
    REQUIRE(out.results.plosive.has_value());
    CHECK(out.results.plosive->measures.events >= thr::session::word_list_min_events);
    CHECK(out.results.plosive->confidence.level == core::Reliability::Trusted);
    REQUIRE(out.results.sibilance.has_value());
    CHECK(out.results.sibilance->measures.events >= thr::session::word_list_min_events);
    REQUIRE(out.results.click.has_value());
    REQUIRE(out.results.directRatio.has_value());
}

TEST_CASE("manual: a phase that has to be redone goes back to waiting with its notice, without a countdown")
{
    auto s = session::makeScript(StepId::Position, manual(), nullptr);
    ClapTrain c;
    c.fs = kFs;
    c.claps = static_cast<int>(thr::session::soft_claps);
    c.leadSec = 0.6;
    c.gapSec = thr::session::soft_clap_round_s;
    std::vector<double> clipped = makeClaps(c);
    for (int i = 0; i < c.claps; ++i)
    {
        const auto at = static_cast<std::size_t>(clapTime(c, i) * kFs);
        for (std::size_t n = 0; n < 24; ++n)
            clipped[at + n] = n % 2 ? -1.0 : 1.0;
    }
    // 三下都削波：最低量永远不够，到上限收尾时要求重拍
    clipped.resize(static_cast<std::size_t>((kGuard + thr::session::soft_clap_max_s) * kFs) + 1);
    feedUntil(*s, clipped, [&] { return s->live().notice != FlowNotice::None; });
    REQUIRE(s->live().notice == FlowNotice::ClapClipped);
    CHECK(s->live().waiting);
    CHECK(s->live().phase == StepPhase::SoftClap);
    CHECK(s->live().noticeSec == 0.0);
    CHECK(s->progressSec() == Catch::Approx(0.0));
    CHECK(s->live().heard == 0.0);
    feedAll(*s, silence(5.0)); // 不倒数：等多久都还在等
    CHECK(s->live().waiting);
    CHECK(s->live().notice == FlowNotice::ClapClipped);
    REQUIRE(s->begin());
    CHECK(s->live().notice == FlowNotice::None);
    CHECK(s->live().phase == StepPhase::SoftClap);
}

TEST_CASE("manual: soft taps are judged one by one, and enough clean ones move on without the user")
{
    auto s = session::makeScript(StepId::Position, manual(), nullptr);
    CHECK(s->live().phase == StepPhase::SoftClap);
    CHECK(s->live().phaseDurationSec == Catch::Approx(thr::session::soft_clap_max_s));
    CHECK(s->live().taps.empty());

    // 第一下削波：当场判成 clipped、不算数；后面三下干净的凑够 soft_claps 就自己进字表
    const auto taps = softTaps(static_cast<int>(thr::session::soft_claps) + 1, {0});
    std::vector<core::TapState> seen;
    bool couldEnd = false;
    feedUntil(
        *s, taps,
        [&] {
            if (s->live().phase != StepPhase::SoftClap)
                return true;
            seen = s->live().taps;
            couldEnd = couldEnd || s->live().canEnd;
            return false;
        },
        480);
    REQUIRE(seen.size() >= 3);
    CHECK(seen[0] == core::TapState::Clipped);
    CHECK(seen[1] == core::TapState::Valid);
    CHECK(seen[2] == core::TapState::Valid);
    CHECK(couldEnd); // 收够 min_clean_claps 下就能点结束，不必等凑够
    CHECK(s->live().phase == StepPhase::Plosive);
    CHECK(s->live().waiting);
    CHECK(s->live().notice == FlowNotice::None);
    CHECK(s->live().taps.empty());
    CHECK(s->progressSec() < static_cast<double>(taps.size()) / kFs); // 没等上限，也没等这串音频喂完
}

TEST_CASE("manual: soft taps that run out of time carry on with enough clean ones and are redone with too few")
{
    const auto cap = static_cast<std::size_t>((kGuard + thr::session::soft_clap_max_s) * kFs) + 1;
    // 到上限只有一下：D/R 给不出，退回待开始重拍；不是削波刷掉的，提示是 redo
    {
        auto s = session::makeScript(StepId::Position, manual(), nullptr);
        auto x = softTaps(1);
        x.resize(cap);
        feedUntil(*s, x, [&] { return s->live().notice != FlowNotice::None; });
        REQUIRE(s->live().notice == FlowNotice::Redo);
        CHECK(s->live().waiting);
        CHECK(s->live().phase == StepPhase::SoftClap);
        CHECK(s->live().taps.empty());
        CHECK(s->progressSec() == Catch::Approx(0.0));
    }
    // 到上限有 min_clean_claps 下：够 D/R 用，照常进字表
    {
        auto s = session::makeScript(StepId::Position, manual(), nullptr);
        auto x = softTaps(static_cast<int>(thr::direct_ratio::min_clean_claps));
        x.resize(cap);
        feedUntil(*s, x, [&] { return s->live().phase != StepPhase::SoftClap; });
        CHECK(s->live().phase == StepPhase::Plosive);
        CHECK(s->live().waiting);
        CHECK(s->live().notice == FlowNotice::None);
        CHECK(s->progressSec() == Catch::Approx(thr::session::soft_clap_max_s).margin(1e-3));
    }
}

TEST_CASE("manual: each direction is started and ended on its own")
{
    auto s = session::makeScript(StepId::Direction, manual(), nullptr);
    const double take = thr::session::direction_take_s;
    // 第一个方向也等用户点“开始”：按下这一步的“开始”时用户还在电脑前
    for (int d = 0; d < 4; ++d)
    {
        CHECK(s->live().directionIndex == d);
        CHECK(s->live().phase == StepPhase::Direction);
        CHECK(s->live().waiting);
        CHECK_FALSE(s->live().canEnd);
        CHECK(s->live().heard == 0.0); // 上一个方向的有声秒数不带过来
        const double before = s->progressSec();
        feedAll(*s, voice(kFs, 2.0, 0.2, 30)); // 坐回去、转话筒时说的话不算
        CHECK(s->progressSec() == before);
        REQUIRE(s->begin());
        CHECK_FALSE(s->end());
        const double level = d == 2 ? 0.2 : 0.05; // 背面明显更响
        feedUntil(*s, counting(kGuard + take, level, kGuard), [&] { return s->live().canEnd; }, 480);
        REQUIRE(s->live().canEnd); // 有声的秒数够了，不用等满上限
        CHECK(s->live().heard >= thr::session::direction_min_voice_s);
        CHECK(s->live().phaseElapsedSec < take);
        REQUIRE(s->end());
    }
    REQUIRE_FALSE(s->done());
    CHECK(s->live().phase == StepPhase::Confirm);
    CHECK(s->live().waiting);
    CHECK(s->live().directionIndex == 0); // 待开始时就说清先转回当前方向
    for (int r = 0; r < 2; ++r)
    {
        if (r == 1)
            CHECK(s->live().directionIndex == 2); // 第二遍朝更响的背面
        REQUIRE(s->begin());
        feedAll(*s, voice(kFs, kGuard + take + 0.5, r == 0 ? 0.05 : 0.2, 20 + static_cast<std::uint64_t>(r), kGuard));
    }
    REQUIRE(s->done());
    const auto out = s->finish();
    REQUIRE(out.results.direction.has_value());
    CHECK(out.results.direction->verdict.kind == core::DirectionCase::MaybeReversed);
}

TEST_CASE("automatic phases ignore start and end, and report no minimum")
{
    session::StepContext c = manual();
    c.control = session::PhaseControl::Auto;
    auto s = session::makeScript(StepId::Gain, c, nullptr);
    CHECK_FALSE(s->live().waiting);
    CHECK(s->live().need == PhaseNeed::None);
    feedAll(*s, voice(kFs, 4.0, 0.1, 3));
    CHECK_FALSE(s->live().canEnd);
    CHECK_FALSE(s->end());
    CHECK_FALSE(s->begin());
    CHECK(s->progressSec() == Catch::Approx(4.0).margin(1e-6)); // 没有点击保护

    // 轻拍在 Auto 下仍按遍计时，不报逐下的判定：三下的尾巴都收完了、这一段还没结束时核对（换段本来就会清空）
    auto pos = session::makeScript(StepId::Position, c, nullptr);
    CHECK(pos->live().rounds == static_cast<int>(thr::session::soft_claps));
    CHECK(pos->live().phaseDurationSec == Catch::Approx(thr::session::soft_clap_round_s));
    auto taps = softTaps(static_cast<int>(thr::session::soft_claps));
    const double inPhase = thr::session::soft_claps * thr::session::soft_clap_round_s - 0.5;
    taps.resize(std::min(taps.size(), static_cast<std::size_t>(inPhase * kFs)));
    feedAll(*pos, taps);
    CHECK(pos->live().phase == StepPhase::SoftClap);
    CHECK(pos->live().taps.empty());

    // 方向步在 Auto 下第一个方向随这一步开始，不等
    auto dir = session::makeScript(StepId::Direction, c, nullptr);
    CHECK_FALSE(dir->live().waiting);
    CHECK_FALSE(dir->begin());

    // 接通步总是自动推进，Manual 也一样
    auto connect = session::makeScript(StepId::Connect, manual(), nullptr);
    CHECK(connect->live().need == PhaseNeed::None);
    feedAll(*connect, silence(thr::no_signal::low_flat_window_s));
    CHECK(connect->done());
}
