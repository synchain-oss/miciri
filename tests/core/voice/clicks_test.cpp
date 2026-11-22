// SPDX-License-Identifier: GPL-3.0-or-later
//
// 口水音：LPC 残差尖峰的检出，擦音、爆破、高频背景三条排除规则各自的正反例，残差倍数门限，计数与电平。
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <optional>

#include "config/Thresholds.h"
#include "core/voice/VoiceSynth.h"
#include "decide/Decide.h"
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
    // 没有口水音时最响一下的两项不是测量值，留默认的 0
    CHECK(r.measures.loudestDbfs == 0.0);
    CHECK(r.measures.loudestRelDb == 0.0);
    CHECK(r.evidence.marks.empty());
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

TEST_CASE("口水音：每分钟次数超过参考值才标出，退后之后还多才建议喝点水", "[voice][clicks][reference]")
{
    const double fs = 48000.0;
    // 8.5 s 里 6 下：每分钟约 42 次，低于参考值
    auto fewTake = speechTake(fs);
    // 每个停顿两下、相隔 300 ms（远大于合并间隔）：12 下，每分钟约 85 次，高于参考值
    auto manyTake = speechTake(fs);
    for (int i = 0; i < 6; ++i)
    {
        addTick(fewTake, fs, 1.05 + 1.0 * i, kTick);
        addTick(manyTake, fs, 1.0 + 1.0 * i, kTick);
        addTick(manyTake, fs, 1.3 + 1.0 * i, kTick);
    }
    const auto few = core::analyseClicks(fewTake, fs);
    const auto many = core::analyseClicks(manyTake, fs);
    INFO("few " << few.measures.perMinute << "/min, many " << many.measures.perMinute << "/min");
    REQUIRE(few.measures.count == 6);
    REQUIRE(many.measures.count == 12);
    CHECK(few.measures.perMinute < thr::click::reference_max_per_min);
    CHECK_FALSE(few.verdict.overReference);
    CHECK(many.measures.perMinute > thr::click::reference_max_per_min);
    CHECK(many.verdict.overReference);
    // 只作提示，不分档
    CHECK(many.verdict.severity == core::Severity::Light);

    // 建议：没超过不提示；第一次超过先退后一点；上一次也超过（退后之后还多）才建议喝点水
    CHECK(core::clickAdviceOf(few, std::nullopt) == core::ClickAdvice::None);
    CHECK(core::clickAdviceOf(many, std::nullopt) == core::ClickAdvice::BackOffALittle);
    CHECK(core::clickAdviceOf(many, few) == core::ClickAdvice::BackOffALittle);
    CHECK(core::clickAdviceOf(many, many) == core::ClickAdvice::DrinkWater);
    CHECK(core::clickAdviceOf(few, many) == core::ClickAdvice::None);
}

TEST_CASE("口水音：恰好等于参考值不标出，多一次才标出", "[voice][clicks][reference]")
{
    // 直接构造 60 s 的扫描结果：每个候选都过三条门限，相隔 0.5 s（远大于合并间隔），各算一次。
    // 60 s 里每分钟次数就等于个数
    const auto scanWith = [](int n) {
        core::ClickScan scan;
        scan.seconds = 60.0;
        scan.speechDb = -20.0;
        for (int i = 0; i < n; ++i)
        {
            core::ClickCandidate c;
            c.timeSec = 0.5 * i;
            c.residualMultiple = 2.0 * thr::click::residual_median_multiple;
            c.relSpeechDb = thr::click::min_rel_speech_db + 10.0;
            c.contextRelDb = thr::click::context_max_rel_db - 10.0;
            scan.candidates.push_back(c);
        }
        return scan;
    };
    const auto n = static_cast<int>(thr::click::reference_max_per_min);
    const auto at = core::summariseClicks(scanWith(n), thr::click::residual_median_multiple);
    const auto above = core::summariseClicks(scanWith(n + 1), thr::click::residual_median_multiple);
    CHECK(at.measures.perMinute == thr::click::reference_max_per_min);
    CHECK_FALSE(at.verdict.overReference);
    CHECK(above.measures.perMinute > thr::click::reference_max_per_min);
    CHECK(above.verdict.overReference);
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
