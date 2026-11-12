// SPDX-License-Identifier: GPL-3.0-or-later
//
// 口水音：LPC 残差尖峰的检出，擦音、爆破、高频背景三条排除规则各自的正反例，残差倍数门限，计数与电平。
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "config/Thresholds.h"
#include "core/voice/VoiceSynth.h"
#include "level/Dsp.h"
#include "voice/Voice.h"

using namespace miciri;
using namespace miciri::testing;

namespace
{
constexpr double kTick = 0.05;

// 八个 0.3 s 的元音（缓起缓落，LPC 能预测），之间是 -80 dBFS 的底噪
std::vector<double> speechTake(double fs, double seconds = 8.5)
{
    std::vector<double> x = whiteNoise(static_cast<std::size_t>(seconds * fs), 1e-4, 51);
    for (int i = 0; i < 8; ++i)
        addVowel(x, fs, 0.5 + 1.0 * i, 0.3, 130.0 + 10.0 * i, 0.1, 0.03);
    return x;
}

const core::ClickCandidate* candidateNear(const core::ClickScan& scan, double t)
{
    const core::ClickCandidate* best = nullptr;
    for (const auto& c : scan.candidates)
        if (std::fabs(c.timeSec - t) < 0.005 && (best == nullptr || c.residualMultiple > best->residualMultiple))
            best = &c;
    return best;
}
} // namespace

TEST_CASE("口水音：平稳的元音与底噪不报", "[voice][clicks]")
{
    const double fs = 48000.0;
    const auto r = core::analyseClicks(speechTake(fs), fs);
    CHECK(r.confidence.level == core::Reliability::Trusted);
    CHECK(r.measures.count == 0);
    CHECK(r.measures.perMinute == 0.0);
}

TEST_CASE("口水音：停顿里的尖峰逐个计数，给每分钟次数与最响一下的电平", "[voice][clicks]")
{
    const double fs = 48000.0;
    auto x = speechTake(fs);
    for (int i = 0; i < 5; ++i)
        addTick(x, fs, 1.05 + 1.0 * i, kTick);
    addTick(x, fs, 6.05, 2.0 * kTick);
    const auto scan = core::scanClicks(x, fs);
    const auto r = core::summariseClicks(scan, thr::click::residual_median_multiple);
    REQUIRE(scan.speechDb.has_value());
    CHECK(r.measures.count == 6);
    CHECK(r.measures.analysedSeconds == within(8.5, 1e-3));
    CHECK(r.measures.perMinute == within(6.0 / 8.5 * 60.0, 1e-3));
    // 最响的是加倍的那一下：高通后峰值比 2 × kTick 略低
    CHECK(r.measures.loudestDbfs < 20.0 * std::log10(2.0 * kTick));
    CHECK(r.measures.loudestDbfs > 20.0 * std::log10(2.0 * kTick) - 4.0);
    CHECK(r.measures.loudestRelDb == within(r.measures.loudestDbfs - *scan.speechDb, 1e-9));
    REQUIRE(r.evidence.marks.size() == 6);
    CHECK(r.evidence.marks[5].kind == core::EvidenceKind::Click);
    CHECK(r.evidence.marks[5].startSec == within(6.05, 0.002));
    // 只作提示：档界还没有实录依据，不分档
    CHECK(r.verdict.severity == core::Severity::Light);
}

TEST_CASE("口水音：之后高频持续的是擦音，不算", "[voice][clicks]")
{
    const double fs = 48000.0;
    const double t = 1.2;
    auto alone = speechTake(fs);
    addTick(alone, fs, t, kTick);
    auto hiss = alone;
    addHiss(hiss, fs, t - 0.06, 0.4, 0.05 * kTick, 61);

    const auto control = core::scanClicks(alone, fs);
    const auto* c0 = candidateNear(control, t);
    REQUIRE(c0 != nullptr);
    CHECK_FALSE(c0->fricative);
    CHECK(core::summariseClicks(control, thr::click::residual_median_multiple).measures.count == 1);

    const auto scan = core::scanClicks(hiss, fs);
    const auto* c = candidateNear(scan, t);
    REQUIRE(c != nullptr);
    INFO("multiple " << c->residualMultiple << " context " << c->contextRelDb);
    CHECK(c->residualMultiple > thr::click::residual_median_multiple);
    CHECK(c->contextRelDb < thr::click::context_max_rel_db);
    CHECK_FALSE(c->plosive);
    CHECK(c->fricative);
    CHECK(core::summariseClicks(scan, thr::click::residual_median_multiple).measures.count == 0);
}

TEST_CASE("口水音：之后低频与元音一起上来的是爆破音，不算", "[voice][clicks]")
{
    const double fs = 48000.0;
    const double t = 1.2;
    auto x = speechTake(fs);
    addTick(x, fs, t, kTick);
    addVowel(x, fs, t + 0.004, 0.3, 120.0, 0.1, 0.003);
    const auto scan = core::scanClicks(x, fs);
    const auto* c = candidateNear(scan, t);
    REQUIRE(c != nullptr);
    INFO("multiple " << c->residualMultiple << " context " << c->contextRelDb);
    CHECK(c->residualMultiple > thr::click::residual_median_multiple);
    CHECK(c->contextRelDb < thr::click::context_max_rel_db);
    CHECK_FALSE(c->fricative);
    CHECK(c->plosive);
    CHECK(core::summariseClicks(scan, thr::click::residual_median_multiple).measures.count == 0);
}

TEST_CASE("口水音：高频背景本来就响时不算（齿音起头、送气）", "[voice][clicks]")
{
    const double fs = 48000.0;
    const double t = 1.2;
    auto x = speechTake(fs);
    addHiss(x, fs, t - 0.2, 0.5, 0.01, 71);
    addTick(x, fs, t, 0.6);
    const auto scan = core::scanClicks(x, fs);
    const auto* c = candidateNear(scan, t);
    REQUIRE(c != nullptr);
    INFO("multiple " << c->residualMultiple << " context " << c->contextRelDb);
    CHECK(c->residualMultiple > thr::click::residual_median_multiple);
    CHECK_FALSE(c->fricative);
    CHECK_FALSE(c->plosive);
    CHECK(c->contextRelDb > thr::click::context_max_rel_db);
    CHECK(core::summariseClicks(scan, thr::click::residual_median_multiple).measures.count == 0);
}

TEST_CASE("口水音：尖峰不到残差倍数门限的不算", "[voice][clicks]")
{
    const double fs = 48000.0;
    auto x = speechTake(fs);
    addTick(x, fs, 1.2, kTick);
    const auto scan = core::scanClicks(x, fs);
    const auto* c = candidateNear(scan, 1.2);
    REQUIRE(c != nullptr);
    CHECK(core::summariseClicks(scan, c->residualMultiple * 0.9).measures.count == 1);
    CHECK(core::summariseClicks(scan, c->residualMultiple * 1.1).measures.count == 0);
}

TEST_CASE("口水音：没有语音时要求再测", "[voice][clicks]")
{
    const double fs = 48000.0;
    const auto r = core::analyseClicks(std::vector<double>(static_cast<std::size_t>(2.0 * fs), 0.0), fs);
    CHECK(r.measures.count == 0);
    CHECK(r.confidence.reason == core::RetestReason::TooShort);
}

TEST_CASE("口水音：各采样率计数一致", "[voice][clicks]")
{
    for (double fs : kRates)
    {
        auto x = speechTake(fs);
        for (int i = 0; i < 6; ++i)
            addTick(x, fs, 1.05 + 1.0 * i, kTick);
        const auto r = core::analyseClicks(x, fs);
        INFO("fs " << fs);
        CHECK(r.measures.count == 6);
    }
}

TEST_CASE("口水音：只有底噪、没人说话时要求再测，不报“没有口水音”", "[voice][clicks]")
{
    const double fs = 48000.0;
    auto x = whiteNoise(static_cast<std::size_t>(8.0 * fs), 1e-3, 52);
    addTick(x, fs, 2.0, kTick);
    const auto scan = core::scanClicks(x, fs);
    CHECK_FALSE(scan.speechDb.has_value());
    CHECK(scan.candidates.empty());
    const auto r = core::summariseClicks(scan, thr::click::residual_median_multiple);
    CHECK(r.measures.count == 0);
    CHECK(r.confidence.level == core::Reliability::Retest);
    CHECK(r.confidence.reason == core::RetestReason::TooShort);
}

TEST_CASE("口水音：22.05 kHz 也能计数", "[voice][clicks]")
{
    const double fs = 22050.0;
    auto x = speechTake(fs);
    for (int i = 0; i < 6; ++i)
        addTick(x, fs, 1.05 + 1.0 * i, kTick);
    const auto r = core::analyseClicks(x, fs);
    CHECK(r.confidence.level == core::Reliability::Trusted);
    CHECK(r.measures.count == 6);
    CHECK(std::isfinite(r.measures.loudestDbfs));
}

TEST_CASE("口水音：候选门限不高于计数门限", "[voice][clicks]")
{
    STATIC_REQUIRE(thr::click::candidate_multiple <= thr::click::residual_median_multiple);
    STATIC_REQUIRE(thr::click::speech_floor_percentile < thr::click::speech_percentile);
}
