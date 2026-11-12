// SPDX-License-Identifier: GPL-3.0-or-later
#include "room/Room.h"

#include <algorithm>
#include <cmath>

#include "config/Thresholds.h"
#include "room/Butterworth.h"

namespace miciri::core
{
namespace
{
// 混响时间的定义：拟合斜率外推到衰减 60 dB 所需的时间
constexpr double kDecayRangeDb = 60.0;
// 拟合段至少要有的点数：只防两三个点的退化拟合，不是可调的分析参数
constexpr std::size_t kMinFitPoints = 5;

std::size_t samplesFor(double seconds, double rate)
{
    return static_cast<std::size_t>(std::llround(std::max(0.0, seconds) * rate));
}
// 相对 base 偏移 ms 毫秒后的位置，夹在 [0, size] 里
std::size_t offsetBy(std::size_t base, double ms, double rate, std::size_t size)
{
    const long long at = static_cast<long long>(base) + std::llround(ms * 1e-3 * rate);
    return static_cast<std::size_t>(std::clamp<long long>(at, 0, static_cast<long long>(size)));
}
double powerOfDb(double db)
{
    return std::pow(10.0, db / 10.0);
}

// 前缀和：任意区间的能量和与均值都是 O(1)
struct Energy
{
    std::vector<double> e;
    std::vector<double> prefix;
    double sum(std::size_t a, std::size_t b) const { return b > a ? prefix[b] - prefix[a] : 0.0; }
    double mean(std::size_t a, std::size_t b) const { return b > a ? sum(a, b) / static_cast<double>(b - a) : 0.0; }
    std::size_t size() const { return e.size(); }
};

// 一下拍手在分析采样率下的样本段。峰前留 segment_before_s（噪声窗加滤波器起振），峰后 afterSec，
// 并停在下一下之前 next_guard_ms。离录音开头太近时段从开头算，噪声窗被截短，短于 min_noise_ms 就不用这一下。
struct Segment
{
    std::size_t begin = 0;
    std::size_t end = 0;
    std::size_t peak = 0; // 相对 begin
    bool hasNoise = false;
};
Segment segmentOf(const std::vector<ClapEvent>& claps, std::size_t i, std::size_t total, double rate, double afterSec)
{
    Segment s;
    const std::size_t at = claps[i].index;
    const std::size_t before = samplesFor(thr::clap::segment_before_s, rate);
    const std::size_t noiseFrom = offsetBy(at, thr::clap::noise_window_ms[0], rate, total);
    const std::size_t noiseTo = offsetBy(at, thr::clap::noise_window_ms[1], rate, total);
    s.hasNoise = noiseTo >= noiseFrom + samplesFor(thr::clap::min_noise_ms * 1e-3, rate);
    s.begin = at > before ? at - before : 0;
    s.peak = at - s.begin;
    s.end = std::min(total, at + samplesFor(afterSec, rate));
    if (i + 1 < claps.size())
    {
        const std::size_t guard = samplesFor(thr::clap::next_guard_ms * 1e-3, rate);
        s.end = std::min(s.end, claps[i + 1].index > guard ? claps[i + 1].index - guard : 0);
    }
    s.end = std::max(s.end, at + 1);
    return s;
}

Energy bandEnergy(const std::vector<double>& x, const Segment& s, dsp::BiquadCascade filter)
{
    Energy out;
    out.e.resize(std::min(s.end, x.size()) - s.begin);
    out.prefix.assign(out.e.size() + 1, 0.0);
    for (std::size_t i = 0; i < out.e.size(); ++i)
    {
        const double y = filter.process(x[s.begin + i]);
        out.e[i] = y * y;
        out.prefix[i + 1] = out.prefix[i] + out.e[i];
    }
    return out;
}

double noiseOf(const Energy& en, std::size_t peak, double rate)
{
    const std::size_t a = offsetBy(peak, thr::clap::noise_window_ms[0], rate, en.size());
    const std::size_t b = offsetBy(peak, thr::clap::noise_window_ms[1], rate, en.size());
    return std::max(en.mean(a, b), dsp::kPowerFloor);
}

// 频带内的峰：在宽带峰前后 peak_search_ms 内找
std::size_t bandPeak(const Energy& en, std::size_t peak, double rate)
{
    const std::size_t a = offsetBy(peak, -thr::clap::peak_search_ms, rate, en.size());
    const std::size_t b = std::max(offsetBy(peak, thr::clap::peak_search_ms, rate, en.size()), a + 1);
    return static_cast<std::size_t>(
        std::max_element(en.e.begin() + static_cast<std::ptrdiff_t>(a), en.e.begin() + static_cast<std::ptrdiff_t>(b)) -
        en.e.begin());
}

// 从 from 起，衰减持续到第一个低于“噪声加 truncate_over_noise_db”的包络块；返回样本数（块的整数倍）
std::size_t decayLength(const Energy& en, std::size_t from, double noise, double rate)
{
    const std::size_t block = std::max<std::size_t>(1, samplesFor(thr::clap::envelope_block_ms * 1e-3, rate));
    const double limit = noise * powerOfDb(thr::clap::truncate_over_noise_db);
    const std::size_t blocks = from < en.size() ? (en.size() - from) / block : 0;
    for (std::size_t j = 0; j < blocks; ++j)
        if (en.mean(from + j * block, from + (j + 1) * block) < limit)
            return j * block;
    return blocks * block;
}

// 频带内的门限骤降：频带仍比噪声高 gate_margin_db 时，前窗与（隔 gate_tail_drop_ms 的）后窗之比
// 扣掉拟合斜率预期的自然衰减后仍超过 gate_tail_drop_db。前窗最早从峰后 gate_search_from_ms 开始（避开直达声的
// 结尾），一直查到衰减截断点之后一个包络块。两个窗都取几十毫秒，窄频带包络的随机起伏才不会被当成门限。
bool gateDrop(const Energy& en, std::size_t ip, std::size_t decay, double noise, double slopeDbPerSec, double rate)
{
    const std::size_t block = std::max<std::size_t>(1, samplesFor(thr::reverb::gate_block_ms * 1e-3, rate));
    const auto blocksOf = [](double ms) {
        return std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(ms / thr::reverb::gate_block_ms)));
    };
    const std::size_t before = blocksOf(thr::reverb::gate_before_ms) * block;
    const std::size_t gap = blocksOf(thr::reverb::gate_tail_drop_ms) * block;
    const std::size_t after = blocksOf(thr::reverb::gate_after_ms) * block;
    const double natural =
        -slopeDbPerSec * (0.5 * static_cast<double>(before + after) + static_cast<double>(gap)) / rate;
    const double loudEnough = noise * powerOfDb(thr::reverb::gate_margin_db);
    const std::size_t last = ip + decay + samplesFor(thr::clap::envelope_block_ms * 1e-3, rate);
    for (std::size_t t = ip + samplesFor(thr::reverb::gate_search_from_ms * 1e-3, rate) + before;
         t <= last && t + gap + after <= en.size(); t += block)
    {
        const double pre = en.mean(t - before, t);
        const double post = en.mean(t + gap, t + gap + after);
        if (pre >= loudEnough && dsp::powerToDb(pre) - dsp::powerToDb(post) - natural > thr::reverb::gate_tail_drop_db)
            return true;
    }
    return false;
}

ClapBand measureT20(const Energy& en, const Segment& s, double rate, double segmentStartSec, const ClapCapture& cap)
{
    ClapBand out;
    const double noise = noiseOf(en, s.peak, rate);
    const std::size_t ip = bandPeak(en, s.peak, rate);

    // 峰噪比：峰附近（峰前一个窗到峰后两个窗起）pnr_smooth_ms 滑动平均的最大值对噪声
    const std::size_t w = std::max<std::size_t>(1, samplesFor(thr::reverb::pnr_smooth_ms * 1e-3, rate));
    double loudest = 0.0;
    for (std::size_t a = ip > w ? ip - w : 0; a <= ip + 2 * w && a + w <= en.size(); ++a)
        loudest = std::max(loudest, en.mean(a, a + w));
    out.pnrDb = dsp::powerToDb(loudest) - dsp::powerToDb(noise);

    const std::size_t decay = decayLength(en, ip, noise, rate);
    const double ipSec = segmentStartSec + static_cast<double>(ip) / rate;
    const double tailEndSec = ipSec + static_cast<double>(decay) / rate + thr::clap::envelope_block_ms * 1e-3;
    const bool zeros = cap.zeroRunWithin(ipSec, tailEndSec);
    // 拟合不出来时报根因：尾部有数字零是门限，峰噪比不够是噪声，其余才是衰减本身的问题
    const auto failed = [&](ClapBandStatus otherwise) {
        out.status = zeros ? ClapBandStatus::Gated
                           : (out.pnrDb < thr::reverb::band_pnr_min_db ? ClapBandStatus::LowPnr : otherwise);
        return out;
    };
    if (decay < samplesFor(thr::reverb::min_tail_ms * 1e-3, rate))
        return failed(ClapBandStatus::ShortDecay);

    // Schroeder 反向积分（先减噪声，负值按 0），归一到 0 dB
    std::vector<double> edc(decay);
    double total = 0.0;
    for (std::size_t i = decay; i-- > 0;)
        edc[i] = (total += std::max(en.e[ip + i] - noise, 0.0));
    if (total <= 0.0)
        return failed(ClapBandStatus::ShortDecay);
    for (double& v : edc)
        v = dsp::powerToDb(v / total);
    const auto firstBelow = [&](double db) {
        return static_cast<std::size_t>(std::find_if(edc.begin(), edc.end(), [db](double v) { return v <= db; }) -
                                        edc.begin());
    };
    const std::size_t i1 = firstBelow(thr::reverb::fit_start_db);
    const std::size_t i2 = firstBelow(thr::reverb::fit_end_db);
    if (i2 >= edc.size())
        return failed(ClapBandStatus::ShortDecay);
    if (i2 < i1 + kMinFitPoints)
        return failed(ClapBandStatus::PoorFit);

    // 最小二乘直线：时间（秒）对 EDC（dB）
    const double n = static_cast<double>(i2 - i1);
    double st = 0.0, sy = 0.0;
    for (std::size_t i = i1; i < i2; ++i)
    {
        st += static_cast<double>(i) / rate;
        sy += edc[i];
    }
    const double mt = st / n, my = sy / n;
    double stt = 0.0, sty = 0.0, syy = 0.0;
    for (std::size_t i = i1; i < i2; ++i)
    {
        const double dt = static_cast<double>(i) / rate - mt, dy = edc[i] - my;
        stt += dt * dt;
        sty += dt * dy;
        syy += dy * dy;
    }
    const double slope = sty / stt;
    if (!(slope < 0.0) || syy <= 0.0)
        return failed(ClapBandStatus::PoorFit);
    const double residual = syy - slope * sty;
    out.t20Sec = -kDecayRangeDb / slope;
    out.r2 = 1.0 - residual / syy;

    if (zeros || gateDrop(en, ip, decay, noise, slope, rate))
        out.status = ClapBandStatus::Gated;
    else if (out.pnrDb < thr::reverb::band_pnr_min_db)
        out.status = ClapBandStatus::LowPnr;
    else if (out.r2 < thr::reverb::fit_r2_min)
        out.status = ClapBandStatus::PoorFit;
    else
        out.status = ClapBandStatus::Valid;
    return out;
}

// 一个频带给不出值时，说给用户听的原因：有效次数够但离散太大是不稳定；否则门限优先，其次峰噪比
RetestReason bandFailure(const std::vector<ReverbClap>& claps, std::size_t band, std::size_t valid)
{
    if (valid >= static_cast<std::size_t>(thr::reverb::repeat_min_kept))
        return RetestReason::Unstable;
    const auto count = [&](ClapBandStatus st) {
        return std::count_if(claps.begin(), claps.end(),
                             [&](const ReverbClap& c) { return c.bands[band].status == st; });
    };
    if (count(ClapBandStatus::Gated) > 0)
        return RetestReason::GateDetected;
    if (count(ClapBandStatus::LowPnr) > 0)
        return RetestReason::LowPnr;
    return RetestReason::Unstable;
}

ReverbBand reverbBandOf(double tMid)
{
    if (tMid < thr::reverb::dry_below_s)
        return ReverbBand::Dry;
    if (tMid < thr::reverb::normal_below_s)
        return ReverbBand::Normal;
    if (tMid < thr::reverb::boomy_below_s)
        return ReverbBand::Boomy;
    return ReverbBand::VeryBoomy;
}

DrBand drBandOf(double dr)
{
    if (dr < thr::direct_ratio::low_below_db)
        return DrBand::Low;
    if (dr > thr::direct_ratio::high_above_db)
        return DrBand::High;
    return DrBand::Mid;
}

void summariseReverb(ReverbAnalysis& out)
{
    ReverbMeasures& m = out.result.measures;
    std::array<RetestReason, 4> why{};
    for (std::size_t b = 0; b < 4; ++b)
    {
        std::vector<double> pnrs, values;
        for (const ReverbClap& c : out.claps)
        {
            if (c.bands[b].status == ClapBandStatus::NoNoiseReference)
                continue;
            pnrs.push_back(c.bands[b].pnrDb);
            if (c.bands[b].status == ClapBandStatus::Valid)
                values.push_back(c.bands[b].t20Sec);
        }
        m.bandPnrDb[b] = dsp::median(pnrs);
        const double centre = dsp::median(values);
        std::vector<double> kept, r2s;
        for (ReverbClap& c : out.claps)
        {
            ClapBand& cb = c.bands[b];
            if (cb.status != ClapBandStatus::Valid)
                continue;
            if (std::fabs(cb.t20Sec - centre) > thr::reverb::repeat_outlier_frac * centre)
                cb.status = ClapBandStatus::Outlier;
            else
            {
                kept.push_back(cb.t20Sec);
                r2s.push_back(cb.r2);
            }
        }
        if (kept.size() < static_cast<std::size_t>(thr::reverb::repeat_min_kept))
        {
            why[b] = bandFailure(out.claps, b, values.size());
            continue;
        }
        m.t20Sec[b] = dsp::median(kept);
        m.bandR2[b] = dsp::median(r2s);
        const auto [lo, hi] = std::minmax_element(kept.begin(), kept.end());
        m.repeatMaxOverMin = std::max(m.repeatMaxOverMin, *hi / *lo);
    }

    const auto withNoise = std::count_if(out.claps.begin(), out.claps.end(), [](const ReverbClap& c) {
        return c.bands[0].status != ClapBandStatus::NoNoiseReference;
    });
    if (m.t20Sec[0] > 0.0 && m.t20Sec[1] > 0.0)
    {
        m.tMidSec = 0.5 * (m.t20Sec[0] + m.t20Sec[1]);
        out.result.verdict.band = reverbBandOf(m.tMidSec);
        return;
    }
    RetestReason reason = why[0] != RetestReason::None ? why[0] : why[1];
    if (withNoise < static_cast<long long>(thr::reverb::repeat_min_kept))
        reason = RetestReason::FewClaps;
    out.result.confidence = retest(reason);
    out.result.verdict.gated = reason == RetestReason::GateDetected;
}

DirectRatioClap measureDirectRatio(const Energy& en, const Segment& s, double rate)
{
    DirectRatioClap out;
    const double noise = noiseOf(en, s.peak, rate);
    const std::size_t ip = bandPeak(en, s.peak, rate);
    const std::size_t a = offsetBy(ip, thr::direct_ratio::direct_window_start_ms, rate, en.size());
    const std::size_t b = offsetBy(ip, thr::direct_ratio::direct_window_end_ms, rate, en.size());
    const std::size_t end = b + decayLength(en, b, noise, rate);
    const double direct = en.sum(a, b) - noise * static_cast<double>(b - a);
    double tail = en.sum(b, end) - noise * static_cast<double>(end - b);
    const std::size_t window = std::max<std::size_t>(1, samplesFor(thr::direct_ratio::tail_snr_window_ms * 1e-3, rate));
    out.tailSnrDb = dsp::powerToDb(en.mean(b, std::min(en.size(), b + window))) - dsp::powerToDb(noise);
    out.lowerBound = out.tailSnrDb < thr::direct_ratio::tail_snr_min_db || tail <= 0.0;
    // 尾部测不到：按尾窗内最多高出噪声“截断余量”估尾部能量的上限，D/R 因此只是下限
    if (out.lowerBound)
        tail = std::max(tail, noise * powerOfDb(thr::clap::truncate_over_noise_db) * static_cast<double>(window));
    out.usable = s.hasNoise && direct > 0.0;
    out.drDb = out.usable ? dsp::powerToDb(direct / tail) : 0.0;
    return out;
}

std::array<dsp::BiquadCascade, 4> reverbFilters(double rate)
{
    std::array<dsp::BiquadCascade, 4> f;
    for (std::size_t b = 0; b < 4; ++b)
    {
        const double fc = thr::reverb::bands_hz[b];
        f[b] = dsp::makeButterworthBandpass(static_cast<int>(thr::clap::filter_order), fc / std::sqrt(2.0),
                                            fc * std::sqrt(2.0), rate);
    }
    return f;
}
} // namespace

ReverbAnalysis ClapAnalyser::finishReverb() const
{
    ReverbAnalysis out;
    const std::vector<double>& x = capture_.samples();
    const double rate = capture_.rate();
    const std::vector<ClapEvent> events = capture_.findClaps();
    const auto filters = reverbFilters(rate);
    for (std::size_t i = 0; i < events.size(); ++i)
    {
        const Segment s = segmentOf(events, i, x.size(), rate, thr::reverb::segment_after_s);
        ReverbClap clap;
        clap.timeSec = static_cast<double>(events[i].index) / rate;
        clap.peakDbfs = events[i].peakDbfs;
        clap.clipped = events[i].clipped;
        for (std::size_t b = 0; b < 4; ++b)
        {
            if (!s.hasNoise)
                clap.bands[b].status = ClapBandStatus::NoNoiseReference;
            else
                clap.bands[b] =
                    measureT20(bandEnergy(x, s, filters[b]), s, rate, static_cast<double>(s.begin) / rate, capture_);
        }
        out.claps.push_back(clap);
    }
    summariseReverb(out);
    return out;
}

DirectRatioAnalysis ClapAnalyser::finishDirectRatio() const
{
    DirectRatioAnalysis out;
    const std::vector<double>& x = capture_.samples();
    const double rate = capture_.rate();
    const std::vector<ClapEvent> events = capture_.findClaps();
    const auto filter = dsp::makeButterworthBandpass(
        static_cast<int>(thr::clap::filter_order), thr::direct_ratio::band_hz[0], thr::direct_ratio::band_hz[1], rate);
    std::vector<double> clean;
    bool lowerBound = false;
    bool anyClipped = false;
    const double window = thr::clap::clip_window_ms * 1e-3;
    for (std::size_t i = 0; i < events.size(); ++i)
    {
        const Segment s = segmentOf(events, i, x.size(), rate, thr::direct_ratio::segment_after_s);
        DirectRatioClap clap = measureDirectRatio(bandEnergy(x, s, filter), s, rate);
        clap.timeSec = static_cast<double>(events[i].index) / rate;
        clap.peakDbfs = events[i].peakDbfs;
        clap.clipped = events[i].clipped;
        if (clap.clipped)
        {
            anyClipped = true;
            out.result.evidence.marks.push_back(
                {EvidenceKind::DigitalClip, clap.timeSec - window, clap.timeSec + window, clap.peakDbfs, -1});
        }
        else if (clap.usable)
        {
            clean.push_back(clap.drDb);
            lowerBound = lowerBound || clap.lowerBound;
        }
        out.claps.push_back(clap);
    }

    if (clean.size() < static_cast<std::size_t>(thr::direct_ratio::min_clean_claps))
    {
        out.result.confidence = retest(anyClipped ? RetestReason::Clipped : RetestReason::FewClaps);
        return out;
    }
    DirectRatioMeasures& m = out.result.measures;
    m.drDb = dsp::median(clean);
    m.lowerBoundOnly = lowerBound;
    out.result.verdict.band = drBandOf(m.drDb);
    // 只知道下限时，下限落在“高”以下说明真实档位可能更高，不当结论用
    if (lowerBound && out.result.verdict.band != DrBand::High)
        out.result.confidence = retest(RetestReason::LowPnr);
    return out;
}

ReverbAnalysis analyseReverb(const std::vector<double>& samples, double sampleRate)
{
    ClapAnalyser a(sampleRate);
    a.push(samples.data(), samples.size());
    return a.finishReverb();
}

DirectRatioAnalysis analyseDirectRatio(const std::vector<double>& samples, double sampleRate)
{
    ClapAnalyser a(sampleRate);
    a.push(samples.data(), samples.size());
    return a.finishDirectRatio();
}
} // namespace miciri::core
