// SPDX-License-Identifier: GPL-3.0-or-later
//
// 各步的编排（不起线程，直接喂样本）、过期规则、距离标尺的未分档处理与快照 JSON 的形状。
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>

#include "config/Thresholds.h"
#include "core/level/Synth.h"
#include "core/room/ClapSynth.h"
#include "plugin/BridgeApi.h"
#include "session/MiniJson.h"
#include "session/Session.h"
#include "session/Steps.h"

using namespace miciri;
using namespace miciri::testing;
using core::FlowNotice;
using core::StepId;
using core::StepPhase;

namespace
{
std::atomic<int> gAnalyses{0};
void countAnalysis() noexcept
{
    gAnalyses.fetch_add(1);
}

session::StepContext mono(double fs = 48000.0)
{
    session::StepContext c;
    c.sampleRate = fs;
    return c;
}

// 分成不规则的小块喂，模拟宿主的回调
void feedAll(session::StepScript& s, const std::vector<double>& x, std::size_t block = 517)
{
    for (std::size_t i = 0; i < x.size() && !s.done(); i += block)
        s.feed(x.data() + i, nullptr, std::min(block, x.size() - i));
}

std::vector<double> silence(double fs, double seconds)
{
    return std::vector<double>(static_cast<std::size_t>(std::llround(fs * seconds)), 0.0);
}

// 一整阶段的轻拍，每一下都削波
std::vector<double> clippedSoftClaps(double fs)
{
    ClapTrain c;
    c.fs = fs;
    c.claps = static_cast<int>(thr::session::soft_claps);
    c.leadSec = 0.6;
    c.gapSec = thr::session::soft_clap_round_s;
    std::vector<double> claps = makeClaps(c);
    for (int i = 0; i < c.claps; ++i)
    {
        const auto at = static_cast<std::size_t>(clapTime(c, i) * fs);
        for (std::size_t n = 0; n < 24; ++n)
            claps[at + n] = n % 2 ? -1.0 : 1.0;
    }
    claps.resize(
        static_cast<std::size_t>(std::llround(thr::session::soft_claps * thr::session::soft_clap_round_s * fs)));
    return claps;
}

template<std::size_t N>
std::vector<std::string> names(const std::array<std::string_view, N>& t)
{
    return {t.begin(), t.end()};
}

template<std::size_t N>
std::vector<std::string> withCommon(const std::array<std::string_view, N>& t)
{
    auto v = names(t);
    for (auto c : bridge::kResultCommon)
        v.emplace_back(c);
    return v;
}
} // namespace

TEST_CASE("planned step lengths come from the thresholds")
{
    using namespace thr::session;
    CHECK(session::plannedSeconds(StepId::Connect) == Catch::Approx(thr::no_signal::low_flat_window_s));
    CHECK(session::plannedSeconds(StepId::Gain) == Catch::Approx(gain_s));
    CHECK(session::plannedSeconds(StepId::Quiet) == Catch::Approx(quiet_s));
    CHECK(session::plannedSeconds(StepId::Room) == Catch::Approx(room_max_s));
    CHECK(session::plannedSeconds(StepId::Position) ==
          Catch::Approx(soft_claps * soft_clap_round_s + 2 * word_list_rounds * word_list_round_s +
                        thr::click::analysis_window_s));
    CHECK(session::plannedSeconds(StepId::Direction) == Catch::Approx(4 * direction_take_s));
}

TEST_CASE("a step ends exactly at its planned sample count, whatever the block size")
{
    const double fs = 44100.0;
    auto s = session::makeScript(StepId::Gain, mono(fs), nullptr);
    const auto planned = static_cast<std::size_t>(std::llround(thr::session::gain_s * fs));
    const auto x = sine(fs, 440.0, fromDb(-9.0), thr::session::gain_s + 1.0);
    std::vector<double> head(x.begin(), x.begin() + static_cast<std::ptrdiff_t>(planned - 1));
    feedAll(*s, head, 333);
    CHECK_FALSE(s->done());
    CHECK(s->live().phase == StepPhase::None);
    CHECK(s->live().phaseElapsedSec == Catch::Approx(static_cast<double>(planned - 1) / fs));
    s->feed(x.data() + planned - 1, nullptr, 2);
    CHECK(s->done());
    CHECK(s->progressSec() == Catch::Approx(thr::session::gain_s));

    gAnalyses = 0;
    auto counted = session::makeScript(StepId::Gain, mono(fs), countAnalysis);
    feedAll(*counted, x);
    const session::StepOutput out = counted->finish();
    REQUIRE(out.results.gain.has_value());
    CHECK(out.results.gain->measures.truePeakDbfsL == Catch::Approx(-9.0).margin(0.1));
    CHECK(gAnalyses > 0);
}

TEST_CASE("position step walks its phases and redoes clipped soft claps after a countdown")
{
    const double fs = 48000.0;
    auto s = session::makeScript(StepId::Position, mono(fs), nullptr);
    CHECK(s->live().phase == StepPhase::SoftClap);
    CHECK(s->live().rounds == static_cast<int>(thr::session::soft_claps));

    // 三下都削波的轻拍：阶段收满时要求重拍
    feedAll(*s, clippedSoftClaps(fs));
    CHECK(s->live().notice == FlowNotice::ClapClipped);
    CHECK(s->live().phase == StepPhase::SoftClap);
    CHECK(s->live().round == 1);
    CHECK(s->live().noticeSec == Catch::Approx(thr::session::notice_s).margin(1e-3));
    CHECK(s->progressSec() == Catch::Approx(0.0));

    // 倒数期间的音频丢掉；倒数完回到第一遍
    feedAll(*s, silence(fs, thr::session::notice_s));
    CHECK(s->live().notice == FlowNotice::None);
    CHECK(s->live().phase == StepPhase::SoftClap);

    // 这次干净地走完，进入爆破音字表
    feedAll(*s, silence(fs, thr::session::soft_claps * thr::session::soft_clap_round_s));
    CHECK(s->live().phase == StepPhase::Plosive);
    CHECK(s->live().round == 1);
    CHECK(s->live().rounds == static_cast<int>(thr::session::word_list_rounds));
    feedAll(*s, voice(fs, 2 * thr::session::word_list_rounds * thr::session::word_list_round_s, 0.1, 3));
    CHECK(s->live().phase == StepPhase::Natural);
    feedAll(*s, voice(fs, thr::click::analysis_window_s, 0.1, 4));
    CHECK(s->done());
    const auto out = s->finish();
    CHECK(out.results.plosive.has_value());
    CHECK(out.results.sibilance.has_value());
    CHECK(out.results.click.has_value());
}

TEST_CASE("a step restarts a phase at most restarts_max times, then carries on and lets the analysis say retest")
{
    const double fs = 48000.0;
    auto s = session::makeScript(StepId::Position, mono(fs), nullptr);
    const auto claps = clippedSoftClaps(fs);
    const auto restarts = static_cast<int>(thr::session::restarts_max);
    for (int i = 0; i < restarts; ++i)
    {
        feedAll(*s, claps);
        REQUIRE(s->live().notice == FlowNotice::ClapClipped);
        feedAll(*s, silence(fs, thr::session::notice_s));
    }
    feedAll(*s, claps);
    CHECK(s->live().notice == FlowNotice::None);
    CHECK(s->live().phase == StepPhase::Plosive);
    feedAll(*s,
            voice(fs,
                  2 * thr::session::word_list_rounds * thr::session::word_list_round_s + thr::click::analysis_window_s,
                  0.1, 7));
    REQUIRE(s->done());
    const auto out = s->finish();
    REQUIRE(out.results.directRatio.has_value());
    CHECK(out.results.directRatio->confidence.level == core::Reliability::Retest);
    CHECK(out.results.directRatio->confidence.reason == core::RetestReason::Clipped);
}

TEST_CASE("room step reports each clap and stops once enough are valid")
{
    const double fs = 48000.0;
    auto s = session::makeScript(StepId::Room, mono(fs), nullptr);
    ClapTrain c;
    c.fs = fs;
    c.claps = 8;
    feedAll(*s, makeClaps(c), 480);
    REQUIRE(s->done());
    const auto valid = std::count(s->live().claps.begin(), s->live().claps.end(), core::ClapState::Valid);
    CHECK(valid == static_cast<long>(thr::session::room_claps_needed));
    CHECK(s->progressSec() < thr::session::room_max_s);
    const auto out = s->finish();
    REQUIRE(out.results.reverb.has_value());
    CHECK(out.results.reverb->confidence.level == core::Reliability::Trusted);
}

TEST_CASE("room step voids a gated set and restarts after the countdown")
{
    const double fs = 48000.0;
    auto s = session::makeScript(StepId::Room, mono(fs), nullptr);
    ClapTrain c;
    c.fs = fs;
    c.claps = static_cast<int>(thr::session::room_claps_max);
    std::vector<double> x = makeClaps(c);
    for (int i = 0; i < c.claps; ++i)
    {
        const auto from = static_cast<std::size_t>((clapTime(c, i) + 0.12) * fs);
        const auto to = std::min(x.size(), static_cast<std::size_t>((clapTime(c, i) + 1.0) * fs));
        for (std::size_t n = from; n < to; ++n)
            x[n] = 0.0;
    }
    x.resize(static_cast<std::size_t>(thr::session::room_max_s * fs), 0.0);
    // 喂到出现提示为止：拍满上限会提前收尾，否则到最长时长收尾
    for (std::size_t i = 0; i < x.size() && s->live().notice != FlowNotice::Gated; i += 480)
        s->feed(x.data() + i, nullptr, std::min<std::size_t>(480, x.size() - i));
    CHECK_FALSE(s->done());
    REQUIRE(s->live().notice == FlowNotice::Gated);
    CHECK(std::count(s->live().claps.begin(), s->live().claps.end(), core::ClapState::Truncated) > 0);
    feedAll(*s, silence(fs, thr::session::notice_s));
    CHECK(s->live().notice == FlowNotice::None);
    CHECK(s->live().claps.empty());
}

TEST_CASE("direction step adds the confirming pair when another direction is clearly louder")
{
    const double fs = 48000.0;
    gAnalyses = 0;
    auto s = session::makeScript(StepId::Direction, mono(fs), countAnalysis);
    const double take = thr::session::direction_take_s;
    for (int d = 0; d < 4; ++d)
    {
        CHECK(s->live().directionIndex == d);
        const double level = d == 2 ? 0.2 : 0.05; // 背面明显更响
        feedAll(*s, voice(fs, take, level, 10 + static_cast<std::uint64_t>(d)));
    }
    REQUIRE_FALSE(s->done());
    CHECK(gAnalyses == 4); // 每量一个方向计一次
    CHECK(s->live().phase == StepPhase::Confirm);
    CHECK(s->live().rounds == 2);
    CHECK(s->plannedSec() == Catch::Approx(6 * take));
    feedAll(*s, voice(fs, take, 0.05, 20));
    CHECK(s->live().directionIndex == 0);
    feedAll(*s, voice(fs, take, 0.2, 21));
    CHECK(s->done());
    CHECK(gAnalyses == 6); // 确认的两遍各计一次
    const auto out = s->finish();
    REQUIRE(out.results.direction.has_value());
    CHECK(out.results.direction->verdict.kind == core::DirectionCase::MaybeReversed);
}

TEST_CASE("expiry rules follow the contract")
{
    using session::Section;
    auto hit = [](core::ChangeKind k) {
        std::vector<Section> out;
        const auto set = session::staleOnChange(k);
        for (std::size_t i = 0; i < set.size(); ++i)
            if (set[i])
                out.push_back(static_cast<Section>(i));
        return out;
    };
    CHECK(hit(core::ChangeKind::Gain) ==
          std::vector<Section>{Section::Gain, Section::Noise, Section::Hum, Section::Sibilance});
    CHECK(hit(core::ChangeKind::Position) == std::vector<Section>{Section::Gain, Section::Noise, Section::DirectRatio,
                                                                  Section::Plosive, Section::Sibilance, Section::Click,
                                                                  Section::Direction, Section::Distance});
    CHECK(hit(core::ChangeKind::Room).size() == session::kSectionCount);
    const auto layout = session::staleOnLayoutChange();
    CHECK(layout[static_cast<std::size_t>(Section::Gain)]);
    CHECK(layout[static_cast<std::size_t>(Section::Hum)]);
    CHECK_FALSE(layout[static_cast<std::size_t>(Section::Reverb)]);
}

TEST_CASE("a change forgets the levels measured on the old chain, so the quiet step cannot pass off a stale SNR")
{
    session::StepContext ctx = mono();
    ctx.speechLevelDbfsA = -20.0;
    ctx.noiseFloorRms = 1e-4;
    // 调增益、挪位置、换房间都让增益一项过期，三种都要忘掉
    for (const auto kind : {core::ChangeKind::Gain, core::ChangeKind::Position, core::ChangeKind::Room})
    {
        session::StepContext c = ctx;
        session::forgetStale(c, session::staleOnChange(kind));
        CHECK_FALSE(c.speechLevelDbfsA.has_value());
        CHECK(c.noiseFloorRms == 0.0);
    }
    session::StepContext kept = ctx;
    session::forgetStale(kept, session::SectionSet{});
    CHECK(kept.speechLevelDbfsA == ctx.speechLevelDbfsA);

    // 调增益后只重做安静步：没有新的说话电平，SNR 要再测，不当成新鲜可信的结果
    session::StepContext after = ctx;
    session::forgetStale(after, session::staleOnChange(core::ChangeKind::Gain));
    auto quiet = session::makeScript(StepId::Quiet, after, nullptr);
    feedAll(*quiet, voice(mono().sampleRate, thr::session::quiet_s, 0.001, 9));
    REQUIRE(quiet->done());
    const auto out = quiet->finish();
    REQUIRE(out.results.noise.has_value());
    CHECK(out.results.noise->confidence.level == core::Reliability::Retest);
}

TEST_CASE("ungraded sibilance and clicks are not taken as 'no evidence' and get no colour")
{
    core::AllResults r;
    core::SibilanceResult sib; // 可信、severity 停在最低档：未分档
    core::ClickResult click;
    r.sibilance = sib;
    r.click = click;
    core::PlosiveResult plosive;
    plosive.confidence = core::retest(core::RetestReason::TooShort);
    r.plosive = plosive;
    // 喷麦要再测，剩下的只有未分档的两项：什么都没测出来，不能说距离合适
    auto d = session::deriveDistance(r);
    REQUIRE(d.has_value());
    CHECK(d->measures.closeEvidence == 0);
    CHECK(d->verdict.kind == core::DistanceCase::Unknown);

    // 喷麦分档了：轻就是没有证据，可以说合适
    r.plosive->confidence = core::trusted();
    d = session::deriveDistance(r);
    REQUIRE(d.has_value());
    CHECK(d->verdict.kind == core::DistanceCase::Fine);

    session::SessionView v;
    v.results = r;
    const JsonNode root = JsonReader(session::snapshotJson(v)).parse();
    CHECK(root["results"]["sibilance"]["tone"].text == "none");
    CHECK(root["results"]["click"]["tone"].text == "none");
    CHECK(root["results"]["plosive"]["tone"].text == "green");
    CHECK(root["results"]["sibilance"]["severity"].text == "light");
}

TEST_CASE("snapshot JSON has exactly the contract's keys in the contract's order")
{
    session::SessionView v;
    v.results.noSignal = core::NoSignalResult{};
    v.results.channels = core::ChannelsResult{};
    v.results.gain = core::GainResult{};
    v.results.noise = core::NoiseResult{};
    v.results.hum = core::HumResult{};
    v.results.deviceProcessing = core::DeviceProcessingResult{};
    v.results.reverb = core::ReverbResult{};
    v.results.directRatio = core::DirectRatioResult{};
    v.results.plosive = core::PlosiveResult{};
    v.results.sibilance = core::SibilanceResult{};
    v.results.click = core::ClickResult{};
    v.results.direction = core::DirectionResult{};
    v.results.distance = core::DistanceResult{};
    v.live.claps = {core::ClapState::Valid, core::ClapState::Truncated};
    v.results.gain->confidence = core::retest(core::RetestReason::TooShort);
    v.steps[static_cast<std::size_t>(StepId::Gain)].status = core::StepStatus::Done;
    v.steps[static_cast<std::size_t>(StepId::Quiet)].status = core::StepStatus::Done;
    v.stale[static_cast<std::size_t>(session::Section::Hum)] = true;

    const JsonNode root = JsonReader(session::snapshotJson(v)).parse();
    CHECK(root.keys() == std::vector<std::string>{"steps", "results", "bands", "live", "connect", "offlineRender"});
    CHECK(root["steps"].keys() == names(core::kStepIdNames));
    for (const auto& step : root["steps"].members)
        CHECK(step.second.keys() == names(bridge::kSnapshotStep));
    CHECK(root["steps"]["gain"]["status"].text == "done");
    CHECK(root["steps"]["quiet"]["status"].text == "stale"); // 它产出的电流声过期了
    CHECK(root["results"].keys() == names(bridge::kResultSections));
    const auto& r = root["results"];
    CHECK(r["noSignal"].keys() == withCommon(bridge::kResultNoSignal));
    CHECK(r["channels"].keys() == withCommon(bridge::kResultChannels));
    CHECK(r["gain"].keys() == withCommon(bridge::kResultGain));
    CHECK(r["noise"].keys() == withCommon(bridge::kResultNoise));
    CHECK(r["hum"].keys() == withCommon(bridge::kResultHum));
    CHECK(r["deviceProcessing"].keys() == withCommon(bridge::kResultDeviceProcessing));
    CHECK(r["reverb"].keys() == withCommon(bridge::kResultReverb));
    CHECK(r["directRatio"].keys() == withCommon(bridge::kResultDirectRatio));
    CHECK(r["plosive"].keys() == withCommon(bridge::kResultPlosive));
    CHECK(r["sibilance"].keys() == withCommon(bridge::kResultSibilance));
    CHECK(r["click"].keys() == withCommon(bridge::kResultClick));
    CHECK(r["direction"].keys() == withCommon(bridge::kResultDirection));
    CHECK(r["distance"].keys() == withCommon(bridge::kResultDistance));
    CHECK(r["reverb"]["t20Sec"].items.size() == 4);
    CHECK(r["gain"]["tone"].text == "none"); // 再测没有结论
    CHECK(r["gain"]["retestReason"].text == "too-short");
    CHECK(r["hum"]["stale"].flag);
    CHECK(root["bands"].keys() == names(bridge::kSnapshotBands));
    CHECK(root["bands"]["tpQuietDbfs"].number == Catch::Approx(thr::gain::tp_quiet_below_dbfs));
    CHECK(root["live"].keys() == names(bridge::kSnapshotLive));
    CHECK(root["live"]["claps"].items.size() == 2);
    CHECK(root["live"]["claps"].items[1].text == "truncated");
    CHECK(root["live"]["clapsNeeded"].number == Catch::Approx(thr::session::room_claps_needed));
    // callbackAgeSec 由编辑器按墙钟补上，其余都在这里
    CHECK(root["connect"].keys() == names(bridge::kSnapshotConnect));

    // 区段枚举与契约的区段名一一对应：只把第 i 项标过期，快照里过期的就是第 i 个名字
    REQUIRE(session::kSectionCount == bridge::kResultSections.size());
    for (std::size_t i = 0; i < session::kSectionCount; ++i)
    {
        session::SessionView one = v;
        one.stale = {};
        one.stale[i] = true;
        const JsonNode res = JsonReader(session::snapshotJson(one)).parse()["results"];
        for (std::size_t k = 0; k < session::kSectionCount; ++k)
            CHECK(res[std::string(bridge::kResultSections[k])]["stale"].flag == (k == i));
    }
}

TEST_CASE("channel prompts follow the host: no mono tracks means no 'make it mono' advice")
{
    session::SessionView v;
    core::ChannelsResult ch;
    ch.verdict.kind = core::ChannelCase::Identical;
    v.results.channels = ch;
    auto prompt = [&]() { return JsonReader(session::snapshotJson(v)).parse()["connect"]["channelPrompt"].text; };
    CHECK(prompt() == "identical");
    v.noMonoTracks = true;
    CHECK(prompt() == "none");
    v.results.channels->verdict.kind = core::ChannelCase::OneDominant;
    CHECK(prompt() == "one-silent-host");
    v.results.channels->verdict.kind = core::ChannelCase::AntiPhase;
    CHECK(prompt() == "antiphase");
    v.results.channels->confidence = core::retest(core::RetestReason::TooShort);
    CHECK(prompt() == "none");
}
