// SPDX-License-Identifier: GPL-3.0-or-later
//
// 齿音：擦音段的检出、两段绝对电平（只供固定增益下的 A/B），以及有效带宽“高出底噪 ≥10 dB 的最高频率”。
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "config/Thresholds.h"
#include "core/voice/VoiceSynth.h"
#include "voice/Voice.h"

using namespace miciri;
using namespace miciri::testing;

namespace
{
constexpr double kHissRms = 0.05;

// 六个 150 ms 的白噪声擦音，夹在元音之间；元音峰值约为 vowelAmplitude
std::vector<double> sibilantTake(double fs, double vowelAmplitude, double gain = 1.0)
{
    std::vector<double> x = whiteNoise(static_cast<std::size_t>(8.0 * fs), 3e-5, 21);
    for (int i = 0; i < 6; ++i)
    {
        const double t = 0.5 + 1.2 * i;
        addNoiseBurst(x, fs, t, 0.15, kHissRms, 100 + static_cast<std::uint64_t>(i));
        addVowel(x, fs, t + 0.2, 0.3, 140.0, vowelAmplitude);
    }
    for (double& v : x)
        v *= gain;
    return x;
}

double bandDb(double rms, double loHz, double hiHz, double fs)
{
    return 10.0 * std::log10(rms * rms * (hiHz - loHz) / (fs / 2.0));
}

// 正弦组成的“说话”：0.5 s 有、0.5 s 无，交替两次；幅度按 1/f 递减，直到 topHz
std::vector<double> bandLimitedSpeech(double fs, double topHz)
{
    std::vector<double> x = whiteNoise(static_cast<std::size_t>(2.0 * fs), 3e-5, 31);
    addSineBand(x, fs, 100.0, topHz, 50.0, 0.05, 0.25, 0.75, 41);
    addSineBand(x, fs, 100.0, topHz, 50.0, 0.05, 1.25, 1.75, 42);
    return x;
}
} // namespace

TEST_CASE("齿音：每个擦音一个事件，两段电平就是该频带成分的均方电平", "[voice][sibilance]")
{
    const double fs = 48000.0;
    const auto r = core::analyseSibilance(sibilantTake(fs, 0.3), fs);
    CHECK(r.measures.events == 6);
    CHECK(r.confidence.level == core::Reliability::Trusted);
    // 白噪声：3-6 kHz 占总功率的 3/24，6-12 kHz 占 6/24；擦音两端的起落让平均值略低
    CHECK(r.measures.midHighDb == within(bandDb(kHissRms, 3000.0, 6000.0, fs), 1.0));
    CHECK(r.measures.highDb == within(bandDb(kHissRms, 6000.0, 12000.0, fs), 1.0));
    CHECK(r.verdict.severity == core::Severity::Light); // 只做 A/B，不分档
    REQUIRE(r.evidence.marks.size() == 6);
    CHECK(r.evidence.marks[0].kind == core::EvidenceKind::Sibilant);
    CHECK(r.evidence.marks[0].startSec == within(0.5, 0.03));
}

TEST_CASE("齿音：固定增益下的绝对电平随录音电平变化，不随元音变化", "[voice][sibilance]")
{
    const double fs = 48000.0;
    const auto base = core::analyseSibilance(sibilantTake(fs, 0.3), fs);
    // 整段衰减 6 dB（挪远、偏轴）：两段电平同样下降 6 dB
    const auto softer = core::analyseSibilance(sibilantTake(fs, 0.3, 0.5), fs);
    CHECK(base.measures.midHighDb - softer.measures.midHighDb == within(6.02, 0.05));
    CHECK(base.measures.highDb - softer.measures.highDb == within(6.02, 0.05));
    // 只有元音变响（近讲效应抬高低频）：齿音读数不变。对元音的比值会在这里变化 10 dB
    const auto louderVowels = core::analyseSibilance(sibilantTake(fs, 0.95), fs);
    CHECK(louderVowels.measures.events == base.measures.events);
    CHECK(louderVowels.measures.midHighDb == within(base.measures.midHighDb, 0.3));
    CHECK(louderVowels.measures.highDb == within(base.measures.highDb, 0.3));
}

TEST_CASE("齿音：只有元音时没有事件，要求再测", "[voice][sibilance]")
{
    const double fs = 48000.0;
    std::vector<double> x = whiteNoise(static_cast<std::size_t>(4.0 * fs), 3e-5, 5);
    for (int i = 0; i < 3; ++i)
        addVowel(x, fs, 0.5 + 1.1 * i, 0.3, 140.0, 0.3);
    const auto r = core::analyseSibilance(x, fs);
    CHECK(r.measures.events == 0);
    CHECK(r.confidence.reason == core::RetestReason::TooShort);
}

TEST_CASE("有效带宽：高出底噪 10 dB 的最高频率，不按能量占比", "[voice][sibilance][bandwidth]")
{
    const double fs = 48000.0;
    // 能量几乎全在 1 kHz 以下（1/f 幅度），但高频一直高出底噪：按能量占比会误判成窄带
    const double full = core::effectiveBandwidthHz(bandLimitedSpeech(fs, 20000.0), fs);
    CHECK(full > 19000.0);
    const double narrow = core::effectiveBandwidthHz(bandLimitedSpeech(fs, 7000.0), fs);
    CHECK(narrow == within(7000.0, 500.0));

    auto r = core::analyseSibilance(bandLimitedSpeech(fs, 7000.0), fs);
    CHECK(r.verdict.highBandDisabled);
    CHECK(r.measures.effectiveBandwidthHz < thr::sibilance::min_effective_bandwidth_hz);
    r = core::analyseSibilance(bandLimitedSpeech(fs, 20000.0), fs);
    CHECK_FALSE(r.verdict.highBandDisabled);
}

TEST_CASE("齿音与有效带宽：各采样率结果一致", "[voice][sibilance]")
{
    for (double fs : kRates)
    {
        const auto r = core::analyseSibilance(sibilantTake(fs, 0.3), fs);
        INFO("fs " << fs);
        CHECK(r.measures.events == 6);
        // 白噪声的功率按原采样率的奈奎斯特频率摊开，期望值随采样率变
        CHECK(r.measures.midHighDb == within(bandDb(kHissRms, 3000.0, 6000.0, fs), 1.0));
        CHECK(r.measures.highDb == within(bandDb(kHissRms, 6000.0, 12000.0, fs), 1.0));
        CHECK(core::effectiveBandwidthHz(bandLimitedSpeech(fs, 7000.0), fs) == within(7000.0, 500.0));
    }
}
