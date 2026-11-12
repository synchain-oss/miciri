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
namespace c = thr::click;

// 正态分布的绝对中位差换成标准差的因子（Φ⁻¹(3/4)）
constexpr double kMadToSigma = 0.6744897501960817;

double dbToAmplitude(double db)
{
    return std::sqrt(dsp::dbToPower(db));
}

// 逐块的线性预测残差：每 lpc_block_ms 一块，用以块为中心、lpc_window_ms 长的汉宁窗段估自相关，
// Levinson-Durbin 解出预测系数，再对这一块做逆滤波（用真实的前文样本）。全静音的块残差留 0。
std::vector<double> lpcResidual(const std::vector<double>& x, double rate)
{
    const auto order = static_cast<std::size_t>(c::lpc_order);
    const auto block = static_cast<std::size_t>(std::max<long long>(1, voice::offsetSamples(c::lpc_block_ms, rate)));
    const auto win = static_cast<std::size_t>(std::max<long long>(2, voice::offsetSamples(c::lpc_window_ms, rate)));
    std::vector<double> window(win), seg(win), r(order + 1), a(order + 1), prev(order + 1);
    for (std::size_t i = 0; i < win; ++i)
        window[i] = 0.5 - 0.5 * std::cos(2.0 * dsp::kPi * static_cast<double>(i) / static_cast<double>(win - 1));
    std::vector<double> e(x.size(), 0.0);
    for (std::size_t s = 0; s + block <= x.size(); s += block)
    {
        const std::size_t centre = s + block / 2;
        const std::size_t from = centre > win / 2 ? centre - win / 2 : 0;
        for (std::size_t i = 0; i < win; ++i)
            seg[i] = from + i < x.size() ? x[from + i] * window[i] : 0.0;
        for (std::size_t lag = 0; lag <= order; ++lag)
        {
            double sum = 0.0;
            for (std::size_t i = lag; i < win; ++i)
                sum += seg[i] * seg[i - lag];
            r[lag] = sum;
        }
        if (r[0] <= dsp::kPowerFloor)
            continue;
        r[0] *= 1.0 + c::lpc_white_noise_frac;
        std::fill(a.begin(), a.end(), 0.0);
        double err = r[0];
        bool stable = true;
        for (std::size_t i = 1; i <= order && stable; ++i)
        {
            double acc = r[i];
            for (std::size_t j = 1; j < i; ++j)
                acc -= a[j] * r[i - j];
            const double k = acc / err;
            prev = a;
            a[i] = k;
            for (std::size_t j = 1; j < i; ++j)
                a[j] = prev[j] - k * prev[i - j];
            err *= 1.0 - k * k;
            stable = err > 0.0;
        }
        if (!stable)
            continue;
        for (std::size_t n = s; n < s + block; ++n)
        {
            double predicted = 0.0;
            for (std::size_t i = 1; i <= order && i <= n; ++i)
                predicted += a[i] * x[n - i];
            e[n] = x[n] - predicted;
        }
    }
    return e;
}

// 局部残差尺度：每块 |e| 的中位数，再取前后 scale_halfwidth_ms 内各块中位数的中位数（两端按边缘值延拓）。
// 只覆盖整块部分，返回的长度是块数 × 块长。
std::vector<double> residualScale(const std::vector<double>& e, double rate)
{
    const auto block = static_cast<std::size_t>(std::max<long long>(1, voice::offsetSamples(c::scale_block_ms, rate)));
    const std::size_t blocks = e.size() / block;
    std::vector<double> medians(blocks), tmp;
    for (std::size_t b = 0; b < blocks; ++b)
    {
        tmp.assign(e.begin() + static_cast<std::ptrdiff_t>(b * block),
                   e.begin() + static_cast<std::ptrdiff_t>((b + 1) * block));
        for (auto& v : tmp)
            v = std::fabs(v);
        medians[b] = dsp::median(tmp);
    }
    const auto half = std::llround(c::scale_halfwidth_ms / c::scale_block_ms);
    const double minimum = std::sqrt(dsp::kPowerFloor);
    std::vector<double> scale(blocks * block);
    for (std::size_t b = 0; b < blocks; ++b)
    {
        tmp.clear();
        for (long long o = -half; o <= half; ++o)
            tmp.push_back(medians[static_cast<std::size_t>(
                std::clamp<long long>(static_cast<long long>(b) + o, 0, static_cast<long long>(blocks) - 1))]);
        const double v = std::max(dsp::median(tmp), minimum) / kMadToSigma;
        std::fill(scale.begin() + static_cast<std::ptrdiff_t>(b * block),
                  scale.begin() + static_cast<std::ptrdiff_t>((b + 1) * block), v);
    }
    return scale;
}

// 居中的滑动均方根，窗外按 0 计（与常见的“same”卷积一致）
std::vector<double> movingRms(const std::vector<double>& y, std::size_t n)
{
    std::vector<double> prefix(y.size() + 1, 0.0);
    for (std::size_t i = 0; i < y.size(); ++i)
        prefix[i + 1] = prefix[i] + y[i] * y[i];
    std::vector<double> out(y.size());
    const auto size = static_cast<long long>(y.size());
    for (long long i = 0; i < size; ++i)
    {
        const long long from = std::clamp<long long>(i - static_cast<long long>(n / 2), 0, size);
        const long long to =
            std::clamp<long long>(i - static_cast<long long>(n / 2) + static_cast<long long>(n), 0, size);
        const double sum = prefix[static_cast<std::size_t>(to)] - prefix[static_cast<std::size_t>(from)];
        out[static_cast<std::size_t>(i)] = std::sqrt(std::max(sum, 0.0) / static_cast<double>(n));
    }
    return out;
}

// [centre + fromMs, centre + toMs) 截到信号内
struct Span
{
    std::size_t from = 0, to = 0;
    bool empty() const { return to <= from; }
};
Span span(long long centre, double fromMs, double toMs, double rate, std::size_t size)
{
    const auto clampTo = [size](long long v) {
        return static_cast<std::size_t>(std::clamp<long long>(v, 0, static_cast<long long>(size)));
    };
    return {clampTo(centre + voice::offsetSamples(fromMs, rate)), clampTo(centre + voice::offsetSamples(toMs, rate))};
}

double rmsOf(const std::vector<double>& y, Span s)
{
    if (s.empty())
        return 0.0;
    double sum = 0.0;
    for (std::size_t i = s.from; i < s.to; ++i)
        sum += y[i] * y[i];
    return std::sqrt(sum / static_cast<double>(s.to - s.from));
}

double medianOf(const std::vector<double>& y, Span s)
{
    return dsp::median(std::vector<double>(y.begin() + static_cast<std::ptrdiff_t>(s.from),
                                           y.begin() + static_cast<std::ptrdiff_t>(s.to)));
}

double maxOf(const std::vector<double>& y, Span s, bool absolute)
{
    double m = 0.0;
    for (std::size_t i = s.from; i < s.to; ++i)
        m = std::max(m, absolute ? std::fabs(y[i]) : y[i]);
    return m;
}

// 本人语音：200-4000 Hz 的非重叠帧，取不比第 speech_percentile 百分位帧低 speech_within_db 以上的帧的平均功率。
// 响的帧比安静的帧（第 speech_floor_percentile 百分位）高不出 speech_over_floor_db 时，录到的只是底噪：
// 没有语音可作参照，返回空（数字静音也走这里）。
std::optional<double> speechLevelDb(const std::vector<double>& voiceBand, double rate)
{
    const auto n = static_cast<std::size_t>(std::max<long long>(1, voice::offsetSamples(c::speech_frame_ms, rate)));
    const auto levels = voice::framePowerDb(voiceBand, n, n);
    if (levels.empty())
        return std::nullopt;
    const double top = dsp::percentile(levels, c::speech_percentile);
    if (top - dsp::percentile(levels, c::speech_floor_percentile) < c::speech_over_floor_db)
        return std::nullopt;
    const double gate = top - c::speech_within_db;
    double sum = 0.0;
    int count = 0;
    for (double l : levels)
        if (l >= gate)
        {
            sum += dsp::dbToPower(l);
            ++count;
        }
    if (count == 0 || sum <= 0.0)
        return std::nullopt;
    return dsp::powerToDb(sum / count);
}
} // namespace

ClickScan scanClicks(const std::vector<double>& samples, double sampleRate)
{
    ClickScan scan;
    double rate = 0.0;
    const auto x = voice::prepare(samples, sampleRate, c::analysis_rate_max_hz, rate);
    scan.seconds = static_cast<double>(x.size()) / rate;
    const int order = static_cast<int>(c::filter_order);
    const auto voiceBand = voice::filtered(voice::bandpass(order, c::voice_band_hz[0], c::voice_band_hz[1], rate), x);
    scan.speechDb = speechLevelDb(voiceBand, rate);
    if (!scan.speechDb)
        return scan;
    const double speechDb = *scan.speechDb;

    const auto hp = voice::filtered(voice::highpass(order, c::highpass_hz, rate), x);
    const auto env =
        movingRms(hp, static_cast<std::size_t>(std::max<long long>(1, voice::offsetSamples(c::envelope_ms, rate))));
    const auto lowBand =
        voice::filtered(voice::bandpass(order, c::plosive_low_band_hz[0], c::plosive_low_band_hz[1], rate), x);
    const auto e = lpcResidual(x, rate);
    const auto scale = residualScale(e, rate);
    std::vector<double> ratio(scale.size());
    for (std::size_t i = 0; i < scale.size(); ++i)
        ratio[i] = std::fabs(e[i]) / scale[i];

    const auto merge = voice::offsetSamples(c::spike_merge_ms, rate);
    const std::size_t size = x.size();
    for (std::size_t i = 0; i < ratio.size();)
    {
        if (ratio[i] <= c::candidate_multiple)
        {
            ++i;
            continue;
        }
        // 相隔不超过 spike_merge_ms 的超限样本算同一个尖峰，取残差比最大的那一点
        std::size_t k = i, last = i;
        for (std::size_t j = i + 1; j < ratio.size() && static_cast<long long>(j - last) <= merge; ++j)
            if (ratio[j] > c::candidate_multiple)
            {
                last = j;
                if (ratio[j] > ratio[k])
                    k = j;
            }
        i = last + 1;

        const auto centre = static_cast<long long>(k);
        const Span around = span(centre, c::peak_window_ms[0], c::peak_window_ms[1], rate, size);
        const double peak = maxOf(hp, around, true);
        const double peakEnv = maxOf(env, around, false);
        const Span post = span(centre, c::fricative_start_ms, c::fricative_start_ms + c::fricative_min_ms, rate, size);

        ClickCandidate cand;
        cand.timeSec = static_cast<double>(k) / rate;
        cand.residualMultiple = ratio[k];
        cand.peakDbfs = dsp::amplitudeToDb(peak);
        cand.relSpeechDb = cand.peakDbfs - speechDb;
        if (!post.empty())
        {
            const double lowest = *std::min_element(env.begin() + static_cast<std::ptrdiff_t>(post.from),
                                                    env.begin() + static_cast<std::ptrdiff_t>(post.to));
            cand.fricative = medianOf(env, post) > peakEnv * dbToAmplitude(c::fricative_median_db) &&
                             lowest > peakEnv * dbToAmplitude(c::fricative_min_db);
        }
        const Span before = span(centre, c::plosive_before_ms[0], c::plosive_before_ms[1], rate, size);
        const Span after = span(centre, c::plosive_after_ms[0], c::plosive_after_ms[1], rate, size);
        // 尖峰贴着开头或结尾时窗口是空的：没有前后可比，不当作爆破
        cand.plosive = !before.empty() && !after.empty() &&
                       dsp::amplitudeToDb(rmsOf(lowBand, after)) - dsp::amplitudeToDb(rmsOf(lowBand, before)) >=
                           c::plosive_rise_db &&
                       dsp::amplitudeToDb(rmsOf(voiceBand, after)) - dsp::amplitudeToDb(rmsOf(voiceBand, before)) >=
                           c::plosive_rise_db;

        // 高频背景：先在尖峰附近找高通信号的最大点，再取它前后两段包络中位数的几何平均
        const Span search = span(centre, -c::context_search_ms, c::context_search_ms, rate, size);
        std::size_t kp = k;
        for (std::size_t j = search.from; j < search.to; ++j)
            if (std::fabs(hp[j]) > std::fabs(hp[kp]))
                kp = j;
        const Span pre =
            span(static_cast<long long>(kp), -c::context_window_ms[1], -c::context_window_ms[0], rate, size);
        const Span postCtx =
            span(static_cast<long long>(kp), c::context_window_ms[0], c::context_window_ms[1], rate, size);
        const double fallback = peakEnv;
        const double cb = pre.empty() ? fallback : medianOf(env, pre);
        const double ca = postCtx.empty() ? fallback : medianOf(env, postCtx);
        cand.contextRelDb = dsp::amplitudeToDb(std::sqrt(cb * ca)) - speechDb;
        scan.candidates.push_back(cand);
    }
    return scan;
}

ClickResult summariseClicks(const ClickScan& scan, double residualMultiple)
{
    ClickResult r;
    r.measures.analysedSeconds = scan.seconds;
    if (!scan.speechDb || scan.seconds <= 0.0)
    {
        r.confidence = retest(RetestReason::TooShort);
        return r;
    }
    struct Cluster
    {
        double start, end, peakDbfs;
    };
    std::vector<Cluster> clusters;
    const double gap = c::cluster_gap_ms / 1000.0;
    for (const auto& cand : scan.candidates)
    {
        if (cand.fricative || cand.plosive || cand.relSpeechDb < c::min_rel_speech_db ||
            cand.residualMultiple < residualMultiple || cand.contextRelDb > c::context_max_rel_db)
            continue;
        if (!clusters.empty() && cand.timeSec - clusters.back().end <= gap)
        {
            clusters.back().end = cand.timeSec;
            clusters.back().peakDbfs = std::max(clusters.back().peakDbfs, cand.peakDbfs);
        }
        else
            clusters.push_back({cand.timeSec, cand.timeSec, cand.peakDbfs});
    }
    r.measures.count = static_cast<std::int64_t>(clusters.size());
    r.measures.perMinute = static_cast<double>(clusters.size()) / scan.seconds * 60.0;
    if (clusters.empty())
        return r; // 没有口水音：最响一下的两项不是测量值，留默认的 0；上层先看 count
    double loudest = clusters.front().peakDbfs;
    for (const auto& cl : clusters)
    {
        loudest = std::max(loudest, cl.peakDbfs);
        r.evidence.marks.push_back({EvidenceKind::Click, cl.start, cl.end, cl.peakDbfs, -1});
    }
    voice::keepStrongest(r.evidence.marks, static_cast<std::size_t>(c::evidence_max_marks));
    r.measures.loudestDbfs = loudest;
    r.measures.loudestRelDb = loudest - *scan.speechDb;
    // 档界还没有实录依据：只给测量值，“可能有口水音”的提示由上层按 count 决定，severity 保持最低档
    return r;
}

ClickResult analyseClicks(const std::vector<double>& samples, double sampleRate)
{
    return summariseClicks(scanClicks(samples, sampleRate), c::residual_median_multiple);
}
} // namespace miciri::core
