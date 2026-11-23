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
// 不喷的字比值接近 0：合成基线跨基频在 1 dB 以内，这里留 1 dB 余量
constexpr double kCleanMaxDb = 2.0;
// 一个喷的字：爆发比元音的低频高 kPopDb，比值要明显高出不喷的字（不要求等于 kPopDb，起音窗与元音参考都会摊薄一点）
constexpr double kPopDb = 10.0;
constexpr double kPoppedMinDb = 6.0;

std::vector<double> listWithOnePop(double popDb)
{
    std::vector<double> pops(12, kNone);
    pops[7] = popDb;
    return pops;
}

// 碰到话筒架的闷响：hz 的正弦，2 ms 起、按时间常数 tau 指数衰减，长 seconds。能量几乎全在 100 Hz 以下
void addThump(std::vector<double>& x, double fs, double startSec, double seconds, double hz, double amplitude,
              double tau)
{
    const auto start = static_cast<std::size_t>(startSec * fs);
    const auto length = static_cast<std::size_t>(seconds * fs);
    for (std::size_t i = 0; i < length && start + i < x.size(); ++i)
    {
        const double t = static_cast<double>(i) / fs;
        const double attack = t < 0.002 ? 0.5 - 0.5 * std::cos(kPi * t / 0.002) : 1.0;
        x[start + i] += amplitude * attack * std::exp(-t / tau) * std::sin(2.0 * kPi * hz * t);
    }
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
        CHECK(std::fabs(events[i].ratioDb) < kCleanMaxDb);
        CHECK(events[i].startSec == within(0.5 + 1.1 * static_cast<double>(i), 0.05));
    }
    const auto r = core::analysePlosive(x, fs);
    CHECK(r.verdict.severity == core::Severity::Light);
    CHECK(r.confidence.level == core::Reliability::Trusted);
    CHECK(r.measures.worstRatioDb < kCleanMaxDb);
}

TEST_CASE("喷麦：录音停在最后一个音节中间时，证据不越过录音结尾", "[voice][plosive]")
{
    const double fs = 48000.0;
    auto x = wordList(fs, listWithOnePop(kPopDb));
    x.resize(static_cast<std::size_t>((0.5 + 1.1 * 11.0 + 0.25) * fs));
    const double seconds = static_cast<double>(x.size()) / fs;
    const auto events = core::plosiveEvents(x, fs);
    REQUIRE(events.size() == 12);
    for (const auto& e : events)
        CHECK(e.endSec <= seconds);
    for (const auto& m : core::analysePlosive(x, fs).evidence.marks)
        CHECK(m.endSec <= seconds);
}

TEST_CASE("喷麦：读数取一遍字表的最大值，一个喷的字就够", "[voice][plosive]")
{
    const double fs = 48000.0;
    const auto x = wordList(fs, listWithOnePop(kPopDb));
    const auto events = core::plosiveEvents(x, fs);
    REQUIRE(events.size() == 12);
    const auto r = core::analysePlosive(x, fs);
    INFO("worst " << r.measures.worstRatioDb << " median " << medianRatio(events));
    // 只有第 8 个字喷：最大值落在它上面，中位数仍接近 0（取中位数就会漏掉）
    CHECK(r.measures.worstRatioDb == within(events[7].ratioDb, 1e-12));
    CHECK(r.measures.worstRatioDb > kPoppedMinDb);
    CHECK(medianRatio(events) < kCleanMaxDb);
    CHECK(r.verdict.severity == core::Severity::Light); // 不分档
    CHECK(r.measures.events == 12);
    REQUIRE_FALSE(r.evidence.marks.empty());
    CHECK(r.evidence.marks[7].kind == core::EvidenceKind::Plosive);
}

TEST_CASE("喷麦：爆发越强比值越大，但不分档", "[voice][plosive]")
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
    CHECK(core::analysePlosive(wordList(fs, listWithOnePop(12.0)), fs).verdict.severity == core::Severity::Light);
}

TEST_CASE("喷麦：后面没有元音的低频闷响不算音节", "[voice][plosive]")
{
    // 一遍字表念完，再录 2.5 s 底噪；其中一份在字表结束 0.6 s 后碰一下话筒架：0.28 s、60 Hz、逐渐衰减的闷响，
    // 几乎全在 100 Hz 以下，后面只有底噪。它有起音，元音窗里却只有闷响的尾巴和底噪，不能当成一次很重的喷麦
    const double fs = 48000.0;
    auto quiet = wordList(fs, std::vector<double>(12, kNone));
    const double listEnd = static_cast<double>(quiet.size()) / fs;
    const auto tail = whiteNoise(static_cast<std::size_t>(2.5 * fs), 1e-4, 23);
    quiet.insert(quiet.end(), tail.begin(), tail.end());
    auto bumped = quiet;
    const double thumpAt = listEnd + 0.6;
    addThump(bumped, fs, thumpAt, 0.28, 60.0, 0.02, 0.06);

    const auto before = core::analysePlosive(quiet, fs);
    const auto after = core::analysePlosive(bumped, fs);
    INFO("worst " << before.measures.worstRatioDb << " -> " << after.measures.worstRatioDb << ", events "
                  << before.measures.events << " -> " << after.measures.events);
    REQUIRE(before.measures.events == 12);
    CHECK(before.measures.worstRatioDb < kCleanMaxDb); // 字都不喷：闷响要是算进来，最大值会落在它上面
    CHECK(after.measures.events == before.measures.events);
    CHECK(after.measures.worstRatioDb == within(before.measures.worstRatioDb, 0.05));
    for (const auto& e : core::plosiveEvents(bumped, fs))
        CHECK(e.startSec < thumpAt - 0.3);
}

TEST_CASE("喷麦：底噪高、刚过起音门限的真实音节照样算", "[voice][plosive]")
{
    // 底噪抬到约 -40 dBFS：起音刚好高出底噪 20 dB 以上，元音频带比底噪高约 25 dB
    const double fs = 48000.0;
    auto x = wordList(fs, listWithOnePop(kPopDb));
    const auto noise = whiteNoise(x.size(), 0.01, 29);
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] += noise[i];
    const auto r = core::analysePlosive(x, fs);
    INFO("worst " << r.measures.worstRatioDb);
    CHECK(r.measures.events == 12);
    CHECK(r.measures.worstRatioDb > kPoppedMinDb);
}

TEST_CASE("喷麦：最大值超过参考值才标出，只是轻提示", "[voice][plosive]")
{
    const double fs = 48000.0;
    const auto clean = core::analysePlosive(wordList(fs, std::vector<double>(12, kNone)), fs);
    CHECK_FALSE(clean.verdict.overReference);
    const auto strong = core::analysePlosive(wordList(fs, listWithOnePop(12.0)), fs);
    INFO("worst " << strong.measures.worstRatioDb);
    CHECK(strong.measures.worstRatioDb > thr::plosive::reference_max_db + 1.0); // 离参考值够远，不贴边
    CHECK(strong.verdict.overReference);
    CHECK(strong.verdict.severity == core::Severity::Light); // 仍不分档
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

    auto x = wordList(fs, listWithOnePop(kPopDb));
    const auto clean = core::analysePlosive(x, fs);
    x[static_cast<std::size_t>(0.2 * fs)] = std::numeric_limits<double>::quiet_NaN();
    x[static_cast<std::size_t>(0.3 * fs)] = std::numeric_limits<double>::infinity();
    const auto dirty = core::analysePlosive(x, fs);
    CHECK(std::isfinite(dirty.measures.worstRatioDb));
    CHECK(dirty.measures.events == clean.measures.events);
}

TEST_CASE("喷麦：22.05 kHz 也能切出音节", "[voice][plosive]")
{
    const double fs = 22050.0;
    const auto r = core::analysePlosive(wordList(fs, listWithOnePop(kPopDb)), fs);
    CHECK(r.measures.events == 12);
    CHECK(r.measures.worstRatioDb > kPoppedMinDb);
}
