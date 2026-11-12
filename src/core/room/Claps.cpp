// SPDX-License-Identifier: GPL-3.0-or-later
#include "room/Claps.h"

#include <algorithm>
#include <cmath>

#include "config/Thresholds.h"

namespace miciri::core
{
namespace
{
std::size_t samplesFor(double seconds, double rate)
{
    return static_cast<std::size_t>(std::llround(std::max(0.0, seconds) * rate));
}
} // namespace

ClapCapture::ClapCapture(double sampleRate)
    : sampleRate_(sampleRate), input_(sampleRate, thr::clap::analysis_rate_max_hz),
      minClipRun_(std::max<std::size_t>(1, static_cast<std::size_t>(thr::clap::clip_min_run_samples))),
      minZeroRun_(std::max<std::size_t>(1, samplesFor(thr::reverb::gate_zero_run_ms * 1e-3, sampleRate)))
{
}

void ClapCapture::track(bool inRun, std::size_t index, std::size_t minLength, Span& open, std::vector<Span>& closed)
{
    if (inRun)
    {
        if (open.length++ == 0)
            open.start = index;
        return;
    }
    if (open.length >= minLength)
        closed.push_back(open);
    open.length = 0;
}

void ClapCapture::push(const double* samples, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i, ++received_)
    {
        const double x = dsp::finiteOrZero(samples[i]);
        track(std::fabs(x) >= thr::gain::clip_digital_threshold, received_, minClipRun_, clipOpen_, clips_);
        track(x == 0.0, received_, minZeroRun_, zeroOpen_, zeros_);
    }
    input_.push(samples, count, analysed_);
}

bool ClapCapture::overlaps(const std::vector<Span>& closed, const Span& open, std::size_t minLength, double startSec,
                           double endSec) const
{
    const auto hits = [&](const Span& s) {
        const double a = static_cast<double>(s.start) / sampleRate_;
        const double b = static_cast<double>(s.start + s.length) / sampleRate_;
        return a < endSec && b > startSec;
    };
    if (open.length >= minLength && hits(open))
        return true;
    return std::any_of(closed.begin(), closed.end(), hits);
}

std::vector<ClapEvent> ClapCapture::findClaps() const
{
    std::vector<ClapEvent> claps;
    std::vector<double> work(analysed_.size());
    std::transform(analysed_.begin(), analysed_.end(), work.begin(), [](double v) { return std::fabs(v); });
    if (work.empty())
        return claps;
    const double loudest = *std::max_element(work.begin(), work.end());
    const double floor =
        std::max(loudest * std::pow(10.0, -thr::clap::rel_db / 20.0), std::pow(10.0, thr::clap::floor_dbfs / 20.0));
    const std::size_t half = samplesFor(0.5 * thr::clap::min_gap_s, rate());
    const double window = thr::clap::clip_window_ms * 1e-3;
    while (claps.size() < static_cast<std::size_t>(thr::clap::max_count))
    {
        const auto it = std::max_element(work.begin(), work.end());
        if (*it < floor || *it <= 0.0)
            break;
        const auto k = static_cast<std::size_t>(it - work.begin());
        const double t = static_cast<double>(k) / rate();
        claps.push_back({k, dsp::amplitudeToDb(*it), clipWithin(t - window, t + window)});
        std::fill(work.begin() + static_cast<std::ptrdiff_t>(k > half ? k - half : 0),
                  work.begin() + static_cast<std::ptrdiff_t>(std::min(work.size(), k + half + 1)), 0.0);
    }
    std::sort(claps.begin(), claps.end(), [](const ClapEvent& a, const ClapEvent& b) { return a.index < b.index; });
    return claps;
}
} // namespace miciri::core
