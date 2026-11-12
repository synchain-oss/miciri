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
namespace p = thr::plosive;

// 起音帧序号：前面一小段足够安静，当前帧比前面的最低点高出 onset_rise_db，且明显高于底噪。
// 找到后往回退到刚开始上升的那一帧；离上一个起音太近的（爆发后紧跟元音）并进同一个音节。
std::vector<long long> onsetFrames(const std::vector<double>& wide, double floorDb, double hopMs)
{
    const auto frames = [hopMs](double ms) { return std::llround(ms / hopMs); };
    const long long nearBack = frames(p::onset_baseline_ms[0]);
    const long long farBack = frames(p::onset_baseline_ms[1]);
    const long long quiet = frames(p::quiet_before_ms);
    const long long refractory = std::max<long long>(1, frames(p::onset_refractory_ms));
    const auto size = static_cast<long long>(wide.size());
    const auto at = [&wide](long long i) { return wide[static_cast<std::size_t>(i)]; };
    std::vector<long long> out;
    for (long long i = farBack; i < size;)
    {
        double base = at(i - farBack);
        for (long long j = i - farBack; j < i - nearBack; ++j)
            base = std::min(base, at(j));
        bool noisyBefore = false;
        if (i >= quiet)
            for (long long j = i - quiet; j < i - nearBack; ++j)
                noisyBefore = noisyBefore || at(j) > floorDb + p::quiet_before_over_floor_db;
        if (noisyBefore || at(i) - base < p::onset_rise_db || at(i) < floorDb + p::onset_over_floor_db)
        {
            ++i;
            continue;
        }
        long long j = i;
        while (j > i - farBack && at(j - 1) > base + p::onset_backtrack_db)
            --j;
        if (out.empty() || static_cast<double>(j - out.back()) * hopMs >= p::syllable_merge_ms)
            out.push_back(j);
        i += refractory;
    }
    return out;
}
} // namespace

std::vector<PlosiveEvent> plosiveEvents(const std::vector<double>& samples, double sampleRate)
{
    double rate = 0.0;
    const auto x = voice::prepare(samples, sampleRate, p::analysis_rate_max_hz, rate);
    const int order = static_cast<int>(p::filter_order);
    const auto n = static_cast<std::size_t>(std::max<long long>(1, voice::offsetSamples(p::envelope_frame_ms, rate)));
    const auto hop = static_cast<std::size_t>(std::max<long long>(1, voice::offsetSamples(p::envelope_hop_ms, rate)));
    const double hopMs = static_cast<double>(hop) * 1000.0 / rate;
    const auto frames = [hopMs](double ms) { return std::llround(ms / hopMs); };

    const auto wide =
        voice::framePowerDb(voice::filtered(voice::butterworth(order, p::envelope_highpass_hz, rate, true), x), n, hop);
    const auto low = voice::framePowerDb(
        voice::filtered(voice::bandpass(order, p::event_band_hz[0], p::event_band_hz[1], rate), x), n, hop);
    const auto vowel = voice::framePowerDb(
        voice::filtered(voice::bandpass(order, p::vowel_band_hz[0], p::vowel_band_hz[1], rate), x), n, hop);
    if (wide.empty())
        return {};
    const double floorDb = dsp::percentile(wide, p::floor_percentile);
    const double lowFloorDb = dsp::percentile(low, p::floor_percentile);
    const auto size = static_cast<long long>(wide.size());
    const long long frameSpan = std::llround(static_cast<double>(n) / static_cast<double>(hop));

    const auto onsets = onsetFrames(wide, floorDb, hopMs);
    std::vector<PlosiveEvent> events;
    for (std::size_t a = 0; a < onsets.size(); ++a)
    {
        const long long j = onsets[a];
        const long long next = a + 1 < onsets.size() ? onsets[a + 1] : size;
        // 音节结束：至少 event_min_ms，之后一直到包络落回底噪附近或下一个起音
        long long k = j + frames(p::event_min_ms);
        while (k < next && wide[static_cast<std::size_t>(k)] > floorDb + p::event_end_over_floor_db)
            ++k;
        const long long end = std::min(k, next) + frameSpan;

        const long long peakFrom = std::max<long long>(0, j + frames(p::peak_window_ms[0]));
        const long long peakTo = std::min(size, j + frames(p::peak_window_ms[1]));
        const long long v0 = j + frames(p::vowel_window_ms[0]);
        const long long v1 = std::min({end, j + frames(p::vowel_window_ms[1]), size});
        if (peakTo <= peakFrom || v1 - v0 < frames(p::vowel_min_ms))
            continue;
        const double peak = *std::max_element(low.begin() + peakFrom, low.begin() + peakTo);

        // 元音稳态：起音之后最响的几帧（比该段最响帧低不超过 vowel_within_db）
        const double loudest = *std::max_element(vowel.begin() + v0, vowel.begin() + v1);
        std::vector<double> steady;
        for (long long f = v0; f < v1; ++f)
            if (vowel[static_cast<std::size_t>(f)] >= loudest - p::vowel_within_db)
                steady.push_back(low[static_cast<std::size_t>(f)]);
        if (static_cast<double>(steady.size()) < p::vowel_min_frames)
            continue;
        // 元音几乎没有低频时，分母取底噪加一点余量，不让比值被底噪放大
        const double reference = std::max(dsp::median(steady), lowFloorDb + p::reference_floor_margin_db);
        PlosiveEvent e;
        e.startSec = static_cast<double>(j) * static_cast<double>(hop) / rate;
        e.endSec = static_cast<double>(end) * static_cast<double>(hop) / rate;
        e.ratioDb = peak - reference;
        events.push_back(e);
    }
    return events;
}

PlosiveResult analysePlosive(const std::vector<double>& samples, double sampleRate)
{
    PlosiveResult r;
    const auto events = plosiveEvents(samples, sampleRate);
    r.measures.events = static_cast<std::int64_t>(events.size());
    if (events.empty())
    {
        r.confidence = retest(RetestReason::TooShort);
        return r;
    }
    double worst = events.front().ratioDb;
    for (const auto& e : events)
    {
        worst = std::max(worst, e.ratioDb);
        r.evidence.marks.push_back({EvidenceKind::Plosive, e.startSec, e.endSec, e.ratioDb, -1});
    }
    voice::keepStrongest(r.evidence.marks, static_cast<std::size_t>(p::evidence_max_marks));
    r.measures.worstRatioDb = worst;
    if (worst > p::heavy_above_db)
        r.verdict.severity = Severity::Heavy;
    else if (worst > p::moderate_above_db)
        r.verdict.severity = Severity::Moderate;
    return r;
}
} // namespace miciri::core
