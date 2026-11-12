// SPDX-License-Identifier: GPL-3.0-or-later
//
// 房间 T20 与位置 D/R：已知 T60 的衰减噪声给解析真值；门限只作废那一下的那个频带；重复性按偏离中位数；
// 削波不影响 T20、D/R 只用没削波的几下；与分块方式无关。
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

#include "config/Thresholds.h"
#include "core/room/ClapSynth.h"
#include "room/Butterworth.h"
#include "room/Room.h"

using namespace miciri;
using namespace miciri::testing;
using core::ClapBandStatus;

namespace
{
int countStatus(const core::ReverbAnalysis& a, ClapBandStatus st)
{
    int n = 0;
    for (const auto& c : a.claps)
        n += static_cast<int>(
            std::count_if(c.bands.begin(), c.bands.end(), [st](const core::ClapBand& b) { return b.status == st; }));
    return n;
}

// 把 [from, to) 秒的样本全部置为严格数字零（门限关上）
void zeroOut(std::vector<double>& x, double fs, double from, double to)
{
    std::fill(x.begin() + static_cast<std::ptrdiff_t>(from * fs), x.begin() + static_cast<std::ptrdiff_t>(to * fs),
              0.0);
}
} // namespace

TEST_CASE("band filters put the band edges at -3 dB at every rate")
{
    for (double fs : kRates)
        for (double fc : thr::reverb::bands_hz)
        {
            INFO("fs " << fs << " fc " << fc);
            const auto f = core::dsp::makeButterworthBandpass(4, fc / std::sqrt(2.0), fc * std::sqrt(2.0), fs);
            CHECK(f.stages().size() == 4);
            CHECK(f.magnitudeDb(fc / std::sqrt(2.0), fs) == within(-3.0103, 0.01));
            CHECK(f.magnitudeDb(fc * std::sqrt(2.0), fs) == within(-3.0103, 0.01));
            CHECK(f.magnitudeDb(fc / 4.0, fs) < -40.0);
        }
}

TEST_CASE("T20 recovers a known T60 in every band at every rate")
{
    // 衰减噪声的每一次实现都有随机起伏（500 Hz 倍频带最窄，单下约 ±5%），所以取四段独立录音的平均来对真值；
    // 500 Hz 放宽到 8%，其余 5%。同一批录音里一次门限也不许误报。
    for (double fs : {44100.0, 48000.0, 96000.0, 192000.0})
        for (double t60 : {0.2, 0.4, 0.8})
        {
            INFO("fs " << fs << " T60 " << t60);
            std::array<double, 4> sum{};
            double tMid = 0.0;
            for (std::uint64_t seed = 1; seed <= 4; ++seed)
            {
                ClapTrain c;
                c.fs = fs;
                c.seed = seed;
                c.t60 = [t60](int) { return t60; };
                const core::ReverbAnalysis a = core::analyseReverb(makeClaps(c), fs);
                REQUIRE(a.claps.size() == 8);
                REQUIRE(a.result.confidence.level == core::Reliability::Trusted);
                CHECK(countStatus(a, ClapBandStatus::Gated) == 0);
                for (std::size_t b = 0; b < 4; ++b)
                    sum[b] += a.result.measures.t20Sec[b] / 4.0;
                tMid += a.result.measures.tMidSec / 4.0;
            }
            CHECK(sum[0] == within(t60, 0.08 * t60));
            for (std::size_t b = 1; b < 4; ++b)
                CHECK(sum[b] == within(t60, 0.05 * t60));
            CHECK(tMid == within(t60, 0.05 * t60));
        }
}

TEST_CASE("T20 bands follow the starting thresholds")
{
    const auto bandFor = [](double t60) {
        ClapTrain c;
        c.t60 = [t60](int) { return t60; };
        return core::analyseReverb(makeClaps(c), c.fs).result.verdict.band;
    };
    CHECK(bandFor(0.18) == core::ReverbBand::Dry);
    CHECK(bandFor(0.35) == core::ReverbBand::Normal);
    CHECK(bandFor(0.55) == core::ReverbBand::Boomy);
    CHECK(bandFor(0.9) == core::ReverbBand::VeryBoomy);
}

TEST_CASE("a strong direct sound is not taken for a gate")
{
    for (double fs : {44100.0, 96000.0})
    {
        ClapTrain c;
        c.fs = fs;
        c.tailGain = 0.004; // 直达比尾巴强得多（近处拍手）
        c.t60 = [](int) { return 0.2; };
        CHECK(countStatus(core::analyseReverb(makeClaps(c), fs), ClapBandStatus::Gated) == 0);
    }
}

TEST_CASE("digital zeros in one tail void only that clap, not the whole set")
{
    ClapTrain c;
    std::vector<double> x = makeClaps(c);
    zeroOut(x, c.fs, clapTime(c, 3) + 0.12, clapTime(c, 3) + 1.0);
    const core::ReverbAnalysis a = core::analyseReverb(x, c.fs);
    for (std::size_t i = 0; i < a.claps.size(); ++i)
        for (const auto& b : a.claps[i].bands)
            CHECK((b.status == ClapBandStatus::Gated) == (i == 3));
    CHECK(a.result.confidence.level == core::Reliability::Trusted);
    CHECK(a.result.verdict.gated == false);
    CHECK(a.result.measures.tMidSec == within(0.4, 0.02));
}

TEST_CASE("a gate in one band voids only that band")
{
    // 尾巴分成低频（350-1400 Hz）与高频（3.4-5 kHz）两部分，只有高频那部分在 100 ms 后被压掉 60 dB
    ClapTrain c;
    c.tailGain = 0.0;
    std::vector<double> x = makeClaps(c);
    ClapTrain tail = c;
    tail.direct = 0.0;
    tail.tailGain = 0.02;
    tail.noiseRms = 0.0;
    std::vector<double> low = makeClaps(tail);
    tail.seed = 99;
    tail.tailShape = [](int, double t) { return t < 0.1 ? 1.0 : 1e-3; };
    std::vector<double> high = makeClaps(tail);
    auto lowFilter = core::dsp::makeButterworthBandpass(4, 350.0, 1400.0, c.fs);
    auto highFilter = core::dsp::makeButterworthBandpass(4, 3400.0, 5000.0, c.fs);
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] += lowFilter.process(low[i]) + highFilter.process(high[i]);

    const core::ReverbAnalysis a = core::analyseReverb(x, c.fs);
    REQUIRE(a.claps.size() == 8);
    for (const auto& clap : a.claps)
    {
        CHECK(clap.bands[0].status == ClapBandStatus::Valid);
        CHECK(clap.bands[1].status == ClapBandStatus::Valid);
        CHECK(clap.bands[3].status == ClapBandStatus::Gated);
    }
    CHECK(a.result.confidence.level == core::Reliability::Trusted);
    CHECK(a.result.verdict.gated == false);
    CHECK(a.result.measures.t20Sec[3] == 0.0);
    CHECK(a.result.measures.tMidSec == within(0.4, 0.04));
}

TEST_CASE("gates in most claps leave the main bands short and mark the result gated")
{
    ClapTrain c;
    std::vector<double> x = makeClaps(c);
    for (int i = 0; i < 6; ++i)
        zeroOut(x, c.fs, clapTime(c, i) + 0.12, clapTime(c, i) + 1.0);
    const core::ReverbAnalysis a = core::analyseReverb(x, c.fs);
    CHECK(a.result.confidence.level == core::Reliability::Retest);
    CHECK(a.result.confidence.reason == core::RetestReason::GateDetected);
    CHECK(a.result.verdict.gated);
    CHECK(core::toneOf(a.result.verdict) == core::Tone::None);
}

TEST_CASE("repeatability drops claps far from the median, not the whole band")
{
    SECTION("a wide but consistent spread is kept (max/min above 1.4)")
    {
        ClapTrain c;
        c.t60 = [](int i) { return i % 2 ? 0.47 : 0.33; };
        const core::ReverbAnalysis a = core::analyseReverb(makeClaps(c), c.fs);
        CHECK(a.result.confidence.level == core::Reliability::Trusted);
        CHECK(countStatus(a, ClapBandStatus::Outlier) == 0);
        CHECK(a.result.measures.repeatMaxOverMin > 1.4);
    }
    SECTION("two wild claps are dropped as outliers")
    {
        ClapTrain c;
        c.t60 = [](int i) { return i == 2 || i == 5 ? 0.9 : 0.4; };
        const core::ReverbAnalysis a = core::analyseReverb(makeClaps(c), c.fs);
        CHECK(a.result.confidence.level == core::Reliability::Trusted);
        for (const auto& b : a.claps[2].bands)
            CHECK(b.status == ClapBandStatus::Outlier);
        CHECK(a.result.measures.tMidSec == within(0.4, 0.02));
    }
    SECTION("no three claps agree")
    {
        ClapTrain c;
        c.claps = 5;
        c.t60 = [](int i) { return std::array<double, 5>{0.2, 0.28, 0.45, 0.7, 1.0}[static_cast<std::size_t>(i)]; };
        const core::ReverbAnalysis a = core::analyseReverb(makeClaps(c), c.fs);
        CHECK(a.result.confidence.reason == core::RetestReason::Unstable);
        CHECK(a.result.verdict.gated == false);
    }
}

TEST_CASE("too few claps and too little peak-to-noise ask for a retest")
{
    ClapTrain few;
    few.claps = 2;
    CHECK(core::analyseReverb(makeClaps(few), few.fs).result.confidence.reason == core::RetestReason::FewClaps);

    ClapTrain noisy;
    noisy.noiseRms = noisy.tailGain * fromDb(-25.0);
    const core::ReverbAnalysis a = core::analyseReverb(makeClaps(noisy), noisy.fs);
    CHECK(a.result.confidence.reason == core::RetestReason::LowPnr);
    CHECK(a.result.measures.bandPnrDb[0] < thr::reverb::band_pnr_min_db);
}

TEST_CASE("clipping the direct sound leaves T20 alone")
{
    // 削波的噪声猝发作直达；尾巴调响一些，让直达与尾巴的比例和正常拍手相近
    ClapTrain c;
    c.direct = 0.0;
    c.tailGain = 0.3;
    std::vector<double> x = makeClaps(c);
    Rng rng(5);
    for (int i = 0; i < c.claps; ++i)
    {
        const auto at = static_cast<std::size_t>(clapTime(c, i) * c.fs);
        for (std::size_t n = 0; n < 48; ++n)
            x[at + n] += 4.0 * rng.gauss();
    }
    for (double& v : x)
        v = std::clamp(v, -1.0, 1.0); // 像转换器一样整段限幅
    const core::ReverbAnalysis a = core::analyseReverb(x, c.fs);
    REQUIRE(a.claps.size() == 8);
    for (const auto& clap : a.claps)
        CHECK(clap.clipped);
    CHECK(a.result.confidence.level == core::Reliability::Trusted);
    CHECK(a.result.measures.tMidSec == within(0.4, 0.04));
}

TEST_CASE("D/R matches the analytic ratio and its bands")
{
    for (double fs : {44100.0, 48000.0, 96000.0})
        for (double target : {-2.0, 8.0, 18.0})
        {
            INFO("fs " << fs << " D/R " << target);
            ClapTrain c;
            c.fs = fs;
            c.claps = 5;
            c.tailDelayMs = 15.0; // 尾巴整个落在直达窗之后
            c.tailGain = 0.002;
            c.t60 = [](int) { return 0.4; };
            // 直达能量 direct^2，尾巴能量由解析式给出：按目标比值反推直达幅度
            c.direct = std::sqrt(tailEnergyFrom(c, 0.4, 0.0) * std::pow(10.0, target / 10.0));
            const core::DirectRatioAnalysis a = core::analyseDirectRatio(makeClaps(c), fs);
            REQUIRE(a.result.confidence.level == core::Reliability::Trusted);
            CHECK(a.result.measures.drDb == within(target, 1.0));
            CHECK_FALSE(a.result.measures.lowerBoundOnly);
            const core::DrBand want =
                target < 5.0 ? core::DrBand::Low : (target > 12.0 ? core::DrBand::High : core::DrBand::Mid);
            CHECK(a.result.verdict.band == want);
        }
}

TEST_CASE("D/R uses only the clean claps")
{
    ClapTrain c;
    c.claps = 5;
    c.tailDelayMs = 15.0;
    c.tailGain = 0.002;
    c.direct = std::sqrt(tailEnergyFrom(c, 0.4, 0.0) * std::pow(10.0, 1.0));
    std::vector<double> x = makeClaps(c);
    const auto clip = [&](int i) {
        const auto at = static_cast<std::size_t>(clapTime(c, i) * c.fs);
        for (std::size_t n = 0; n < 24; ++n)
            x[at + n] = n % 2 ? -1.0 : 1.0;
    };
    clip(1);
    clip(3);
    const core::DirectRatioAnalysis a = core::analyseDirectRatio(x, c.fs);
    REQUIRE(a.claps.size() == 5);
    CHECK(a.claps[1].clipped);
    CHECK(a.claps[3].clipped);
    CHECK(a.result.evidence.marks.size() == 2);
    CHECK(a.result.confidence.level == core::Reliability::Trusted);
    CHECK(a.result.measures.drDb == within(10.0, 1.0));

    for (int i : {0, 2, 4})
        clip(i);
    const core::DirectRatioAnalysis all = core::analyseDirectRatio(x, c.fs);
    CHECK(all.result.confidence.reason == core::RetestReason::Clipped);
}

TEST_CASE("an unmeasurable tail gives only a lower bound")
{
    ClapTrain c;
    c.claps = 4;
    c.tailGain = 1e-6; // 尾巴比底噪还低
    const core::DirectRatioAnalysis a = core::analyseDirectRatio(makeClaps(c), c.fs);
    CHECK(a.result.measures.lowerBoundOnly);
    CHECK(a.result.verdict.band == core::DrBand::High);
    CHECK(a.result.confidence.level == core::Reliability::Trusted);
}

TEST_CASE("results do not depend on how the samples are split into blocks")
{
    ClapTrain c;
    c.fs = 96000.0;
    c.claps = 4;
    const std::vector<double> x = makeClaps(c);
    core::ClapAnalyser blocks(c.fs);
    feedInRandomBlocks(blocks, x, 11);
    const core::ReverbAnalysis whole = core::analyseReverb(x, c.fs);
    const core::ReverbAnalysis split = blocks.finishReverb();
    for (std::size_t b = 0; b < 4; ++b)
        CHECK(split.result.measures.t20Sec[b] == whole.result.measures.t20Sec[b]);
    CHECK(blocks.finishDirectRatio().result.measures.drDb == core::analyseDirectRatio(x, c.fs).result.measures.drDb);
}
