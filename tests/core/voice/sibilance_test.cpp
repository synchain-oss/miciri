// SPDX-License-Identifier: GPL-3.0-or-later
//
// 齿音：擦音段的检出、两段绝对电平（只供固定增益下的 A/B），以及有效带宽“高出底噪 ≥10 dB 的最高频率”。
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>

#include "config/Thresholds.h"
#include "core/voice/VoiceSynth.h"
#include "voice/Voice.h"
#include "voice/VoiceDsp.h"

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

TEST_CASE("齿音：对元音的比值随元音变化，超过参考值才标出", "[voice][sibilance][reference]")
{
    const double fs = 48000.0;
    const auto quietVowels = core::analyseSibilance(sibilantTake(fs, 0.03), fs);
    const auto louderVowels = core::analyseSibilance(sibilantTake(fs, 0.3), fs);
    INFO("quiet vowels " << quietVowels.measures.highOverVowelDb << " dB, louder "
                         << louderVowels.measures.highOverVowelDb);
    CHECK(quietVowels.measures.vowelEvents == 6);
    CHECK(louderVowels.measures.vowelEvents == 6);
    // 只有元音变了：比值跟着元音走 20 dB，绝对电平不变
    CHECK(quietVowels.measures.highOverVowelDb - louderVowels.measures.highOverVowelDb == within(20.0, 0.5));
    CHECK(quietVowels.measures.highDb == within(louderVowels.measures.highDb, 0.3));
    // 两边都离参考值几个 dB：元音轻时超过，元音响时不超过
    CHECK(quietVowels.measures.highOverVowelDb > thr::sibilance::reference_max_db + 3.0);
    CHECK(quietVowels.verdict.overReference);
    CHECK(louderVowels.measures.highOverVowelDb < thr::sibilance::reference_max_db - 3.0);
    CHECK_FALSE(louderVowels.verdict.overReference);
}

TEST_CASE("齿音：擦音后面没有元音时没有元音参考，不标超过", "[voice][sibilance][reference]")
{
    const double fs = 48000.0;
    std::vector<double> x = whiteNoise(static_cast<std::size_t>(8.0 * fs), 3e-5, 21);
    for (int i = 0; i < 6; ++i)
        addNoiseBurst(x, fs, 0.5 + 1.2 * i, 0.15, kHissRms, 100 + static_cast<std::uint64_t>(i));
    const auto r = core::analyseSibilance(x, fs);
    CHECK(r.measures.events == 6);
    CHECK(r.measures.vowelEvents == 0);
    CHECK_FALSE(r.verdict.overReference);
}

TEST_CASE("齿音：有效带宽不到 10 kHz 时高频项关闭，比值超过参考值也不标出", "[voice][sibilance][reference]")
{
    // 16 kHz 采样：有效带宽到不了 10 kHz。擦音照样检得出、元音很轻，比值本来超过参考值
    const double fs = 16000.0;
    const auto r = core::analyseSibilance(sibilantTake(fs, 0.03), fs);
    INFO("bandwidth " << r.measures.effectiveBandwidthHz << " Hz, over vowel " << r.measures.highOverVowelDb << " dB");
    CHECK(r.measures.events == 6);
    CHECK(r.measures.vowelEvents == 6);
    CHECK(r.measures.highOverVowelDb > thr::sibilance::reference_max_db + 3.0);
    CHECK(r.verdict.highBandDisabled);
    CHECK_FALSE(r.verdict.overReference);
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

TEST_CASE("有效带宽：说话几乎不停时底噪不被人声抬高，一个窄谷不会误停", "[voice][sibilance][bandwidth]")
{
    const double fs = 48000.0;
    // 3 s 里说话占 95%，只在中间留 150 ms 的空：最安静的 20% 帧里大半是人声，按它们的平均当底噪会把带宽判得很窄
    std::vector<double> x = whiteNoise(static_cast<std::size_t>(3.0 * fs), 3e-5, 51);
    addSineBand(x, fs, 100.0, 16000.0, 50.0, 0.05, 0.0, 1.4, 52);
    addSineBand(x, fs, 100.0, 16000.0, 50.0, 0.05, 1.55, 3.0, 53);
    CHECK(core::effectiveBandwidthHz(x, fs) == within(16000.0, 500.0));

    // 7-9 kHz 缺一段：7.5、8.5 kHz 两个频点的频带边上还落着两侧最近的正弦的主瓣（10 ms 帧，主瓣约 ±200 Hz），
    // 只有 8 kHz 频点的频带离两侧都有 500 Hz，里面没有人声。只有这一个频点不满足，接着往上仍高出底噪，不停在这里
    std::vector<double> gap = whiteNoise(static_cast<std::size_t>(2.0 * fs), 3e-5, 61);
    for (const double start : {0.25, 1.25})
    {
        addSineBand(gap, fs, 100.0, 7000.0, 50.0, 0.05, start, start + 0.5, 62);
        addSineBand(gap, fs, 9000.0, 16000.0, 50.0, 0.05, start, start + 0.5, 63);
    }
    CHECK(core::effectiveBandwidthHz(gap, fs) == within(16000.0, 500.0));
}

TEST_CASE("有效带宽：一段数字静音不把底噪拉到零", "[voice][sibilance][bandwidth]")
{
    const double fs = 48000.0;
    // 7 kHz 以下的人声，开头 150 ms 是数字零（噪声门、录音开头的空白）。底噪若把这些帧也算进低百分位就是零，
    // 每个频点都“高出底噪”，带宽一路报到最高，窄带话筒的高频项不再关闭
    std::vector<double> x = bandLimitedSpeech(fs, 7000.0);
    std::fill(x.begin(), x.begin() + static_cast<std::ptrdiff_t>(0.15 * fs), 0.0);
    CHECK(core::effectiveBandwidthHz(x, fs) == within(7000.0, 500.0));
    CHECK(core::analyseSibilance(x, fs).verdict.highBandDisabled);
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

TEST_CASE("有效带宽：只有底噪时为 0，高频项关闭", "[voice][sibilance][bandwidth]")
{
    const double fs = 48000.0;
    const auto noise = whiteNoise(static_cast<std::size_t>(3.0 * fs), 1e-3, 7);
    CHECK(core::effectiveBandwidthHz(noise, fs) == 0.0);
    CHECK(core::analyseSibilance(noise, fs).verdict.highBandDisabled);
}

TEST_CASE("齿音与有效带宽：22.05 kHz 时结果有限，带宽不超过奈奎斯特频率", "[voice][sibilance]")
{
    const double fs = 22050.0;
    const auto r = core::analyseSibilance(sibilantTake(fs, 0.3), fs);
    CHECK(r.measures.events == 6);
    CHECK(std::isfinite(r.measures.midHighDb));
    CHECK(std::isfinite(r.measures.highDb));
    CHECK(r.measures.effectiveBandwidthHz <= fs / 2.0);
    CHECK(core::effectiveBandwidthHz(bandLimitedSpeech(fs, 7000.0), fs) == within(7000.0, 500.0));
}

TEST_CASE("齿音：频带求和只走带内的 bin，结果与逐个判断逐位相同", "[voice][sibilance]")
{
    const std::vector<double> power = whiteNoise(257, 1.0, 77);
    const auto direct = [&power](double binHz, double lo, double hi) {
        double sum = 0.0;
        for (std::size_t k = 0; k < power.size(); ++k)
        {
            const double f = static_cast<double>(k) * binHz;
            if (f >= lo && f < hi)
                sum += power[k];
        }
        return sum;
    };
    for (double binHz : {100.0, 93.75, 91.875, 0.0})
        for (const auto& band : std::vector<std::array<double, 2>>{{3000.0, 6000.0},
                                                                   {6000.0, 12000.0},
                                                                   {0.0, 100.0},
                                                                   {-50.0, 1e9},
                                                                   {2250.0, 3281.25},
                                                                   {5000.0, 5000.0},
                                                                   {7000.0, 6000.0},
                                                                   {30000.0, 40000.0}})
        {
            INFO("bin " << binHz << " band " << band[0] << ".." << band[1]);
            CHECK(core::voice::bandSum(power, binHz, band[0], band[1]) == direct(binHz, band[0], band[1]));
        }
}
