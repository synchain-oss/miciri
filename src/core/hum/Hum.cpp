// SPDX-License-Identifier: GPL-3.0-or-later
#include "hum/Hum.h"

#include <algorithm>
#include <cmath>

#include "config/Thresholds.h"

namespace miciri::core
{
namespace
{
// 以 bin 为中心、宽 width（奇数）的窗口里的中值，两端用边缘值延拓
double windowMedian(const std::vector<double>& v, std::size_t bin, std::size_t width, std::vector<double>& scratch)
{
    const auto half = static_cast<long long>(width / 2);
    scratch.clear();
    for (long long o = -half; o <= half; ++o)
    {
        const long long i =
            std::clamp<long long>(static_cast<long long>(bin) + o, 0, static_cast<long long>(v.size()) - 1);
        scratch.push_back(v[static_cast<std::size_t>(i)]);
    }
    auto mid = scratch.begin() + static_cast<std::ptrdiff_t>(scratch.size() / 2);
    std::nth_element(scratch.begin(), mid, scratch.end());
    return *mid;
}

struct Harmonic
{
    int k;
    std::size_t bin;
    double overMedianDb;
};

struct Candidate
{
    double nominalHz = 0.0;
    double f0 = 0.0;
    std::vector<Harmonic> detected;
    double score = 0.0;
    int counted = 0;
};
} // namespace

HumAnalyser::HumAnalyser(double sampleRate)
    : sampleRate_(sampleRate), input_(sampleRate, thr::hum::analysis_rate_max_hz)
{
    segmentLength_ = static_cast<std::size_t>(std::llround(thr::hum::welch_window_s * input_.rate()));
    const auto overlap = static_cast<std::size_t>(static_cast<double>(segmentLength_) * thr::hum::welch_overlap);
    hop_ = std::max<std::size_t>(1, segmentLength_ - overlap);
    window_.resize(segmentLength_);
    for (std::size_t i = 0; i < segmentLength_; ++i)
    {
        window_[i] =
            0.5 - 0.5 * std::cos(2.0 * dsp::kPi * static_cast<double>(i) / static_cast<double>(segmentLength_));
        windowPower_ += window_[i] * window_[i];
    }
}

void HumAnalyser::push(const double* samples, std::size_t count)
{
    samplesIn_ += count;
    decimated_.clear();
    input_.push(samples, count, decimated_);
    pending_.insert(pending_.end(), decimated_.begin(), decimated_.end());
    std::vector<double> frame;
    std::vector<double> spectrum;
    while (pending_.size() >= segmentLength_)
    {
        frame.resize(segmentLength_);
        double mean = 0.0;
        for (std::size_t i = 0; i < segmentLength_; ++i)
            mean += pending_[i];
        mean /= static_cast<double>(segmentLength_);
        for (std::size_t i = 0; i < segmentLength_; ++i)
            frame[i] = (pending_[i] - mean) * window_[i];
        dsp::powerSpectrum(frame.data(), segmentLength_, spectrum);
        // 单边功率谱密度：除 采样率 × 窗功率，除直流与（偶数点时的）Nyquist 外的各点乘 2
        const double scale = 1.0 / (input_.rate() * windowPower_);
        const bool even = segmentLength_ % 2 == 0;
        for (std::size_t k = 0; k < spectrum.size(); ++k)
            spectrum[k] *= scale * ((k == 0 || (even && k + 1 == spectrum.size())) ? 1.0 : 2.0);
        segments_.push_back(spectrum);
        pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(hop_));
    }
}

HumResult HumAnalyser::finish() const
{
    HumResult result;
    const std::size_t segmentCount = segments_.size();
    if (segmentCount == 0)
    {
        result.confidence = retest(RetestReason::TooShort);
        return result;
    }
    if (segmentCount < static_cast<std::size_t>(thr::hum::min_segments))
        result.confidence = retest(RetestReason::TooShort);

    const std::size_t bins = segments_[0].size();
    const double df = input_.rate() / static_cast<double>(segmentLength_);
    const std::size_t medianWidth = static_cast<std::size_t>(2.0 * thr::hum::local_median_halfwidth_hz / df) | 1U;
    std::vector<double> scratch;

    // 平均谱、局部中值本底、峰比本底高多少 dB
    std::vector<double> mean(bins, 0.0);
    for (const auto& s : segments_)
        for (std::size_t k = 0; k < bins; ++k)
            mean[k] += s[k];
    for (double& v : mean)
        v /= static_cast<double>(segmentCount);
    std::vector<double> localFloor(bins);
    std::vector<double> over(bins);
    for (std::size_t k = 0; k < bins; ++k)
    {
        localFloor[k] = windowMedian(mean, k, medianWidth, scratch);
        over[k] = dsp::powerToDb(mean[k]) - dsp::powerToDb(localFloor[k]);
    }

    // 某个 bin 在多少比例的段里比自己的局部中值高 stable_segment_db
    auto stableFraction = [&](std::size_t bin) {
        std::size_t stable = 0;
        for (const auto& s : segments_)
            if (dsp::powerToDb(s[bin]) - dsp::powerToDb(windowMedian(s, bin, medianWidth, scratch)) >=
                thr::hum::stable_segment_db)
                ++stable;
        return static_cast<double>(stable) / static_cast<double>(segmentCount);
    };
    auto lowerBound = [&](double hz) {
        return static_cast<std::size_t>(std::clamp(std::ceil(hz / df - 1e-9), 0.0, static_cast<double>(bins)));
    };
    auto peakIn = [&](double centreHz, double halfWidthHz) {
        const std::size_t lo = std::min(lowerBound(centreHz - halfWidthHz), bins - 1);
        const std::size_t hi = std::min(lowerBound(centreHz + halfWidthHz) + 1, bins);
        return static_cast<std::size_t>(
            std::max_element(over.begin() + static_cast<std::ptrdiff_t>(lo),
                             over.begin() + static_cast<std::ptrdiff_t>(std::max(hi, lo + 1))) -
            over.begin());
    };

    // 对每个候选基频：由最强谐波估 f0，再数稳定的谐波
    const double fmax = thr::hum::harmonic_max_hz;
    std::vector<Candidate> candidates;
    for (double nominal : thr::hum::f0_candidates_hz)
    {
        const int count = static_cast<int>(std::floor(fmax / nominal));
        std::size_t strongestBin = 0;
        int strongestK = 1;
        double strongest = -1e300;
        for (int k = 1; k <= count; ++k)
        {
            const double window = std::max(thr::hum::harmonic_window_min_hz, thr::hum::f0_search_window_frac * k);
            const std::size_t bin = peakIn(k * nominal, window);
            if (over[bin] > strongest)
            {
                strongest = over[bin];
                strongestBin = bin;
                strongestK = k;
            }
        }
        double fraction = 0.0; // 抛物线插值的亚 bin 偏移
        if (strongestBin >= 1 && strongestBin + 1 < bins)
        {
            const double a = std::log(std::max(mean[strongestBin - 1], dsp::kPowerFloor));
            const double b = std::log(std::max(mean[strongestBin], dsp::kPowerFloor));
            const double c = std::log(std::max(mean[strongestBin + 1], dsp::kPowerFloor));
            const double denominator = a - 2.0 * b + c;
            fraction = denominator != 0.0 ? 0.5 * (a - c) / denominator : 0.0;
        }
        Candidate cand;
        cand.nominalHz = nominal;
        cand.f0 = (static_cast<double>(strongestBin) + fraction) * df / strongestK;
        if (std::fabs(cand.f0 - nominal) > thr::hum::f0_accept_hz)
            continue;
        for (int k = 1; k <= count; ++k)
        {
            const double window = std::max(thr::hum::harmonic_window_min_hz, thr::hum::harmonic_window_frac * k);
            const std::size_t bin = peakIn(k * cand.f0, window);
            if (over[bin] >= thr::hum::peak_over_median_db && stableFraction(bin) >= thr::hum::stable_fraction)
            {
                cand.detected.push_back({k, bin, over[bin]});
                if (k <= static_cast<int>(thr::hum::count_harmonics_up_to))
                {
                    ++cand.counted;
                    cand.score += over[bin];
                }
            }
        }
        candidates.push_back(std::move(cand));
    }
    const Candidate* best = nullptr;
    for (const Candidate& c : candidates)
        if (best == nullptr || c.score > best->score)
            best = &c;
    const bool isHum = best != nullptr && best->counted >= static_cast<int>(thr::hum::min_stable_harmonics);

    // 占噪声能量的比例的分母：20 Hz 以上的总功率
    double totalPower = 0.0;
    for (std::size_t k = lowerBound(thr::hum::level_floor_min_hz); k < bins; ++k)
        totalPower += mean[k] * df;
    const auto energyHalfBins = static_cast<std::size_t>(std::llround(thr::hum::harmonic_energy_halfwidth_hz / df));
    auto excessPower = [&](std::size_t bin) {
        double sum = 0.0;
        for (std::size_t k = bin > energyHalfBins ? bin - energyHalfBins : 0;
             k <= std::min(bins - 1, bin + energyHalfBins); ++k)
            sum += std::max(mean[k] - localFloor[k], 0.0) * df;
        return sum;
    };
    const double durationSec = static_cast<double>(samplesIn_) / sampleRate_;

    // 啸叫：2 kHz 到 whistle_max_hz 之间、不属于已检出谐波的稳定窄峰。更高的频段里常有听不见的接口与时钟杂散，
    // 不查；上限以下的杂散（例如 8 kHz 一类整数倍）由下面的电平下限挡住。
    std::vector<std::size_t> candidatesBins;
    const auto excludeBins = static_cast<std::size_t>(std::llround(thr::hum::whistle_exclude_halfwidth_hz / df));
    for (std::size_t k = lowerBound(thr::hum::whistle_min_hz);
         k < bins && static_cast<double>(k) * df <= thr::hum::whistle_max_hz; ++k)
    {
        if (over[k] < thr::hum::whistle_over_median_db)
            continue;
        bool nearHarmonic = false;
        if (isHum)
            for (const Harmonic& h : best->detected)
                nearHarmonic = nearHarmonic || (k + excludeBins >= h.bin && k <= h.bin + excludeBins);
        if (!nearHarmonic && stableFraction(k) >= thr::hum::stable_fraction)
            candidatesBins.push_back(k);
    }
    std::size_t whistleBin = 0;
    bool whistle = false;
    const auto gapBins = static_cast<std::size_t>(std::llround(thr::hum::whistle_group_gap_hz / df));
    for (std::size_t i = 0; i < candidatesBins.size();)
    {
        std::size_t j = i;
        std::size_t peak = candidatesBins[i];
        while (j + 1 < candidatesBins.size() && candidatesBins[j + 1] - candidatesBins[j] <= gapBins)
        {
            ++j;
            if (over[candidatesBins[j]] > over[peak])
                peak = candidatesBins[j];
        }
        i = j + 1;
        // 窄峰本身的功率低于 whistle_min_dbfs 的不算：峰比本底高，但绝对电平低到听不见
        if (dsp::powerToDb(excessPower(peak)) < thr::hum::whistle_min_dbfs)
            continue;
        if (!whistle || over[peak] > over[whistleBin])
            whistleBin = peak;
        whistle = true;
    }

    HumMeasures& m = result.measures;
    if (whistle)
    {
        // 啸叫比电流声严重（红 对 琥珀），同时出现时报啸叫；电流声的峰仍留在证据里
        const double power = excessPower(whistleBin);
        result.verdict.kind = HumKind::Whistle;
        m.fundamentalHz = static_cast<double>(whistleBin) * df;
        m.overFloorDb = over[whistleBin];
        m.energySharePct = totalPower > 0.0 ? power / totalPower * 100.0 : 0.0;
        m.highestHarmonicHz = m.fundamentalHz;
        m.levelDbfs = dsp::powerToDb(power);
        m.peakDbfs = m.levelDbfs;
        result.evidence.marks.push_back(
            {EvidenceKind::HumPeak, 0.0, durationSec, static_cast<double>(whistleBin) * df, -1});
    }
    if (isHum)
    {
        double total = 0.0;
        double strongestPower = 0.0;
        double strongestOver = 0.0;
        int highestK = 0;
        for (const Harmonic& h : best->detected)
        {
            const double power = excessPower(h.bin);
            total += power;
            strongestPower = std::max(strongestPower, power);
            strongestOver = std::max(strongestOver, h.overMedianDb);
            highestK = std::max(highestK, h.k);
            result.evidence.marks.push_back(
                {EvidenceKind::HumPeak, 0.0, durationSec, static_cast<double>(h.bin) * df, -1});
        }
        if (!whistle)
        {
            const double highestHz = highestK * best->f0;
            result.verdict.kind = highestHz > thr::hum::buzz_above_hz ? HumKind::Buzz : HumKind::Hum;
            m.fundamentalHz = best->f0;
            m.overFloorDb = strongestOver;
            m.energySharePct = totalPower > 0.0 ? total / totalPower * 100.0 : 0.0;
            m.highestHarmonicHz = highestHz;
            m.levelDbfs = dsp::powerToDb(total);
            m.peakDbfs = dsp::powerToDb(strongestPower);
            // 严重度看最强一根谐波从周围底噪里冒出多少：冒得不多只标轻微。占噪声能量的比例只作信息，
            // 它受宽带噪声大小左右（底噪大的录音里真电流声占比反而小），不能当严重度。
            // 另一条是可闻度：电流声成分的总电平低于 slight_below_dbfs 也只标轻微。这是绝对电平，前提是增益步
            // 已把人声带到目标附近；冒出本底很多、但电平低到在正常增益的人声录音里听不见的，不该排到增益与底噪前面。
            result.verdict.slight =
                m.overFloorDb < thr::hum::slight_below_over_floor_db || m.levelDbfs < thr::hum::slight_below_dbfs;
        }
    }
    return result;
}

HumResult analyseHum(const std::vector<double>& samples, double sampleRate)
{
    HumAnalyser analyser(sampleRate);
    analyser.push(samples.data(), samples.size());
    return analyser.finish();
}
} // namespace miciri::core
