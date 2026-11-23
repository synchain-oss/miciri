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

// 线性预测残差。每 lpc_block_ms 一块，在块中心用 lpc_window_ms 长的汉宁窗段估自相关，Levinson-Durbin 解出预测系数；
// 每个样本的预测系数在前后两个块中心之间按位置线性插值，再做逆滤波（用真实的前文样本）。
// 块之间不跳变，块又远短于窗，所以残差几乎不随分析起点挪动：用户点“开始”的时刻落在块里的哪个位置都一样。
// 全静音或不稳定的块没有预测系数，它那一侧按残差 0 参与插值。
std::vector<double> lpcResidual(const std::vector<double>& x, double rate)
{
    const auto order = static_cast<std::size_t>(c::lpc_order);
    const auto block = static_cast<std::size_t>(std::max<long long>(1, voice::offsetSamples(c::lpc_block_ms, rate)));
    const auto win = static_cast<std::size_t>(std::max<long long>(2, voice::offsetSamples(c::lpc_window_ms, rate)));
    std::vector<double> window(win), r(order + 1), a(order + 1), prev(order + 1);
    for (std::size_t i = 0; i < win; ++i)
        window[i] = 0.5 - 0.5 * std::cos(2.0 * dsp::kPi * static_cast<double>(i) / static_cast<double>(win - 1));
    // 加窗段倒着存（z[win - 1 - i] 是第 i 个加窗样本），后面垫 order 个 0。自相关 r[lag] = Σ s[i]·s[i - lag]
    // 按“样本在外、延迟在内”累加：内层是连续的 r[0..order] += s[i]·z[win - 1 - i ..]，各延迟互不依赖，
    // 每个延迟的求和顺序仍是 i 从小到大
    std::vector<double> z(win + order, 0.0);
    const std::size_t blocks = (x.size() + block - 1) / block;
    // 第 b 块的预测系数；全静音或不稳定时为空
    const auto predictorOf = [&](std::size_t b) -> std::vector<double> {
        const std::size_t centre = b * block + block / 2;
        const std::size_t from = centre > win / 2 ? centre - win / 2 : 0;
        for (std::size_t i = 0; i < win; ++i)
            z[win - 1 - i] = from + i < x.size() ? x[from + i] * window[i] : 0.0;
        std::fill(r.begin(), r.end(), 0.0);
        for (std::size_t i = 0; i < win; ++i)
        {
            const double* tail = z.data() + (win - 1 - i);
            const double v = tail[0];
            for (std::size_t lag = 0; lag <= order; ++lag)
                r[lag] += v * tail[lag];
        }
        if (r[0] <= dsp::kPowerFloor)
            return {};
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
            return {};
        return a;
    };

    // 样本按顺序走，前后两个块只增不减：只留这两块的系数，用到时才算，内存不随录音长度增长
    constexpr auto kNone = static_cast<std::size_t>(-1);
    std::size_t loBlock = kNone, hiBlock = kNone;
    std::vector<double> pl, ph;
    std::vector<double> e(x.size(), 0.0);
    for (std::size_t n = 0; n < x.size(); ++n)
    {
        // 前一个块中心 lo 与后一个 hi；第一个中心之前、最后一个中心之后只用最近的那个
        const std::size_t b = n / block;
        const std::size_t lo = n >= b * block + block / 2 || b == 0 ? b : b - 1;
        const std::size_t hi = std::min(lo + 1, blocks - 1);
        const double offset = static_cast<double>(n) - static_cast<double>(lo * block + block / 2);
        const double t = hi == lo ? 0.0 : std::clamp(offset / static_cast<double>(block), 0.0, 1.0);
        if (lo != loBlock)
        {
            pl = lo == hiBlock ? std::move(ph) : predictorOf(lo);
            loBlock = lo;
            hiBlock = kNone;
        }
        if (hi != hiBlock)
        {
            ph = hi == lo ? pl : predictorOf(hi);
            hiBlock = hi;
        }
        // 两组系数各算一次预测，再按位置插值（等于先插值系数再预测）；没有系数的一侧残差按 0
        double predictedLo = 0.0, predictedHi = 0.0;
        const std::size_t taps = std::min(order, n);
        if (!pl.empty())
            for (std::size_t i = 1; i <= taps; ++i)
                predictedLo += pl[i] * x[n - i];
        if (!ph.empty())
            for (std::size_t i = 1; i <= taps; ++i)
                predictedHi += ph[i] * x[n - i];
        const double eLo = pl.empty() ? 0.0 : x[n] - predictedLo;
        const double eHi = ph.empty() ? 0.0 : x[n] - predictedHi;
        e[n] = eLo + t * (eHi - eLo);
    }
    return e;
}

// 局部残差尺度：scale_block_ms 长的块每 scale_hop_ms 一个，取块内 |e| 的中位数；再把前后 scale_halfwidth_ms 内
// 各块的中位数（两端按边缘值延拓）排序，两端各去掉 scale_trim_frac 后取平均。不取中位数的中位数：窗口里语音块与
// 停顿块各占一半时，中位数在两簇之间跳，起点挪一点尺度就跳；去掉两端后的平均随窗口内容连续地变。留下的排位
// 也不能太宽：留得越宽，语音刚起、刚收的几十毫秒里混进的停顿块越多，尺度被拉低，基音脉冲就会被当成尖峰。
// 每个样本的尺度在前后两个块中心之间线性插值，两端用最近的块。输入短于一块时返回空。
std::vector<double> residualScale(const std::vector<double>& e, double rate)
{
    const auto block = static_cast<std::size_t>(std::max<long long>(1, voice::offsetSamples(c::scale_block_ms, rate)));
    const auto hop = static_cast<std::size_t>(std::max<long long>(1, voice::offsetSamples(c::scale_hop_ms, rate)));
    if (e.size() < block)
        return {};
    std::vector<double> magnitude(e.size());
    for (std::size_t n = 0; n < e.size(); ++n)
        magnitude[n] = std::fabs(e[n]);
    const auto medians = dsp::slidingCentres(magnitude, block, hop, 0.0);
    const std::size_t blocks = medians.size();
    // 前后各 half 块：两端按边缘值延拓后，在延拓序列上逐块滑动
    const auto half =
        static_cast<std::size_t>(std::max<long long>(0, std::llround(c::scale_halfwidth_ms / c::scale_hop_ms)));
    std::vector<double> padded;
    padded.reserve(blocks + 2 * half);
    padded.insert(padded.end(), half, medians.front());
    padded.insert(padded.end(), medians.begin(), medians.end());
    padded.insert(padded.end(), half, medians.back());
    auto local = dsp::slidingCentres(padded, 2 * half + 1, 1, c::scale_trim_frac);
    const double minimum = std::sqrt(dsp::kPowerFloor);
    for (auto& v : local)
        v = std::max(v, minimum) / kMadToSigma;
    std::vector<double> scale(e.size());
    const auto first = static_cast<double>(block / 2);
    for (std::size_t n = 0; n < e.size(); ++n)
    {
        const double pos = (static_cast<double>(n) - first) / static_cast<double>(hop);
        if (pos <= 0.0)
            scale[n] = local.front();
        else if (pos >= static_cast<double>(blocks - 1))
            scale[n] = local.back();
        else
        {
            const auto b = static_cast<std::size_t>(pos);
            const double t = pos - static_cast<double>(b);
            scale[n] = local[b] + t * (local[b + 1] - local[b]);
        }
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

// 本人语音：200-4000 Hz 的帧（约 speech_frame_ms 长、取整数个 speech_hop_ms，每 speech_hop_ms 一帧，互相重叠），
// 取不比第 speech_percentile 百分位帧低 speech_within_db 以上的帧的平均功率。
// 响的帧比安静的帧（第 speech_floor_percentile 百分位）高不出 speech_over_floor_db 时，录到的只是底噪：
// 没有语音可作参照，返回空（数字静音也走这里）。
std::optional<double> speechLevelDb(const std::vector<double>& voiceBand, double rate)
{
    const auto hop = static_cast<std::size_t>(std::max<long long>(1, voice::offsetSamples(c::speech_hop_ms, rate)));
    const auto parts =
        static_cast<std::size_t>(std::max<long long>(1, std::llround(c::speech_frame_ms / c::speech_hop_ms)));
    auto levels = dsp::overlappedMeanSquares(voiceBand, hop, parts);
    for (auto& v : levels)
        v = dsp::powerToDb(v);
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
        // 相隔不超过 spike_merge_ms 的超限样本算同一个尖峰，残差比取组内最大值。
        // 位置取第一个到最后一个超限样本之间高通信号绝对值最大的样本，中间没过门限的样本也算：
        // 组里常有两个几乎等高的残差峰，按残差比取位置会随起点在两者之间跳，
        // 后面按位置取的窗口（擦音、爆破、高频背景）跟着跳；高通信号随整段平移严格不变，按它取位置就不跳
        const std::size_t first = i;
        std::size_t last = i;
        double multiple = ratio[i];
        for (std::size_t j = i + 1; j < ratio.size() && static_cast<long long>(j - last) <= merge; ++j)
            if (ratio[j] > c::candidate_multiple)
            {
                last = j;
                multiple = std::max(multiple, ratio[j]);
            }
        i = last + 1;
        std::size_t k = first;
        for (std::size_t j = first + 1; j <= last; ++j)
            if (std::fabs(hp[j]) > std::fabs(hp[k]))
                k = j;

        const auto centre = static_cast<long long>(k);
        const Span around = span(centre, c::peak_window_ms[0], c::peak_window_ms[1], rate, size);
        const double peak = maxOf(hp, around, true);
        const double peakEnv = maxOf(env, around, false);
        const Span post = span(centre, c::fricative_start_ms, c::fricative_start_ms + c::fricative_min_ms, rate, size);

        ClickCandidate cand;
        cand.timeSec = static_cast<double>(k) / rate;
        cand.residualMultiple = multiple;
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
    // 参考值（经验值，不是档界）：超过时卡片给“退后一点”的轻提示，不分档
    r.verdict.overReference = r.measures.perMinute > c::reference_max_per_min;
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
