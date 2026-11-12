// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cmath>

#include "config/Thresholds.h"
#include "voice/Voice.h"
#include "voice/VoiceDsp.h"

namespace miciri::core
{
namespace
{
namespace s = thr::sibilance;

struct Analysis
{
    voice::Spectrogram spec;
    std::vector<double> wideDb; // 每帧宽带电平
    std::vector<double> noise; // 底噪谱：最安静那部分帧的平均
};

Analysis analyse(const std::vector<double>& samples, double sampleRate)
{
    Analysis a;
    double rate = 0.0;
    const auto x = voice::prepare(samples, sampleRate, s::analysis_rate_max_hz, rate);
    a.spec = voice::spectrogram(x, rate, s::frame_ms, s::hop_ms);
    if (a.spec.power.empty())
        return a;
    for (const auto& p : a.spec.power)
        a.wideDb.push_back(dsp::powerToDb(voice::bandSum(p, a.spec.binHz, s::wideband_hz[0], s::wideband_hz[1])));
    const double quiet = dsp::percentile(a.wideDb, s::noise_frame_percentile);
    a.noise.assign(a.spec.power.front().size(), 0.0);
    int count = 0;
    for (std::size_t f = 0; f < a.spec.power.size(); ++f)
    {
        if (a.wideDb[f] > quiet)
            continue;
        for (std::size_t k = 0; k < a.noise.size(); ++k)
            a.noise[k] += a.spec.power[f][k];
        ++count;
    }
    for (auto& v : a.noise)
        v /= std::max(count, 1);
    return a;
}

std::vector<double> frameMean(const voice::Spectrogram& spec, const std::vector<bool>& use)
{
    std::vector<double> mean(spec.power.empty() ? 0 : spec.power.front().size(), 0.0);
    int count = 0;
    for (std::size_t f = 0; f < spec.power.size(); ++f)
    {
        if (!use[f])
            continue;
        for (std::size_t k = 0; k < mean.size(); ++k)
            mean[k] += spec.power[f][k];
        ++count;
    }
    for (auto& v : mean)
        v /= std::max(count, 1);
    return mean;
}

std::vector<SibilantEvent> events(const Analysis& a)
{
    const auto& spec = a.spec;
    const double lo = s::mid_high_band_hz[0];
    const double hi = s::high_band_hz[1];
    const double noiseHf = voice::bandSum(a.noise, spec.binHz, lo, hi);
    std::vector<std::size_t> on;
    for (std::size_t f = 0; f < spec.power.size(); ++f)
    {
        const auto& p = spec.power[f];
        double weighted = 0.0, total = 0.0, logSum = 0.0, linSum = 0.0;
        int bins = 0;
        for (std::size_t k = 0; k < p.size(); ++k)
        {
            const double hz = static_cast<double>(k) * spec.binHz;
            if (hz >= s::centroid_band_hz[0] && hz < s::centroid_band_hz[1])
            {
                weighted += p[k] * hz;
                total += p[k];
            }
            if (hz >= lo && hz < hi)
            {
                logSum += std::log(std::max(p[k], dsp::kPowerFloor));
                linSum += p[k];
                ++bins;
            }
        }
        const double centroid = weighted / std::max(total, dsp::kPowerFloor);
        const double flatness = bins > 0 ? std::exp(logSum / bins) / std::max(linSum / bins, dsp::kPowerFloor) : 0.0;
        const double hf = voice::bandSum(p, spec.binHz, lo, hi);
        const double voiced = voice::bandSum(p, spec.binHz, s::unvoiced_band_hz[0], s::unvoiced_band_hz[1]);
        if (centroid >= s::centroid_min_hz && flatness >= s::flatness_min &&
            hf >= noiseHf * dsp::dbToPower(s::over_noise_db) && hf >= voiced * dsp::dbToPower(s::unvoiced_over_db))
            on.push_back(f);
    }

    const double hopMs = spec.hopSeconds * 1000.0;
    const auto maxGap = static_cast<std::size_t>(std::llround(s::gap_max_ms / hopMs));
    const auto minSpan = static_cast<std::size_t>(std::llround(s::event_min_ms / hopMs));
    const double keepFrac = dsp::dbToPower(s::noise_subtract_floor_db);
    std::vector<SibilantEvent> out;
    for (std::size_t i = 0; i < on.size();)
    {
        std::size_t j = i;
        while (j + 1 < on.size() && on[j + 1] - on[j] <= maxGap + 1)
            ++j;
        const std::size_t first = on[i], last = on[j];
        i = j + 1;
        if (last - first + 1 < minSpan)
            continue;
        // 每帧先减底噪谱、每个 bin 至少留一点底噪，再按帧平均
        double mid = 0.0, high = 0.0;
        for (std::size_t f = first; f <= last; ++f)
        {
            std::vector<double> clean(spec.power[f].size());
            for (std::size_t k = 0; k < clean.size(); ++k)
                clean[k] = std::max(spec.power[f][k] - a.noise[k], a.noise[k] * keepFrac);
            mid += voice::bandSum(clean, spec.binHz, s::mid_high_band_hz[0], s::mid_high_band_hz[1]);
            high += voice::bandSum(clean, spec.binHz, s::high_band_hz[0], s::high_band_hz[1]);
        }
        const auto frames = static_cast<double>(last - first + 1);
        SibilantEvent e;
        e.startSec = static_cast<double>(first) * spec.hopSeconds;
        e.endSec = static_cast<double>(last) * spec.hopSeconds + spec.frameSeconds;
        e.midHighDb = voice::bandLevelDb(mid / frames, spec);
        e.highDb = voice::bandLevelDb(high / frames, spec);
        out.push_back(e);
    }
    return out;
}

double bandwidth(const Analysis& a)
{
    const auto& spec = a.spec;
    if (spec.power.empty())
        return 0.0;
    const double quiet = dsp::percentile(a.wideDb, s::noise_frame_percentile);
    const double active = dsp::percentile(a.wideDb, s::bandwidth_active_percentile) - s::bandwidth_active_within_db;
    std::vector<bool> useQuiet(spec.power.size()), useActive(spec.power.size());
    for (std::size_t f = 0; f < spec.power.size(); ++f)
    {
        useQuiet[f] = a.wideDb[f] <= quiet;
        useActive[f] = a.wideDb[f] >= active;
    }
    const auto speech = frameMean(spec, useActive);
    const auto noise = frameMean(spec, useQuiet);
    const auto half = static_cast<long long>(std::llround(s::bandwidth_smoothing_hz / spec.binHz / 2.0));
    const auto bins = static_cast<long long>(speech.size());
    const auto smooth = [&](const std::vector<double>& v, long long k) {
        double sum = 0.0;
        long long used = 0;
        for (long long o = std::max(0LL, k - half); o <= std::min(bins - 1, k + half); ++o, ++used)
            sum += v[static_cast<std::size_t>(o)];
        return sum / static_cast<double>(std::max(used, 1LL));
    };
    auto k = static_cast<long long>(std::ceil(s::bandwidth_search_from_hz / spec.binHz));
    while (k < bins &&
           dsp::powerToDb(smooth(speech, k)) - dsp::powerToDb(smooth(noise, k)) >= s::bandwidth_over_noise_db)
        ++k;
    return static_cast<double>(std::min(k, bins - 1)) * spec.binHz;
}
} // namespace

std::vector<SibilantEvent> sibilantEvents(const std::vector<double>& samples, double sampleRate)
{
    return events(analyse(samples, sampleRate));
}

double effectiveBandwidthHz(const std::vector<double>& samples, double sampleRate)
{
    return bandwidth(analyse(samples, sampleRate));
}

SibilanceResult analyseSibilance(const std::vector<double>& samples, double sampleRate)
{
    SibilanceResult r;
    const auto a = analyse(samples, sampleRate);
    r.measures.effectiveBandwidthHz = bandwidth(a);
    r.verdict.highBandDisabled = r.measures.effectiveBandwidthHz < s::min_effective_bandwidth_hz;
    const auto found = events(a);
    r.measures.events = static_cast<std::int64_t>(found.size());
    if (found.empty())
    {
        r.confidence = retest(RetestReason::TooShort);
        return r;
    }
    std::vector<double> mid, high;
    for (const auto& e : found)
    {
        mid.push_back(e.midHighDb);
        high.push_back(e.highDb);
        r.evidence.marks.push_back(
            {EvidenceKind::Sibilant, e.startSec, e.endSec, r.verdict.highBandDisabled ? e.midHighDb : e.highDb, -1});
    }
    voice::keepStrongest(r.evidence.marks, static_cast<std::size_t>(s::evidence_max_marks));
    // 绝对电平只用于同一增益下的 A/B 比较；不分档，severity 保持最低档
    r.measures.midHighDb = dsp::median(mid);
    r.measures.highDb = dsp::median(high);
    return r;
}
} // namespace miciri::core
