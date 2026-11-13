// SPDX-License-Identifier: GPL-3.0-or-later
#include "session/Evidence.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

#include "config/Thresholds.h"

namespace miciri::session
{
namespace
{
// 包络的单位是千分之一满刻度（桥契约规定的取值范围 -1000..1000），这是单位换算，不是可调的阈值
constexpr double kPerMille = 1000.0;
// 整格都是 NaN 的基础格：最小记成最大可能值、最大记成最小可能值，合并时不参与；合并后仍是这样的格输出 0
constexpr std::int16_t kEmptyLo = std::numeric_limits<std::int16_t>::max();
constexpr std::int16_t kEmptyHi = std::numeric_limits<std::int16_t>::min();

std::int16_t perMille(double v)
{
    return static_cast<std::int16_t>(std::lround(std::clamp(v, -1.0, 1.0) * kPerMille));
}

std::size_t whole(double v)
{
    return static_cast<std::size_t>(std::llround(v));
}

// 不短于 hopMin 的最少整数个采样
std::size_t baseFrames(double sampleRate)
{
    const double hopMin = thr::session::evidence_hop_min_s;
    auto n = static_cast<std::size_t>(std::max<long long>(1, std::llround(hopMin * sampleRate)));
    if (static_cast<double>(n) / sampleRate < hopMin)
        ++n;
    return n;
}

std::optional<EvidenceEventKind> eventKindOf(core::EvidenceKind kind)
{
    switch (kind)
    {
    case core::EvidenceKind::Plosive:
        return EvidenceEventKind::Plosive;
    case core::EvidenceKind::Sibilant:
        return EvidenceEventKind::Sibilance;
    case core::EvidenceKind::Click:
        return EvidenceEventKind::Mouth;
    default:
        return std::nullopt; // 削波、平顶、电流声峰、零段等不画在证据波形上
    }
}
} // namespace

EnvelopeRecorder::EnvelopeRecorder(double sampleRate) : sampleRate_(sampleRate), base_(baseFrames(sampleRate)) {}

void EnvelopeRecorder::push(const double* x, std::size_t frames)
{
    for (std::size_t i = 0; i < frames; ++i)
    {
        const double v = x[i];
        if (!std::isnan(v))
        {
            now_.lo = now_.any ? std::min(now_.lo, v) : v;
            now_.hi = now_.any ? std::max(now_.hi, v) : v;
            now_.any = true;
        }
        ++now_.frames;
        if (++now_.inCell == base_)
            flush();
    }
}

void EnvelopeRecorder::flush()
{
    // 收满的格只追加；重来时 rewind() 按 cells 截短
    lo_.push_back(now_.any ? perMille(now_.lo) : kEmptyLo);
    hi_.push_back(now_.any ? perMille(now_.hi) : kEmptyHi);
    ++now_.cells;
    now_.inCell = 0;
    now_.any = false;
}

void EnvelopeRecorder::rewind()
{
    now_ = saved_;
    lo_.resize(now_.cells);
    hi_.resize(now_.cells);
}

void EnvelopeRecorder::finish(EvidencePack& pack) const
{
    pack.durationSec = static_cast<double>(now_.frames) / sampleRate_;
    pack.min.clear();
    pack.max.clear();
    const std::size_t cells = now_.cells + (now_.inCell > 0 ? 1 : 0);
    const std::size_t most = std::max<std::size_t>(1, whole(thr::session::evidence_cells_max));
    const std::size_t k = std::max<std::size_t>(1, (cells + most - 1) / most);
    pack.hopSec = static_cast<double>(k * base_) / sampleRate_;
    // 未收满的最后一格：量化后与收满的格一起合并（取整是单调的，先取整再取最小最大与反过来相同）
    auto lo = [&](std::size_t i) { return i < now_.cells ? lo_[i] : (now_.any ? perMille(now_.lo) : kEmptyLo); };
    auto hi = [&](std::size_t i) { return i < now_.cells ? hi_[i] : (now_.any ? perMille(now_.hi) : kEmptyHi); };
    for (std::size_t first = 0; first < cells; first += k)
    {
        const std::size_t last = std::min(cells, first + k);
        std::int16_t a = lo(first), b = hi(first);
        for (std::size_t i = first + 1; i < last; ++i)
        {
            a = std::min(a, lo(i));
            b = std::max(b, hi(i));
        }
        if (a > b) // 合并进来的样本全是 NaN
            a = b = 0;
        pack.min.push_back(a);
        pack.max.push_back(b);
    }
}

std::vector<EvidenceEvent> evidenceEvents(const core::AllResults& r)
{
    std::vector<EvidenceEvent> events;
    auto add = [&events](const auto& result) {
        if (!result)
            return;
        for (const auto& m : result->evidence.marks)
            if (const auto kind = eventKindOf(m.kind))
                events.push_back({*kind, m.startSec, std::max(0.0, m.endSec - m.startSec), m.value});
    };
    add(r.noSignal);
    add(r.channels);
    add(r.gain);
    add(r.noise);
    add(r.hum);
    add(r.deviceProcessing);
    add(r.reverb);
    add(r.directRatio);
    add(r.plosive);
    add(r.sibilance);
    add(r.click);
    add(r.direction);
    add(r.distance);
    std::stable_sort(events.begin(), events.end(),
                     [](const EvidenceEvent& a, const EvidenceEvent& b) { return a.timeSec < b.timeSec; });
    const std::size_t most = whole(thr::session::evidence_events_max);
    if (events.size() > most)
        events.resize(most);
    return events;
}
} // namespace miciri::session
