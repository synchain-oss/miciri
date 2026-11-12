// SPDX-License-Identifier: GPL-3.0-or-later
#include "session/Steps.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "channels/Channels.h"
#include "config/Thresholds.h"
#include "direction/Direction.h"
#include "hum/Hum.h"
#include "level/Dsp.h"
#include "level/Level.h"
#include "noise/Noise.h"
#include "room/Room.h"
#include "voice/Voice.h"

namespace miciri::session
{
using core::FlowNotice;
using core::StepPhase;

// ---- 编排 ----

StepScript::StepScript(const StepContext& ctx, AnalysisHook hook) : ctx_(ctx), hook_(hook) {}

void StepScript::addPhase(StepPhase phase, int rounds, double seconds)
{
    for (int r = 1; r <= rounds; ++r)
        plan_.push_back({phase, r, rounds, std::max<std::size_t>(1, framesFor(seconds))});
    updateLive();
}

void StepScript::analysed() const
{
    if (hook_ != nullptr)
        hook_();
}

std::size_t StepScript::framesFor(double seconds) const
{
    return static_cast<std::size_t>(std::llround(std::max(0.0, seconds) * ctx_.sampleRate));
}

const double* StepScript::pick(const double* left, const double* right) const
{
    return ctx_.stereo && right != nullptr && ctx_.channel == core::AnalysedChannel::Right ? right : left;
}

double StepScript::progressSec() const
{
    return static_cast<double>(consumed_) / ctx_.sampleRate;
}

double StepScript::plannedSec() const
{
    std::size_t total = 0;
    for (const auto& s : plan_)
        total += s.frames;
    return static_cast<double>(total) / ctx_.sampleRate;
}

void StepScript::feed(const double* left, const double* right, std::size_t frames)
{
    while (frames > 0 && !done_ && segment_ < plan_.size())
    {
        if (noticeLeft_ > 0)
        {
            const std::size_t k = std::min(frames, noticeLeft_);
            noticeLeft_ -= k;
            left += k;
            right = right != nullptr ? right + k : nullptr;
            frames -= k;
            if (noticeLeft_ == 0)
            {
                resetPhase(plan_[segment_].phase);
                live_.notice = FlowNotice::None;
            }
            updateLive();
            continue;
        }
        const Segment seg = plan_[segment_];
        const std::size_t k = std::min(frames, seg.frames - inSegment_);
        take(seg, left, right, k);
        inSegment_ += k;
        consumed_ += k;
        left += k;
        right = right != nullptr ? right + k : nullptr;
        frames -= k;

        const bool early = finishedEarly();
        if (inSegment_ == seg.frames || early)
        {
            const bool phaseEnds = early || segment_ + 1 == plan_.size() || plan_[segment_ + 1].phase != seg.phase;
            if (phaseEnds)
            {
                const FlowNotice notice = endOfPhase(seg.phase);
                if (notice != FlowNotice::None)
                {
                    restartPhase(notice);
                    continue;
                }
            }
            inSegment_ = 0;
            if (early)
                segment_ = plan_.size();
            else
                ++segment_;
            done_ = segment_ >= plan_.size();
        }
        updateLive();
    }
}

// 回到这一阶段的第一遍，进度也退回去；先倒数 notice_s 秒（这段音频丢掉），倒数完再清这一阶段收到的东西
void StepScript::restartPhase(FlowNotice notice)
{
    const StepPhase phase = plan_[segment_].phase;
    std::size_t first = segment_;
    while (first > 0 && plan_[first - 1].phase == phase)
        --first;
    for (std::size_t i = first; i < segment_; ++i)
        consumed_ -= plan_[i].frames;
    consumed_ -= inSegment_;
    segment_ = first;
    inSegment_ = 0;
    noticeLeft_ = std::max<std::size_t>(1, framesFor(thr::session::notice_s));
    live_.notice = notice;
    updateLive();
}

void StepScript::updateLive()
{
    if (segment_ >= plan_.size())
        return;
    const Segment& seg = plan_[segment_];
    live_.phase = seg.phase;
    live_.round = seg.round;
    live_.rounds = seg.rounds;
    live_.phaseElapsedSec = static_cast<double>(inSegment_) / ctx_.sampleRate;
    live_.phaseDurationSec = static_cast<double>(seg.frames) / ctx_.sampleRate;
    if (seg.phase == StepPhase::Direction)
        live_.directionIndex = seg.round - 1;
    live_.noticeSec = static_cast<double>(noticeLeft_) / ctx_.sampleRate;
}

namespace
{
void append(std::vector<double>& to, const double* from, std::size_t frames)
{
    to.insert(to.end(), from, from + frames);
}

int count(double v)
{
    return static_cast<int>(std::lround(v));
}

// ---- 接通：观察窗内的无信号诊断；立体声轨再做声道检查 ----
class ConnectStep final : public StepScript
{
public:
    ConnectStep(const StepContext& ctx, AnalysisHook hook) : StepScript(ctx, hook)
    {
        addPhase(StepPhase::None, 1, thr::no_signal::low_flat_window_s);
    }

    StepOutput finish() override
    {
        StepOutput out;
        const core::NoSignalContext nctx{ctx_.signalSeenBefore};
        auto best = core::analyseNoSignal(left_, ctx_.sampleRate, nctx);
        analysed();
        if (ctx_.stereo)
        {
            // 无信号看有声音的那一路：取最响一帧更高的那一路
            auto other = core::analyseNoSignal(right_, ctx_.sampleRate, nctx);
            analysed();
            if (other.measures.maxFrameDbfs > best.measures.maxFrameDbfs)
                best = other;
            out.results.channels = core::analyseChannels(left_, right_, ctx_.sampleRate);
            analysed();
        }
        out.signalSeen = best.verdict.state == core::SignalState::Ok;
        out.results.noSignal = best;
        return out;
    }

private:
    void take(const Segment&, const double* left, const double* right, std::size_t frames) override
    {
        append(left_, left, frames);
        if (ctx_.stereo && right != nullptr)
            append(right_, right, frames);
    }

    std::vector<double> left_, right_;
};

// ---- 增益：两路的峰值、削波与响度；顺带记下说话电平给安静步算 SNR ----
class GainStep final : public StepScript
{
public:
    GainStep(const StepContext& ctx, AnalysisHook hook)
        : StepScript(ctx, hook), gain_(ctx.sampleRate, ctx.stereo, core::GainContext{ctx.noiseFloorRms}),
          speech_(ctx.sampleRate)
    {
        addPhase(StepPhase::None, 1, thr::session::gain_s);
    }

    StepOutput finish() override
    {
        StepOutput out;
        out.results.gain = gain_.finish();
        analysed();
        out.speechLevelDbfsA = speech_.finish();
        analysed();
        return out;
    }

private:
    void take(const Segment&, const double* left, const double* right, std::size_t frames) override
    {
        gain_.push(left, ctx_.stereo ? right : nullptr, frames);
        speech_.push(pick(left, right), frames);
    }

    core::GainAnalyser gain_;
    core::SpeechLevelAnalyser speech_;
};

// ---- 安静：底噪与 SNR、电流声、设备处理 ----
class QuietStep final : public StepScript
{
public:
    QuietStep(const StepContext& ctx, AnalysisHook hook)
        : StepScript(ctx, hook), noise_(ctx.sampleRate, core::NoiseContext{ctx.speechLevelDbfsA}), hum_(ctx.sampleRate)
    {
        addPhase(StepPhase::None, 1, thr::session::quiet_s);
    }

    StepOutput finish() override
    {
        StepOutput out;
        out.results.noise = noise_.finish();
        analysed();
        out.results.hum = hum_.finish();
        analysed();
        core::DeviceProcessingContext dctx;
        if (out.results.noise->confidence.level == core::Reliability::Trusted)
            dctx.noiseFloorDbfs = out.results.noise->measures.noiseDbfs;
        out.results.deviceProcessing = core::analyseDeviceProcessing(samples_, ctx_.sampleRate, dctx);
        analysed();
        return out;
    }

private:
    void take(const Segment&, const double* left, const double* right, std::size_t frames) override
    {
        const double* x = pick(left, right);
        noise_.push(x, frames);
        hum_.push(x, frames);
        append(samples_, x, frames);
    }

    core::NoiseAnalyser noise_;
    core::HumAnalyser hum_;
    std::vector<double> samples_;
};

// 一下拍手给用户看的判定：500 Hz 与 1 kHz（主指标 Tmid 的两个频带）都量出来才算有效；
// 有一个频带被门限作废算 truncated，峰噪比不够算 too-soft，其余（拟合差、衰减太短、没有噪声参考）算 noisy
core::ClapState clapState(const core::ReverbClap& clap)
{
    using core::ClapBandStatus;
    const ClapBandStatus a = clap.bands[0].status;
    const ClapBandStatus b = clap.bands[1].status;
    auto measured = [](ClapBandStatus s) { return s == ClapBandStatus::Valid || s == ClapBandStatus::Outlier; };
    if (measured(a) && measured(b))
        return core::ClapState::Valid;
    if (a == ClapBandStatus::Gated || b == ClapBandStatus::Gated)
        return core::ClapState::Truncated;
    if (a == ClapBandStatus::LowPnr || b == ClapBandStatus::LowPnr)
        return core::ClapState::TooSoft;
    return core::ClapState::Noisy;
}

// ---- 房间：边收边看拍手，凑够有效的几下或拍满上限就收尾；门限让整组作废时倒数后整步重来 ----
class RoomStep final : public StepScript
{
public:
    RoomStep(const StepContext& ctx, AnalysisHook hook)
        : StepScript(ctx, hook), capture_(std::make_unique<core::ClapAnalyser>(ctx.sampleRate)),
          every_(framesFor(thr::session::live_clap_every_s))
    {
        addPhase(StepPhase::None, 1, thr::session::room_max_s);
    }

    StepOutput finish() override
    {
        StepOutput out;
        if (!final_)
        {
            final_ = capture_->finishReverb();
            analysed();
        }
        out.results.reverb = final_->result;
        return out;
    }

private:
    void take(const Segment&, const double* left, const double* right, std::size_t frames) override
    {
        capture_->push(pick(left, right), frames);
        received_ += frames;
        sinceLook_ += frames;
        if (sinceLook_ >= every_)
        {
            sinceLook_ = 0;
            look();
        }
    }

    // 只报尾巴已经收完的几下，免得一下拍手还没衰减完就先被判成 noisy
    void look()
    {
        const auto analysis = capture_->finishReverb();
        analysed();
        const double heard = static_cast<double>(received_) / ctx_.sampleRate;
        live_.claps.clear();
        for (const auto& clap : analysis.claps)
            if (clap.timeSec + thr::reverb::segment_after_s <= heard)
                live_.claps.push_back(clapState(clap));
        if (live_.claps.size() > static_cast<std::size_t>(count(thr::session::room_claps_max)))
            live_.claps.resize(static_cast<std::size_t>(count(thr::session::room_claps_max)));
    }

    bool finishedEarly() const override
    {
        const auto valid = std::count(live_.claps.begin(), live_.claps.end(), core::ClapState::Valid);
        return valid >= count(thr::session::room_claps_needed) ||
               live_.claps.size() >= static_cast<std::size_t>(count(thr::session::room_claps_max));
    }

    FlowNotice endOfPhase(StepPhase) override
    {
        final_ = capture_->finishReverb();
        analysed();
        return final_->result.verdict.gated ? FlowNotice::Gated : FlowNotice::None;
    }

    void resetPhase(StepPhase) override
    {
        capture_ = std::make_unique<core::ClapAnalyser>(ctx_.sampleRate);
        live_.claps.clear();
        received_ = 0;
        sinceLook_ = 0;
        final_.reset();
    }

    std::unique_ptr<core::ClapAnalyser> capture_;
    std::size_t every_;
    std::size_t received_ = 0;
    std::size_t sinceLook_ = 0;
    std::optional<core::ReverbAnalysis> final_;
};

// ---- 位置：轻拍（D/R）→ 爆破音字表 → 齿音字表 → 自然念一段；口水音用字表加自然的一段一起分析 ----
class PositionStep final : public StepScript
{
public:
    PositionStep(const StepContext& ctx, AnalysisHook hook)
        : StepScript(ctx, hook), claps_(std::make_unique<core::ClapAnalyser>(ctx.sampleRate))
    {
        addPhase(StepPhase::SoftClap, count(thr::session::soft_claps), thr::session::soft_clap_round_s);
        addPhase(StepPhase::Plosive, count(thr::session::word_list_rounds), thr::session::word_list_round_s);
        addPhase(StepPhase::Sibilant, count(thr::session::word_list_rounds), thr::session::word_list_round_s);
        addPhase(StepPhase::Natural, 1, thr::click::analysis_window_s);
    }

    StepOutput finish() override
    {
        StepOutput out;
        if (directRatio_)
            out.results.directRatio = directRatio_->result;
        out.results.plosive = core::analysePlosive(plosive_, ctx_.sampleRate);
        analysed();
        out.results.sibilance = core::analyseSibilance(sibilant_, ctx_.sampleRate);
        analysed();
        std::vector<double> speech;
        speech.reserve(plosive_.size() + sibilant_.size() + natural_.size());
        speech.insert(speech.end(), plosive_.begin(), plosive_.end());
        speech.insert(speech.end(), sibilant_.begin(), sibilant_.end());
        speech.insert(speech.end(), natural_.begin(), natural_.end());
        out.results.click = core::analyseClicks(speech, ctx_.sampleRate);
        analysed();
        return out;
    }

private:
    void take(const Segment& seg, const double* left, const double* right, std::size_t frames) override
    {
        const double* x = pick(left, right);
        switch (seg.phase)
        {
        case StepPhase::SoftClap:
            claps_->push(x, frames);
            break;
        case StepPhase::Plosive:
            append(plosive_, x, frames);
            break;
        case StepPhase::Sibilant:
            append(sibilant_, x, frames);
            break;
        default:
            append(natural_, x, frames);
            break;
        }
    }

    // 轻拍要不削波：没削波的几下不够时重拍
    FlowNotice endOfPhase(StepPhase phase) override
    {
        if (phase != StepPhase::SoftClap)
            return FlowNotice::None;
        directRatio_ = claps_->finishDirectRatio();
        analysed();
        const auto& c = directRatio_->result.confidence;
        return c.level == core::Reliability::Retest && c.reason == core::RetestReason::Clipped ? FlowNotice::ClapClipped
                                                                                               : FlowNotice::None;
    }

    void resetPhase(StepPhase phase) override
    {
        if (phase == StepPhase::SoftClap)
        {
            claps_ = std::make_unique<core::ClapAnalyser>(ctx_.sampleRate);
            directRatio_.reset();
        }
    }

    std::unique_ptr<core::ClapAnalyser> claps_;
    std::optional<core::DirectRatioAnalysis> directRatio_;
    std::vector<double> plosive_, sibilant_, natural_;
};

// ---- 方向：四个方向各唱一段；别的方向明显更响时追加确认，重唱当前方向与那个方向 ----
class DirectionStep final : public StepScript
{
public:
    static constexpr int kCurrent = 0; // 用户开始时面对的方向就是当前方向
    static constexpr int kConfirmTakes = 2; // 确认重唱的那一对：当前方向、更响的那个方向

    DirectionStep(const StepContext& ctx, AnalysisHook hook) : StepScript(ctx, hook)
    {
        addPhase(StepPhase::Direction, static_cast<int>(takes_.size()), thr::session::direction_take_s);
    }

    StepOutput finish() override
    {
        StepOutput out;
        out.results.direction = analysis_.result;
        return out;
    }

private:
    void take(const Segment& seg, const double* left, const double* right, std::size_t frames) override
    {
        const auto i = static_cast<std::size_t>(seg.round - 1);
        if (seg.phase == StepPhase::Confirm)
        {
            live_.directionIndex = seg.round == 1 ? kCurrent : analysis_.repeatIndex;
            append(confirm_[std::min<std::size_t>(i, 1)], pick(left, right), frames);
        }
        else
            append(audio_[std::min(i, audio_.size() - 1)], pick(left, right), frames);
    }

    FlowNotice endOfPhase(StepPhase phase) override
    {
        if (phase == StepPhase::Direction)
        {
            for (std::size_t i = 0; i < takes_.size(); ++i)
            {
                takes_[i] = core::measureDirectionTake(audio_[i], ctx_.sampleRate);
                analysed();
            }
            analysis_ = core::decideDirection(takes_, kCurrent);
            if (analysis_.needsRepeat)
                addPhase(StepPhase::Confirm, kConfirmTakes, thr::session::direction_take_s);
        }
        else if (phase == StepPhase::Confirm)
        {
            const auto current = core::measureDirectionTake(confirm_[0], ctx_.sampleRate);
            const auto other = core::measureDirectionTake(confirm_[1], ctx_.sampleRate);
            analysed();
            analysis_ = core::decideDirection(takes_, kCurrent, core::DirectionRepeat{current.levelDb, other.levelDb});
        }
        return FlowNotice::None;
    }

    std::array<core::DirectionTake, 4> takes_{};
    std::array<std::vector<double>, 4> audio_;
    std::array<std::vector<double>, kConfirmTakes> confirm_;
    core::DirectionAnalysis analysis_;
};
} // namespace

std::unique_ptr<StepScript> makeScript(core::StepId id, const StepContext& ctx, AnalysisHook hook)
{
    switch (id)
    {
    case core::StepId::Connect:
        return std::make_unique<ConnectStep>(ctx, hook);
    case core::StepId::Gain:
        return std::make_unique<GainStep>(ctx, hook);
    case core::StepId::Quiet:
        return std::make_unique<QuietStep>(ctx, hook);
    case core::StepId::Room:
        return std::make_unique<RoomStep>(ctx, hook);
    case core::StepId::Position:
        return std::make_unique<PositionStep>(ctx, hook);
    case core::StepId::Direction:
        return std::make_unique<DirectionStep>(ctx, hook);
    }
    return nullptr;
}

double plannedSeconds(core::StepId id)
{
    StepContext ctx;
    const auto script = makeScript(id, ctx, nullptr);
    return script != nullptr ? script->plannedSec() : 0.0;
}
} // namespace miciri::session
