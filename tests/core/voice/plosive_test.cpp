// SPDX-License-Identifier: GPL-3.0-or-later
//
// 喷麦：起音切音节、同音节瞬态比、一遍字表取最大值。
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

#include "config/Thresholds.h"
#include "core/voice/VoiceSynth.h"
#include "voice/Voice.h"

using namespace miciri;
using namespace miciri::testing;

namespace
{
constexpr double kNone = -200.0; // 这个字不喷

std::vector<double> listWithOnePop(double popDb)
{
    std::vector<double> pops(12, kNone);
    pops[7] = popDb;
    return pops;
}

double medianRatio(const std::vector<core::PlosiveEvent>& events)
{
    std::vector<double> r;
    for (const auto& e : events)
        r.push_back(e.ratioDb);
    std::sort(r.begin(), r.end());
    return r.empty() ? 0.0 : r[r.size() / 2];
}
} // namespace

TEST_CASE("喷麦：每个音节一个事件，不喷的字比值接近 0", "[voice][plosive]")
{
    const double fs = 48000.0;
    const auto x = wordList(fs, std::vector<double>(12, kNone));
    const auto events = core::plosiveEvents(x, fs);
    REQUIRE(events.size() == 12);
    for (std::size_t i = 0; i < events.size(); ++i)
    {
        INFO("syllable " << i << " ratio " << events[i].ratioDb);
        CHECK(std::fabs(events[i].ratioDb) < 2.0);
        CHECK(events[i].startSec == within(0.5 + 1.1 * static_cast<double>(i), 0.05));
    }
    const auto r = core::analysePlosive(x, fs);
    CHECK(r.verdict.severity == core::Severity::Light);
    CHECK(r.confidence.level == core::Reliability::Trusted);
    CHECK(r.measures.worstRatioDb < thr::plosive::moderate_above_db);
}

TEST_CASE("喷麦：严重度取一遍字表的最大值，一个喷的字就够", "[voice][plosive]")
{
    const double fs = 48000.0;
    const auto x = wordList(fs, listWithOnePop(10.0));
    const auto events = core::plosiveEvents(x, fs);
    REQUIRE(events.size() == 12);
    const auto r = core::analysePlosive(x, fs);
    INFO("worst " << r.measures.worstRatioDb << " median " << medianRatio(events));
    // 只有第 8 个字喷：最大值落在它上面，中位数仍接近 0（取中位数就会漏掉）
    CHECK(r.measures.worstRatioDb == within(events[7].ratioDb, 1e-12));
    CHECK(r.measures.worstRatioDb > thr::plosive::heavy_above_db);
    CHECK(medianRatio(events) < 2.0);
    CHECK(r.verdict.severity == core::Severity::Heavy);
    CHECK(r.measures.events == 12);
    REQUIRE_FALSE(r.evidence.marks.empty());
    CHECK(r.evidence.marks[7].kind == core::EvidenceKind::Plosive);
}

TEST_CASE("喷麦：爆发越强比值越大，档位随之升高", "[voice][plosive]")
{
    const double fs = 48000.0;
    double previous = -std::numeric_limits<double>::infinity();
    for (double popDb : {-6.0, 0.0, 6.0, 12.0})
    {
        const auto r = core::analysePlosive(wordList(fs, listWithOnePop(popDb)), fs);
        INFO("pop " << popDb << " dB -> worst " << r.measures.worstRatioDb);
        CHECK(r.measures.worstRatioDb > previous + 3.0);
        previous = r.measures.worstRatioDb;
    }
    CHECK(core::analysePlosive(wordList(fs, listWithOnePop(0.0)), fs).verdict.severity == core::Severity::Light);
}

TEST_CASE("喷麦：同一段录音整体衰减不改变比值", "[voice][plosive]")
{
    const double fs = 48000.0;
    const auto loud = core::analysePlosive(wordList(fs, listWithOnePop(8.0)), fs);
    const auto soft = core::analysePlosive(wordList(fs, listWithOnePop(8.0), 0.25), fs);
    CHECK(soft.measures.events == loud.measures.events);
    CHECK(soft.measures.worstRatioDb == within(loud.measures.worstRatioDb, 0.5));
}

TEST_CASE("喷麦：各采样率结果一致", "[voice][plosive]")
{
    const double reference =
        core::analysePlosive(wordList(48000.0, listWithOnePop(8.0)), 48000.0).measures.worstRatioDb;
    for (double fs : kRates)
    {
        const auto r = core::analysePlosive(wordList(fs, listWithOnePop(8.0)), fs);
        INFO("fs " << fs << " worst " << r.measures.worstRatioDb);
        CHECK(r.measures.events == 12);
        CHECK(r.measures.worstRatioDb == within(reference, 1.0));
    }
}

TEST_CASE("喷麦：没有起音时要求再测，NaN 与 Inf 不让分析失效", "[voice][plosive]")
{
    const double fs = 48000.0;
    const auto quiet = core::analysePlosive(whiteNoise(static_cast<std::size_t>(3.0 * fs), 1e-4, 3), fs);
    CHECK(quiet.measures.events == 0);
    CHECK(quiet.confidence.level == core::Reliability::Retest);
    CHECK(quiet.confidence.reason == core::RetestReason::TooShort);

    auto x = wordList(fs, listWithOnePop(10.0));
    const auto clean = core::analysePlosive(x, fs);
    x[static_cast<std::size_t>(0.2 * fs)] = std::numeric_limits<double>::quiet_NaN();
    x[static_cast<std::size_t>(0.3 * fs)] = std::numeric_limits<double>::infinity();
    const auto dirty = core::analysePlosive(x, fs);
    CHECK(std::isfinite(dirty.measures.worstRatioDb));
    CHECK(dirty.measures.events == clean.measures.events);
}

TEST_CASE("喷麦：档界先后关系", "[voice][plosive]")
{
    STATIC_REQUIRE(thr::plosive::moderate_above_db < thr::plosive::heavy_above_db);
}

TEST_CASE("喷麦：22.05 kHz 也能切出音节", "[voice][plosive]")
{
    const double fs = 22050.0;
    const auto r = core::analysePlosive(wordList(fs, listWithOnePop(10.0)), fs);
    CHECK(r.measures.events == 12);
    CHECK(r.verdict.severity == core::Severity::Heavy);
}
