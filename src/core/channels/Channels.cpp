// SPDX-License-Identifier: GPL-3.0-or-later
#include "channels/Channels.h"

#include <algorithm>
#include <cmath>

#include "config/Thresholds.h"
#include "level/Dsp.h"

namespace miciri::core
{
namespace
{
std::vector<double> frameEnergies(const std::vector<double>& x, std::size_t length, std::size_t frames)
{
    dsp::FrameMeanSquare meter(length);
    for (std::size_t i = 0; i < frames * length; ++i)
        meter.add(x[i]);
    return meter.frames();
}

double mean(const std::vector<double>& v, const std::vector<std::size_t>& indices)
{
    double sum = 0.0;
    for (std::size_t i : indices)
        sum += v[i];
    return indices.empty() ? 0.0 : sum / static_cast<double>(indices.size());
}

bool allZero(const std::vector<double>& x, std::size_t n)
{
    return std::all_of(x.begin(), x.begin() + static_cast<std::ptrdiff_t>(n), [](double v) { return v == 0.0; });
}

void setStatuses(ChannelsVerdict& v, ChannelCase kind, ChannelStatus left, ChannelStatus right, AnalysedChannel who)
{
    v.kind = kind;
    v.statusL = left;
    v.statusR = right;
    v.analysedChannel = who;
}
} // namespace

ChannelsResult analyseChannels(const std::vector<double>& leftIn, const std::vector<double>& rightIn, double sampleRate)
{
    ChannelsResult result;
    const std::size_t n = std::min(leftIn.size(), rightIn.size());
    std::vector<double> left(n);
    std::vector<double> right(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        left[i] = dsp::finiteOrZero(leftIn[i]);
        right[i] = dsp::finiteOrZero(rightIn[i]);
    }
    ChannelsMeasures& m = result.measures;
    ChannelsVerdict& v = result.verdict;

    const bool zeroLeft = allZero(left, n);
    const bool zeroRight = allZero(right, n);
    const auto length = static_cast<std::size_t>(std::llround(thr::channel_check::frame_ms * 1e-3 * sampleRate));
    const std::size_t frames = length == 0 ? 0 : n / length;
    if (frames == 0 || (zeroLeft && zeroRight))
    {
        setStatuses(v, ChannelCase::Undetermined, zeroLeft ? ChannelStatus::Silent : ChannelStatus::Unknown,
                    zeroRight ? ChannelStatus::Silent : ChannelStatus::Unknown, AnalysedChannel::Left);
        result.confidence = retest(frames == 0 ? RetestReason::TooShort : RetestReason::Ambiguous);
        return result;
    }

    std::vector<double> diff(n);
    std::vector<double> sum(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        diff[i] = left[i] - right[i];
        sum[i] = left[i] + right[i];
    }
    const std::vector<double> eL = frameEnergies(left, length, frames);
    const std::vector<double> eR = frameEnergies(right, length, frames);
    const std::vector<double> eD = frameEnergies(diff, length, frames);
    const std::vector<double> eS = frameEnergies(sum, length, frames);
    const double floorPct = thr::channel_check::floor_percentile;
    const double floorL = dsp::percentile(eL, floorPct);
    const double floorR = dsp::percentile(eR, floorPct);
    m.floorDbfsL = dsp::powerToDb(floorL);
    m.floorDbfsR = dsp::powerToDb(floorR);

    std::vector<std::size_t> all(frames);
    for (std::size_t i = 0; i < frames; ++i)
        all[i] = i;

    if (zeroLeft || zeroRight)
    {
        m.activeDbfsL = dsp::powerToDb(mean(eL, all));
        m.activeDbfsR = dsp::powerToDb(mean(eR, all));
        m.deltaDb = m.activeDbfsL - m.activeDbfsR;
        setStatuses(v, ChannelCase::OneSilent, zeroLeft ? ChannelStatus::Silent : ChannelStatus::Signal,
                    zeroRight ? ChannelStatus::Silent : ChannelStatus::Signal,
                    zeroLeft ? AnalysedChannel::Right : AnalysedChannel::Left);
        return result;
    }

    // 以较响的一路为参照找活跃帧：高于它自己的底噪 active_over_floor_db
    const double hi = thr::channel_check::steady_hi_percentile;
    const double lo = thr::channel_check::steady_lo_percentile;
    const std::vector<double>& ref = dsp::percentile(eL, hi) >= dsp::percentile(eR, hi) ? eL : eR;
    const double refFloor = dsp::percentile(ref, floorPct);
    const double activeAbove = refFloor * dsp::dbToPower(thr::channel_check::active_over_floor_db);
    std::vector<std::size_t> active;
    for (std::size_t i = 0; i < frames; ++i)
        if (ref[i] > activeAbove)
            active.push_back(i);
    const double frameSeconds = static_cast<double>(length) / sampleRate;
    const auto minActive = static_cast<std::size_t>(std::ceil(thr::channel_check::min_active_s / frameSeconds - 1e-9));
    bool floorUsable = true;
    if (active.size() < minActive)
    {
        // 活跃帧太少：持续不断的信号（没有停顿）改走回退阈值，其余的判不了
        const double spread = dsp::powerToDb(dsp::percentile(ref, hi)) - dsp::powerToDb(dsp::percentile(ref, lo));
        if (spread >= thr::channel_check::steady_max_spread_db)
        {
            setStatuses(v, ChannelCase::Undetermined, ChannelStatus::Unknown, ChannelStatus::Unknown,
                        AnalysedChannel::Left);
            result.confidence = retest(RetestReason::TooShort);
            return result;
        }
        active = all;
        floorUsable = false;
    }
    const double activeL = mean(eL, active);
    const double activeR = mean(eR, active);
    const double activeD = mean(eD, active);
    const double activeS = mean(eS, active);
    const double activeRef = mean(ref, active);
    const auto minPauseFrames =
        static_cast<std::size_t>(std::ceil(thr::channel_check::floor_min_pause_s / frameSeconds - 1e-9));
    floorUsable = floorUsable && frames - active.size() >= minPauseFrames &&
                  refFloor * dsp::dbToPower(thr::channel_check::floor_valid_below_active_db) < activeRef;

    const double meanLR = 0.5 * (activeL + activeR);
    const double weakActive = std::min(activeL, activeR);
    const double weakFloor = activeL < activeR ? floorL : floorR;
    m.activeDbfsL = dsp::powerToDb(activeL);
    m.activeDbfsR = dsp::powerToDb(activeR);
    m.deltaDb = m.activeDbfsL - m.activeDbfsR;
    m.weakRiseDb = dsp::powerToDb(weakActive) - dsp::powerToDb(weakFloor);
    m.sumOverFloorDb = dsp::powerToDb(activeS) - dsp::powerToDb(floorL + floorR);
    m.diffOverFloorDb = dsp::powerToDb(activeD) - dsp::powerToDb(floorL + floorR);
    m.usedFallback = !floorUsable;
    const double diffRel = dsp::powerToDb(activeD) - dsp::powerToDb(meanLR);
    const double sumRel = dsp::powerToDb(activeS) - dsp::powerToDb(meanLR);

    const AnalysedChannel louder = activeL >= activeR ? AnalysedChannel::Left : AnalysedChannel::Right;
    const double delta = std::fabs(m.deltaDb);
    if (delta >= thr::channel_check::one_quiet_delta_db &&
        (m.weakRiseDb < thr::channel_check::one_quiet_weak_rise_db || delta >= thr::channel_check::one_sided_delta_db))
    {
        const bool leftWeak = activeL < activeR;
        setStatuses(v, ChannelCase::OneDominant, leftWeak ? ChannelStatus::FloorOnly : ChannelStatus::Signal,
                    leftWeak ? ChannelStatus::Signal : ChannelStatus::FloorOnly, louder);
        return result;
    }
    bool antiPhase;
    bool identical;
    if (floorUsable)
    {
        antiPhase = m.sumOverFloorDb <= thr::channel_check::anti_sum_over_floor_db &&
                    m.diffOverFloorDb > thr::channel_check::anti_diff_over_floor_db;
        identical = m.diffOverFloorDb <= thr::channel_check::ident_diff_over_floor_db &&
                    diffRel <= thr::channel_check::ident_rel_db;
    }
    else
    {
        antiPhase = sumRel <= thr::channel_check::anti_fallback_sum_rel_db &&
                    diffRel > thr::channel_check::anti_fallback_diff_rel_db;
        identical = diffRel <= thr::channel_check::ident_fallback_rel_db;
    }
    if (antiPhase)
        setStatuses(v, ChannelCase::AntiPhase, ChannelStatus::Signal, ChannelStatus::InvertedFromLeft,
                    AnalysedChannel::Left);
    else if (identical)
        setStatuses(v, ChannelCase::Identical, ChannelStatus::Signal, ChannelStatus::SameAsLeft, AnalysedChannel::Left);
    else
        setStatuses(v, ChannelCase::TrueStereo, ChannelStatus::Signal, ChannelStatus::Signal, louder);
    return result;
}
} // namespace miciri::core
