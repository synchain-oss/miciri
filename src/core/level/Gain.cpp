// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cmath>

#include "config/Thresholds.h"
#include "level/Dsp.h"
#include "level/KWeighting.h"
#include "level/Level.h"

namespace miciri::core
{
namespace
{
// BS.1770 对响度的定义：LKFS = -0.691 + 10 log10(K 加权均方值)
constexpr double kLoudnessOffsetDb = -0.691;

struct ChannelFindings
{
    double samplePeak = 0.0;
    double truePeak = 0.0;
    std::int64_t clipRuns = 0;
    std::int64_t flatPeaks = 0;
};

std::size_t clipMinRun(double sampleRate)
{
    const auto byTime = static_cast<std::size_t>(std::llround(thr::gain::clip_digital_min_run_us * 1e-6 * sampleRate));
    return std::max(static_cast<std::size_t>(thr::gain::clip_digital_min_run_samples), byTime);
}

// 数字削波：连续超过阈值且不短于最短时长的段数
std::int64_t countClipRuns(const std::vector<double>& x, double sampleRate, int channel,
                           std::vector<EvidenceMark>& marks)
{
    const std::size_t minRun = clipMinRun(sampleRate);
    const auto maxMarks = static_cast<std::size_t>(thr::gain::clip_evidence_max_marks);
    std::int64_t runs = 0;
    for (std::size_t i = 0; i < x.size();)
    {
        if (std::fabs(x[i]) < thr::gain::clip_digital_threshold)
        {
            ++i;
            continue;
        }
        std::size_t j = i;
        double peak = 0.0;
        while (j < x.size() && std::fabs(x[j]) >= thr::gain::clip_digital_threshold)
            peak = std::max(peak, std::fabs(x[j++]));
        if (j - i >= minRun)
        {
            ++runs;
            if (marks.size() < maxMarks)
                marks.push_back({EvidenceKind::DigitalClip, static_cast<double>(i) / sampleRate,
                                 static_cast<double>(j) / sampleRate, dsp::amplitudeToDb(peak), channel});
        }
        i = j;
    }
    return runs;
}

// 一个极性上的平顶：每个高于全局峰值一半的波峰取峰帽中位数作电平，找平台，再验平台边缘确有下落；
// 返回落在同一电平上的平台数（最大的一簇）。不足 flat_min_peaks 个平台时返回平台总数。
std::int64_t countPlateauPeaks(const std::vector<double>& x, double sampleRate, double globalPeak, double sigma,
                               bool negate)
{
    const double sign = negate ? -1.0 : 1.0;
    const std::size_t n = x.size();
    const double high = thr::gain::flat_level_floor_frac * globalPeak;
    const std::size_t minLen =
        std::max(static_cast<std::size_t>(thr::gain::flat_min_samples),
                 static_cast<std::size_t>(std::ceil(thr::gain::flat_min_ms * 1e-3 * sampleRate)));
    const std::size_t kneeWindow = std::max<std::size_t>(
        1, static_cast<std::size_t>(std::ceil(thr::gain::flat_knee_within_ms * 1e-3 * sampleRate)));
    struct Plateau
    {
        double level;
        double width;
    };
    std::vector<Plateau> plateaus;
    for (std::size_t i = 0; i < n;)
    {
        if (sign * x[i] < high)
        {
            ++i;
            continue;
        }
        std::size_t end = i;
        std::size_t top = i;
        while (end < n && sign * x[end] >= high)
        {
            if (sign * x[end] > sign * x[top])
                top = end;
            ++end;
        }
        const std::size_t runStart = i;
        i = end;

        const double peak = sign * x[top];
        const double capFloor =
            peak - std::max(thr::gain::flat_top_cap_frac * peak, thr::gain::flat_cap_sigma_k * sigma);
        std::size_t left = top;
        while (left > runStart && sign * x[left - 1] >= capFloor)
            --left;
        std::size_t right = top;
        while (right + 1 < end && sign * x[right + 1] >= capFloor)
            ++right;
        if (right - left + 1 < minLen)
            continue;

        std::vector<double> cap;
        for (std::size_t k = left; k <= right; ++k)
            cap.push_back(sign * x[k]);
        const double level = dsp::median(cap);
        const double tolerance = std::max(thr::gain::flat_tol_rel * level, thr::gain::flat_sigma_k * sigma);

        // 峰帽里最长的、落在 level ± tolerance 内的连续一段
        std::size_t bestStart = 0;
        std::size_t bestLen = 0;
        for (std::size_t k = left; k <= right;)
        {
            if (std::fabs(sign * x[k] - level) > tolerance)
            {
                ++k;
                continue;
            }
            std::size_t m = k;
            while (m <= right && std::fabs(sign * x[m] - level) <= tolerance)
                ++m;
            if (m - k > bestLen)
            {
                bestStart = k;
                bestLen = m - k;
            }
            k = m;
        }
        if (bestLen < minLen)
            continue;

        // 平台至少一侧要在 kneeWindow 内下落 flat_knee_drop_frac：圆滑的低频正弦峰顶过不了这一关
        const double dropTo = level * (1.0 - thr::gain::flat_knee_drop_frac);
        const std::size_t plateauEnd = bestStart + bestLen; // 开区间
        bool fallsLeft = false;
        for (std::size_t k = bestStart > kneeWindow ? bestStart - kneeWindow : 0; k < bestStart; ++k)
            fallsLeft = fallsLeft || sign * x[k] <= dropTo;
        bool fallsRight = false;
        for (std::size_t k = plateauEnd; k < std::min(n, plateauEnd + kneeWindow); ++k)
            fallsRight = fallsRight || sign * x[k] <= dropTo;
        if (!fallsLeft && !fallsRight)
            continue;
        plateaus.push_back({level, std::max(2.0 * thr::gain::flat_tol_rel * level, thr::gain::flat_sigma_k * sigma)});
    }
    if (plateaus.size() < static_cast<std::size_t>(thr::gain::flat_min_peaks))
        return static_cast<std::int64_t>(plateaus.size());
    std::sort(plateaus.begin(), plateaus.end(), [](const Plateau& a, const Plateau& b) { return a.level < b.level; });
    double width = 0.0;
    for (const Plateau& p : plateaus)
        width = std::max(width, p.width);
    std::size_t best = 0;
    std::size_t low = 0;
    for (std::size_t hi = 0; hi < plateaus.size(); ++hi)
    {
        while (plateaus[hi].level - plateaus[low].level > width)
            ++low;
        best = std::max(best, hi - low + 1);
    }
    return static_cast<std::int64_t>(best);
}

// BS.1770 的门限积分响度：400 ms 块、100 ms 步进，先去掉绝对门限以下的块，再去掉比剩余均值低一个相对门限的块。
// 声道功率相加。不足一个块时对整段不设门限。
double gatedLoudness(const std::vector<const std::vector<double>*>& channels, double sampleRate)
{
    const std::size_t n = channels.front()->size();
    const auto hopLength = static_cast<std::size_t>(std::llround(thr::gain::loudness_hop_ms * 1e-3 * sampleRate));
    const auto blockHops =
        static_cast<std::size_t>(std::llround(thr::gain::loudness_block_ms / thr::gain::loudness_hop_ms));
    const std::size_t hops = hopLength == 0 ? 0 : n / hopLength;
    std::vector<double> hopEnergy(hops, 0.0);
    double total = 0.0;
    for (const auto* channel : channels)
    {
        dsp::BiquadCascade weighting = dsp::makeKWeighting(sampleRate);
        for (std::size_t i = 0; i < n; ++i)
        {
            const double y = weighting.process((*channel)[i]);
            total += y * y;
            if (hopLength != 0 && i / hopLength < hops)
                hopEnergy[i / hopLength] += y * y;
        }
    }
    if (hops < blockHops || blockHops == 0)
        return kLoudnessOffsetDb + dsp::powerToDb(total / static_cast<double>(n));

    std::vector<double> blocks;
    for (std::size_t i = 0; i + blockHops <= hops; ++i)
    {
        double sum = 0.0;
        for (std::size_t h = i; h < i + blockHops; ++h)
            sum += hopEnergy[h];
        blocks.push_back(sum / static_cast<double>(blockHops * hopLength));
    }
    auto meanAbove = [&](double gateLufs, double& mean) {
        double sum = 0.0;
        std::size_t kept = 0;
        for (double z : blocks)
            if (kLoudnessOffsetDb + dsp::powerToDb(z) > gateLufs)
            {
                sum += z;
                ++kept;
            }
        mean = kept ? sum / static_cast<double>(kept) : 0.0;
        return kept;
    };
    double absoluteMean = 0.0;
    if (meanAbove(thr::gain::loudness_abs_gate_lufs, absoluteMean) == 0)
        return kLoudnessOffsetDb + dsp::powerToDb(0.0);
    double gatedMean = 0.0;
    meanAbove(kLoudnessOffsetDb + dsp::powerToDb(absoluteMean) + thr::gain::loudness_rel_gate_db, gatedMean);
    return kLoudnessOffsetDb + dsp::powerToDb(gatedMean);
}

GainBand bandOf(double truePeakDbfs, bool clipped)
{
    if (clipped || truePeakDbfs > thr::gain::tp_too_loud_above_dbfs)
        return GainBand::TooLoud;
    if (truePeakDbfs > thr::gain::tp_loud_above_dbfs)
        return GainBand::Loud;
    if (truePeakDbfs >= thr::gain::tp_quiet_below_dbfs)
        return GainBand::Good;
    if (truePeakDbfs >= thr::gain::tp_too_quiet_below_dbfs)
        return GainBand::Quiet;
    return GainBand::TooQuiet;
}
} // namespace

GainAnalyser::GainAnalyser(double sampleRate, bool stereo, GainContext ctx)
    : sampleRate_(sampleRate), stereo_(stereo), ctx_(ctx)
{
}

void GainAnalyser::push(const double* left, const double* right, std::size_t frames)
{
    for (std::size_t i = 0; i < frames; ++i)
    {
        channel_[0].push_back(dsp::finiteOrZero(left[i]));
        if (stereo_)
            channel_[1].push_back(dsp::finiteOrZero(right ? right[i] : left[i]));
    }
}

GainResult GainAnalyser::finish() const
{
    GainResult result;
    if (channel_[0].empty())
    {
        result.confidence = retest(RetestReason::TooShort);
        return result;
    }
    const int oversample =
        static_cast<int>(sampleRate_ <= thr::gain::true_peak_rate_split_hz ? thr::gain::true_peak_oversample_le_96k
                                                                           : thr::gain::true_peak_oversample_gt_96k);
    const int channels = stereo_ ? 2 : 1;
    ChannelFindings findings[2];
    for (int c = 0; c < channels; ++c)
    {
        const std::vector<double>& x = channel_[c];
        ChannelFindings& f = findings[c];
        for (double v : x)
            f.samplePeak = std::max(f.samplePeak, std::fabs(v));
        f.truePeak = dsp::truePeak(x.data(), x.size(), oversample);
        f.clipRuns = countClipRuns(x, sampleRate_, stereo_ ? c : -1, result.evidence.marks);
        f.flatPeaks = std::max(countPlateauPeaks(x, sampleRate_, f.samplePeak, ctx_.noiseFloorRms, false),
                               countPlateauPeaks(x, sampleRate_, f.samplePeak, ctx_.noiseFloorRms, true));
    }
    if (!stereo_)
        findings[1] = findings[0];

    GainMeasures& m = result.measures;
    m.samplePeakDbfsL = dsp::amplitudeToDb(findings[0].samplePeak);
    m.samplePeakDbfsR = dsp::amplitudeToDb(findings[1].samplePeak);
    m.truePeakDbfsL = dsp::amplitudeToDb(findings[0].truePeak);
    m.truePeakDbfsR = dsp::amplitudeToDb(findings[1].truePeak);
    m.digitalClipRunsL = findings[0].clipRuns;
    m.digitalClipRunsR = findings[1].clipRuns;
    m.flatPlateauPeaksL = findings[0].flatPeaks;
    m.flatPlateauPeaksR = findings[1].flatPeaks;
    std::vector<const std::vector<double>*> loudnessInput{&channel_[0]};
    if (stereo_)
        loudnessInput.push_back(&channel_[1]);
    m.loudnessLkfs = gatedLoudness(loudnessInput, sampleRate_);

    GainVerdict& v = result.verdict;
    v.digitalClip = findings[0].clipRuns > 0 || findings[1].clipRuns > 0;
    v.maybeFlatTop =
        std::max(findings[0].flatPeaks, findings[1].flatPeaks) >= static_cast<std::int64_t>(thr::gain::flat_min_peaks);
    v.band = bandOf(std::max(m.truePeakDbfsL, m.truePeakDbfsR), v.digitalClip);
    return result;
}

GainResult analyseGain(const std::vector<double>& left, const std::vector<double>* right, double sampleRate,
                       const GainContext& ctx)
{
    GainAnalyser analyser(sampleRate, right != nullptr, ctx);
    analyser.push(left.data(), right ? right->data() : nullptr, left.size());
    return analyser.finish();
}
} // namespace miciri::core
