// SPDX-License-Identifier: GPL-3.0-or-later
//
// 证据波形：包络的最小最大值、格数与 hop 下限、阶段重来时退回、事件的映射排序与上限、回执 JSON 的形状，
// 以及位置步的事件与包络在同一条时间轴上（重拍之后也对得上）。不起线程；生命周期（rev、runs）在 session_test 里测。
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "config/Thresholds.h"
#include "core/level/Synth.h"
#include "core/room/ClapSynth.h"
#include "core/voice/VoiceSynth.h"
#include "plugin/BridgeApi.h"
#include "session/Evidence.h"
#include "session/MiniJson.h"
#include "session/Steps.h"

using namespace miciri;
using namespace miciri::testing;
using core::EvidenceKind;
using core::StepId;
using session::EnvelopeRecorder;
using session::EvidenceEventKind;
using session::EvidencePack;

namespace
{
EvidencePack record(EnvelopeRecorder& r)
{
    EvidencePack p;
    r.finish(p);
    return p;
}

void feedBlocks(EnvelopeRecorder& r, const std::vector<double>& x, std::size_t block = 517)
{
    for (std::size_t i = 0; i < x.size(); i += block)
        r.push(x.data() + i, std::min(block, x.size() - i));
}

void feedAll(session::StepScript& s, const std::vector<double>& x, std::size_t block = 517)
{
    for (std::size_t i = 0; i < x.size() && !s.done(); i += block)
        s.feed(x.data() + i, nullptr, std::min(block, x.size() - i));
}

std::vector<double> padded(std::vector<double> x, double fs, double seconds, std::uint64_t seed)
{
    const auto n = static_cast<std::size_t>(std::llround(fs * seconds));
    const auto pad = whiteNoise(n, 1e-4, seed);
    for (std::size_t i = x.size(); i < n; ++i)
        x.push_back(pad[i]);
    x.resize(n);
    return x;
}

// 一整阶段的轻拍，每一下都削波：阶段收满时要求重拍
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

// 包络在 [from, to) 秒里的最大值
int loudest(const EvidencePack& p, double from, double to)
{
    int most = -1000;
    for (std::size_t i = 0; i < p.max.size(); ++i)
        if (static_cast<double>(i + 1) * p.hopSec > from && static_cast<double>(i) * p.hopSec < to)
            most = std::max<int>(most, p.max[i]);
    return most;
}
} // namespace

TEST_CASE("the envelope keeps each cell's min and max in per-mille of full scale, 2 ms cells for a short step")
{
    const double fs = 48000.0;
    const auto base = static_cast<std::size_t>(std::llround(thr::session::evidence_hop_min_s * fs));
    const std::size_t cells = 500;
    std::vector<double> x(cells * base, 0.0);
    for (std::size_t c = 0; c < cells; ++c)
    {
        x[c * base + (c * 7) % base] = static_cast<double>(c % 10) / 10.0;
        x[c * base + (c * 13 + 5) % base] = -static_cast<double>(c % 7) / 8.0;
    }
    EnvelopeRecorder r(fs);
    feedBlocks(r, x);
    const auto p = record(r);
    CHECK(p.durationSec == Catch::Approx(static_cast<double>(x.size()) / fs));
    CHECK(p.hopSec == Catch::Approx(thr::session::evidence_hop_min_s));
    REQUIRE(p.min.size() == cells);
    REQUIRE(p.max.size() == cells);
    for (std::size_t c = 0; c < cells; ++c)
    {
        CHECK(p.max[c] == std::lround(static_cast<double>(c % 10) / 10.0 * 1000.0));
        CHECK(p.min[c] == -std::lround(static_cast<double>(c % 7) / 8.0 * 1000.0));
    }
}

TEST_CASE("a cell is the fewest whole samples not shorter than the floor")
{
    const double fs = 44100.0; // 2 ms 是 88.2 个采样：取 89 个，不取 88
    EnvelopeRecorder r(fs);
    const std::vector<double> x(4410, 0.25);
    feedBlocks(r, x, 64);
    const auto p = record(r);
    CHECK(p.hopSec >= thr::session::evidence_hop_min_s);
    CHECK(p.hopSec == Catch::Approx(89.0 / fs));
    CHECK(p.min.size() == (x.size() + 88) / 89); // 最后一格不满
    CHECK(p.max.back() == 250);
}

TEST_CASE("a long step merges whole cells so it stays under the cell limit, and a spike lands in its own cell")
{
    const double fs = 8000.0; // 2 ms = 16 个采样；40 s 是 20000 个基础格，三个并一格
    const double seconds = 40.0;
    std::vector<double> x(static_cast<std::size_t>(seconds * fs), 0.0);
    const std::size_t spikeAt = 100000;
    x[spikeAt] = 0.5;
    x.back() = -0.25;
    EnvelopeRecorder r(fs);
    feedBlocks(r, x, 1023);
    const auto p = record(r);
    const double floorHop = std::max(thr::session::evidence_hop_min_s, seconds / thr::session::evidence_cells_max);
    CHECK(p.min.size() <= static_cast<std::size_t>(thr::session::evidence_cells_max));
    CHECK(p.hopSec >= floorHop);
    CHECK(p.hopSec < floorHop + thr::session::evidence_hop_min_s + 1e-12); // 多出的不到一个基础格
    CHECK(static_cast<double>(p.min.size()) * p.hopSec >= seconds - 1e-9);
    CHECK(p.hopSec == Catch::Approx(48.0 / fs));
    const auto spikeCell = static_cast<std::size_t>(static_cast<double>(spikeAt) / fs / p.hopSec);
    for (std::size_t i = 0; i + 1 < p.max.size(); ++i)
        CHECK(p.max[i] == (i == spikeCell ? 500 : 0));
    CHECK(p.min.back() == -250);
}

TEST_CASE("out-of-range samples clamp to full scale, NaN is skipped and rounding is to the nearest per-mille")
{
    const double fs = 1000.0; // 2 ms = 2 个采样
    const double inf = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const std::vector<double> x{2.0, -3.0, nan, nan, inf, nan, 0.0004, 0.0006, -0.2504, -0.2496};
    EnvelopeRecorder r(fs);
    feedBlocks(r, x, 3);
    const auto p = record(r);
    REQUIRE(p.min.size() == 5);
    CHECK((p.min[0] == -1000 && p.max[0] == 1000));
    CHECK((p.min[1] == 0 && p.max[1] == 0)); // 整格都是 NaN
    CHECK((p.min[2] == 1000 && p.max[2] == 1000));
    CHECK((p.min[3] == 0 && p.max[3] == 1));
    CHECK((p.min[4] == -250 && p.max[4] == -250));
}

TEST_CASE("rewinding to the phase start drops everything after it, partial cell included")
{
    const double fs = 48000.0;
    const std::vector<double> before(150, 0.1), dropped(300, 0.9), after(200, -0.2);
    EnvelopeRecorder redone(fs);
    feedBlocks(redone, before, 64);
    redone.mark();
    feedBlocks(redone, dropped, 64);
    redone.rewind();
    feedBlocks(redone, after, 64);

    EnvelopeRecorder straight(fs);
    feedBlocks(straight, before, 64);
    feedBlocks(straight, after, 64);

    const auto a = record(redone);
    const auto b = record(straight);
    CHECK(a.durationSec == b.durationSec);
    CHECK(a.min == b.min);
    CHECK(a.max == b.max);
    CHECK(*std::max_element(a.max.begin(), a.max.end()) == 100);
}

TEST_CASE("only plosive, sibilance and mouth marks become events, sorted by time, at most the limit")
{
    core::AllResults r;
    core::PlosiveResult plosive;
    plosive.evidence.marks = {{EvidenceKind::Plosive, 5.0, 5.04, 9.5, -1}, {EvidenceKind::Plosive, 1.0, 1.05, 3.0, -1}};
    r.plosive = plosive;
    core::SibilanceResult sibilance;
    sibilance.evidence.marks = {{EvidenceKind::Sibilant, 3.0, 3.15, -40.0, -1}};
    r.sibilance = sibilance;
    core::ClickResult click;
    click.evidence.marks = {{EvidenceKind::Click, 2.0, 2.003, -45.0, -1}};
    r.click = click;
    core::GainResult gain; // 削波与电流声的标记不画在证据波形上
    gain.evidence.marks = {{EvidenceKind::DigitalClip, 0.5, 0.6, 0.0, 0}};
    r.gain = gain;
    core::HumResult hum;
    hum.evidence.marks = {{EvidenceKind::HumPeak, 0.0, 8.0, 50.0, -1}};
    r.hum = hum;

    const auto e = session::evidenceEvents(r);
    REQUIRE(e.size() == 4);
    CHECK(e[0].kind == EvidenceEventKind::Plosive);
    CHECK(e[0].timeSec == 1.0);
    CHECK(e[0].durSec == Catch::Approx(0.05));
    CHECK(e[0].levelDb == 3.0);
    CHECK(e[1].kind == EvidenceEventKind::Mouth);
    CHECK(e[1].levelDb == -45.0);
    CHECK(e[2].kind == EvidenceEventKind::Sibilance);
    CHECK(e[2].levelDb == -40.0);
    CHECK(e[3].timeSec == 5.0);

    // 超出上限：留最早的
    const auto most = static_cast<std::size_t>(thr::session::evidence_events_max);
    core::ClickResult many;
    for (std::size_t i = most + 50; i-- > 0;)
        many.evidence.marks.push_back({EvidenceKind::Click, static_cast<double>(i), static_cast<double>(i), -50.0, -1});
    core::AllResults lots;
    lots.click = many;
    const auto capped = session::evidenceEvents(lots);
    REQUIRE(capped.size() == most);
    CHECK(capped.front().timeSec == 0.0);
    CHECK(capped.back().timeSec == static_cast<double>(most - 1));
    CHECK(std::is_sorted(capped.begin(), capped.end(),
                         [](const auto& a, const auto& b) { return a.timeSec < b.timeSec; }));
}

TEST_CASE("the evidence payload has exactly the contract's keys, and event kinds use the contract's names")
{
    CHECK(names(bridge::kEvidenceEventKinds) == std::vector<std::string>{"plosive", "sibilance", "mouth"});
    CHECK(bridge::kEvidenceEventKinds[static_cast<std::size_t>(EvidenceEventKind::Mouth)] == "mouth");
    EvidencePack p;
    p.step = StepId::Position;
    p.rev = 3;
    p.durationSec = 0.004;
    p.hopSec = 0.002;
    p.min = {-12, -1000};
    p.max = {15, 1000};
    p.events = {{EvidenceEventKind::Sibilance, 0.001, 0.002, -40.5}};
    const JsonNode root = JsonReader(session::evidenceJson(p)).parse();
    CHECK(root.keys() == names(bridge::kEvidencePack));
    CHECK(root["envelope"].keys() == names(bridge::kEvidenceEnvelope));
    CHECK(root["stepId"].text == "position");
    CHECK(root["rev"].number == 3.0);
    REQUIRE(root["envelope"]["min"].items.size() == 2);
    CHECK(root["envelope"]["min"].items[1].number == -1000.0);
    CHECK(root["envelope"]["max"].items[0].number == 15.0);
    REQUIRE(root["events"].items.size() == 1);
    CHECK(root["events"].items[0].keys() == names(bridge::kEvidenceEvent));
    CHECK(root["events"].items[0]["kind"].text == "sibilance");
    CHECK(root["events"].items[0]["levelDb"].number == -40.5);
}

TEST_CASE("position-step events sit on the envelope's timeline, also after the soft claps were redone")
{
    const double fs = 48000.0;
    session::StepContext ctx;
    ctx.sampleRate = fs;
    auto s = session::makeScript(StepId::Position, ctx, nullptr);

    // 第一次轻拍满刻度削波，倒数后重拍：被丢掉的那次与倒数不在时间轴上
    feedAll(*s, clippedSoftClaps(fs));
    REQUIRE(s->live().notice == core::FlowNotice::ClapClipped);
    feedAll(*s, std::vector<double>(static_cast<std::size_t>(std::llround(thr::session::notice_s * fs)), 0.9));
    const double clapSec = thr::session::soft_claps * thr::session::soft_clap_round_s;
    feedAll(*s, std::vector<double>(static_cast<std::size_t>(std::llround(clapSec * fs)), 0.0));
    REQUIRE(s->live().phase == core::StepPhase::Plosive);

    const double listSec = thr::session::word_list_rounds * thr::session::word_list_round_s;
    std::vector<double> pops(16, -200.0);
    pops[3] = 10.0;
    feedAll(*s, padded(wordList(fs, pops), fs, listSec, 61));
    std::vector<double> hiss = whiteNoise(static_cast<std::size_t>(8.0 * fs), 3e-5, 21);
    for (int i = 0; i < 6; ++i)
    {
        addNoiseBurst(hiss, fs, 0.5 + 1.2 * i, 0.15, 0.05, 100 + static_cast<std::uint64_t>(i));
        addVowel(hiss, fs, 0.7 + 1.2 * i, 0.3, 140.0, 0.3);
    }
    feedAll(*s, padded(hiss, fs, listSec, 62));
    const double naturalSec = thr::click::analysis_window_s;
    std::vector<double> natural = whiteNoise(static_cast<std::size_t>(naturalSec * fs), 1e-4, 51);
    for (int i = 0; i < 8; ++i)
        addVowel(natural, fs, 0.5 + 1.0 * i, 0.3, 130.0 + 10.0 * i, 0.1, 0.03);
    for (int i = 0; i < 5; ++i)
        addTick(natural, fs, 1.05 + 1.0 * i, 0.05);
    feedAll(*s, natural);
    REQUIRE(s->done());

    const auto out = s->finish();
    const auto p = s->evidence(out.results);
    CHECK(p.durationSec == Catch::Approx(clapSec + 2.0 * listSec + naturalSec));
    CHECK(p.min.size() == p.max.size());
    CHECK(p.min.size() <= static_cast<std::size_t>(thr::session::evidence_cells_max));
    CHECK(p.hopSec >= std::max(thr::session::evidence_hop_min_s, p.durationSec / thr::session::evidence_cells_max));
    // 重拍后的轻拍是静音：被丢掉的满刻度那次不在包络里
    CHECK(loudest(p, 0.0, clapSec - p.hopSec) == 0);

    std::vector<session::EvidenceEvent> plosive, sibilance, mouth;
    for (const auto& e : p.events)
        (e.kind == EvidenceEventKind::Plosive     ? plosive
         : e.kind == EvidenceEventKind::Sibilance ? sibilance
                                                  : mouth)
            .push_back(e);
    REQUIRE(plosive.size() >= 10);
    CHECK(plosive.front().timeSec == Catch::Approx(clapSec + 0.5).margin(0.1));
    for (const auto& e : plosive)
        CHECK((e.timeSec >= clapSec && e.timeSec < clapSec + listSec));
    for (const auto& e : sibilance)
        CHECK((e.timeSec >= clapSec + listSec && e.timeSec < clapSec + 2.0 * listSec));

    // 合成信号里放进去的每个事件：在轴上对应的位置找得到，且落在包络有声音的地方、之前的停顿是安静的（标记与波形没有错位）。
    // 字表元音的硬收尾、补齐用的白噪声也可能被报成口水音或擦音，那是合成信号的性质，这里不管
    auto find = [](const std::vector<session::EvidenceEvent>& list, double t, double tol) {
        const auto it =
            std::find_if(list.begin(), list.end(), [&](const auto& e) { return std::fabs(e.timeSec - t) < tol; });
        return it == list.end() ? nullptr : &*it;
    };
    auto aligned = [&p](double t) {
        INFO("event at " << t);
        CHECK(loudest(p, t, t + 0.05) >= 40);
        CHECK(loudest(p, t - 0.2, t - 0.05) < 10);
    };
    for (const auto& e : plosive)
        aligned(e.timeSec);
    for (int i = 0; i < 6; ++i)
    {
        const auto* e = find(sibilance, clapSec + listSec + 0.5 + 1.2 * i, 0.05);
        REQUIRE(e != nullptr);
        aligned(e->timeSec);
    }
    for (int i = 0; i < 5; ++i) // 口水音那段是三段拼起来分析的，自然朗读里的尖峰要换算回这一步的时间轴
    {
        const auto* e = find(mouth, clapSec + 2.0 * listSec + 1.05 + i, 0.01);
        REQUIRE(e != nullptr);
        aligned(e->timeSec);
    }
}
