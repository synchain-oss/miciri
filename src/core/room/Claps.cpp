// SPDX-License-Identifier: GPL-3.0-or-later
#include "room/Claps.h"

#include <algorithm>
#include <cmath>
#include <deque>

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

    // 只认有起音的样本：它前面 [onset_lookback_ms, peak_search_ms) 里的最大值要比它低 onset_db。
    // 衰减尾巴里的随机峰前面总是同样响或更响的尾巴，这样很混的房间里尾巴不会被当成新的一下。
    // 窗口最大值用单调队列滑动求，整段只走一遍。
    const std::size_t look = samplesFor(thr::clap::onset_lookback_ms * 1e-3, rate());
    const std::size_t rise = samplesFor(thr::clap::peak_search_ms * 1e-3, rate());
    const double onset = std::pow(10.0, thr::clap::onset_db / 20.0);
    std::deque<std::size_t> window;
    for (std::size_t k = 0; k < analysed_.size(); ++k)
    {
        if (k >= rise)
        {
            const std::size_t j = k - rise;
            const double v = std::fabs(analysed_[j]);
            while (!window.empty() && std::fabs(analysed_[window.back()]) <= v)
                window.pop_back();
            window.push_back(j);
        }
        while (!window.empty() && window.front() + look < k)
            window.pop_front();
        if (!window.empty() && work[k] < onset * std::fabs(analysed_[window.front()]))
            work[k] = 0.0;
    }

    const std::size_t half = samplesFor(0.5 * thr::clap::min_gap_s, rate());
    const double clipWindow = thr::clap::clip_window_ms * 1e-3;
    while (claps.size() < static_cast<std::size_t>(thr::clap::max_count))
    {
        const auto it = std::max_element(work.begin(), work.end());
        if (*it < floor || *it <= 0.0)
            break;
        const auto k = static_cast<std::size_t>(it - work.begin());
        const double t = static_cast<double>(k) / rate();
        claps.push_back({k, dsp::amplitudeToDb(*it), clipWithin(t - clipWindow, t + clipWindow)});
        std::fill(work.begin() + static_cast<std::ptrdiff_t>(k > half ? k - half : 0),
                  work.begin() + static_cast<std::ptrdiff_t>(std::min(work.size(), k + half + 1)), 0.0);
    }
    std::sort(claps.begin(), claps.end(), [](const ClapEvent& a, const ClapEvent& b) { return a.index < b.index; });
    return claps;
}
} // namespace miciri::core
