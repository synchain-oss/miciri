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

StepScript::StepScript(const StepContext& ctx, AnalysisHook hook) : ctx_(ctx), hook_(hook), envelope_(ctx.sampleRate)
{
    if (manual())
    {
        guardFrames_ = framesFor(thr::session::click_guard_s);
        guardLeft_ = guardFrames_; // 第一段随这一步开始：开始那一下的点击也要丢掉
    }
}

EvidencePack StepScript::evidence(const core::AllResults& results) const
{
    EvidencePack pack;
    envelope_.finish(pack);
    pack.events = evidenceEvents(results);
    return pack;
}

void StepScript::addPhase(StepPhase phase, int rounds, double seconds)
{
    for (int r = 1; r <= rounds; ++r)
        plan_.push_back({phase, r, rounds, std::max<std::size_t>(1, framesFor(seconds))});
    updateLive();
}

void StepScript::addPasses(StepPhase phase, int passes, double secondsEach)
{
    if (manual())
        addPhase(phase, 1, std::max(1, passes) * secondsEach);
    else
        addPhase(phase, passes, secondsEach);
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

double StepScript::segmentSeconds() const
{
    return static_cast<double>(segmentForwarded_) / ctx_.sampleRate;
}

void StepScript::feed(const double* left, const double* right, std::size_t frames)
{
    auto skip = [&](std::size_t k) {
        left += k;
        right = right != nullptr ? right + k : nullptr;
        frames -= k;
    };
    while (frames > 0 && !done_ && segment_ < plan_.size())
    {
        if (noticeLeft_ > 0) // Auto 的倒数：这段音频丢掉，倒数完再清这一阶段收到的东西
        {
            const std::size_t k = std::min(frames, noticeLeft_);
            noticeLeft_ -= k;
            skip(k);
            if (noticeLeft_ == 0)
            {
                resetPhase(plan_[segment_].phase);
                live_.notice = FlowNotice::None;
            }
            updateLive();
            continue;
        }
        if (waiting_) // 待开始：整块丢掉
            return;
        if (guardLeft_ > 0) // 刚开始：丢掉点击声
        {
            const std::size_t k = std::min(frames, guardLeft_);
            guardLeft_ -= k;
            skip(k);
            continue;
        }
        const Segment seg = plan_[segment_];
        const std::size_t k = std::min(frames, seg.frames - inSegment_);
        delay(seg, left, right, k);
        inSegment_ += k;
        skip(k);
        const bool early = finishedEarly();
        if (inSegment_ == seg.frames || early)
            closeSegment(seg, true, early);
        updateLive();
    }
}

bool StepScript::begin()
{
    if (!manual() || done_ || !waiting_ || segment_ >= plan_.size())
        return false;
    waiting_ = false;
    guardLeft_ = guardFrames_;
    live_.notice = FlowNotice::None;
    updateLive();
    return true;
}

bool StepScript::end()
{
    if (!manual() || done_ || waiting_ || segment_ >= plan_.size() || !reached_)
        return false;
    const Segment seg = plan_[segment_];
    closeSegment(seg, false, false);
    updateLive();
    return true;
}

// 点击保护的延迟：样本先在这里放 guardFrames_ 帧，再送去分析。Auto 下没有延迟
void StepScript::delay(const Segment& seg, const double* left, const double* right, std::size_t frames)
{
    if (guardFrames_ == 0)
    {
        forward(seg, left, right, frames);
        return;
    }
    delayL_.insert(delayL_.end(), left, left + frames);
    if (right != nullptr)
        delayR_.insert(delayR_.end(), right, right + frames);
    const std::size_t held = delayL_.size() - delayHead_;
    if (held > guardFrames_)
    {
        const std::size_t out = held - guardFrames_;
        forward(seg, delayL_.data() + delayHead_, delayR_.empty() ? nullptr : delayR_.data() + delayHead_, out);
        delayHead_ += out;
    }
    if (delayHead_ >= guardFrames_) // 送走的攒够了再挪，摊下来每个样本只挪一次
    {
        delayL_.erase(delayL_.begin(), delayL_.begin() + static_cast<std::ptrdiff_t>(delayHead_));
        if (!delayR_.empty())
            delayR_.erase(delayR_.begin(), delayR_.begin() + static_cast<std::ptrdiff_t>(delayHead_));
        delayHead_ = 0;
    }
}

void StepScript::forward(const Segment& seg, const double* left, const double* right, std::size_t frames)
{
    if (frames == 0)
        return;
    envelope_.push(pick(left, right), frames);
    take(seg, left, right, frames);
    consumed_ += frames;
    segmentForwarded_ += frames;
}

void StepScript::closeSegment(const Segment& seg, bool keep, bool early)
{
    if (keep && delayL_.size() > delayHead_)
        forward(seg, delayL_.data() + delayHead_, delayR_.empty() ? nullptr : delayR_.data() + delayHead_,
                delayL_.size() - delayHead_);
    delayL_.clear();
    delayR_.clear();
    delayHead_ = 0;
    const bool phaseEnds = early || segment_ + 1 == plan_.size() || plan_[segment_ + 1].phase != seg.phase;
    if (phaseEnds)
    {
        // 重来有上限：一直重来不成，就按收到的音频往下走，由分析给出再测与原因
        const FlowNotice notice = endOfPhase(seg.phase);
        if (notice != FlowNotice::None && restarts_ < std::lround(thr::session::restarts_max))
        {
            ++restarts_;
            restartPhase(notice);
            return;
        }
    }
    inSegment_ = 0;
    segmentForwarded_ = 0;
    reached_ = false;
    segment_ = early ? plan_.size() : segment_ + 1;
    done_ = segment_ >= plan_.size();
    if (done_)
        return;
    if (plan_[segment_].phase != seg.phase)
    {
        envelope_.mark(); // 新阶段从这里开始：这一阶段重来时证据波形与进度退回这里
        phaseStart_ = consumed_;
    }
    segmentBegins(plan_[segment_]);
    waiting_ = manual();
}

// 回到这一阶段的第一遍，进度与证据波形也退回去。Auto：先倒数 notice_s
// 秒（这段音频丢掉），倒数完再清这一阶段收到的东西； Manual：马上清掉，回到待开始，提示留到用户再开始
void StepScript::restartPhase(FlowNotice notice)
{
    const StepPhase phase = plan_[segment_].phase;
    std::size_t first = segment_;
    while (first > 0 && plan_[first - 1].phase == phase)
        --first;
    segment_ = first;
    inSegment_ = 0;
    segmentForwarded_ = 0;
    reached_ = false;
    consumed_ = phaseStart_;
    envelope_.rewind();
    live_.notice = notice;
    if (manual())
    {
        resetPhase(phase);
        segmentBegins(plan_[segment_]);
        waiting_ = true;
    }
    else
        noticeLeft_ = std::max<std::size_t>(1, framesFor(thr::session::notice_s));
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
    live_.waiting = waiting_;
    const Need n = manual() ? need(seg) : Need{};
    if (manual() && !waiting_ && n.left <= 0.0)
        reached_ = true; // 够了就一直够：后面的音频改变了百分位，也不把已经能按的“结束”收回去
    live_.need = n.kind;
    live_.needLeft = reached_ ? 0.0 : std::max(0.0, n.left);
    live_.heard = n.heard;
    live_.canEnd = manual() && !waiting_ && reached_;
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

// 安静步最少要收多久，由两项分析自己的下限推出：电流声的 Welch 至少要 hum.min_segments 段
// （第一段一个窗长，之后每段多一个跳距），底噪至少要 noise.min_frames 帧。少于它们，结果就是 too-short
double quietMinSeconds()
{
    const double hum =
        thr::hum::welch_window_s * (1.0 + (thr::hum::min_segments - 1.0) * (1.0 - thr::hum::welch_overlap));
    const double noise = thr::noise::min_frames * thr::noise::frame_ms * 1e-3;
    return std::max(hum, noise);
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

    // 有声的秒数用说话电平的同一条活跃帧规则
    Need need(const Segment&) const override
    {
        const double voiced = speech_.activeSeconds();
        return {core::PhaseNeed::VoiceSeconds, thr::session::gain_min_voice_s - voiced, voiced};
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

    Need need(const Segment&) const override
    {
        const double heard = segmentSeconds();
        return {core::PhaseNeed::Seconds, quietMinSeconds() - heard, heard};
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
        live_.clapDetails.clear();
        for (const auto& clap : analysis.claps)
            if (clap.timeSec + thr::reverb::segment_after_s <= heard)
            {
                live_.claps.push_back(clapState(clap));
                live_.clapDetails.push_back(clap);
            }
        const auto most = static_cast<std::size_t>(count(thr::session::room_claps_max));
        if (live_.claps.size() > most)
        {
            live_.claps.resize(most);
            live_.clapDetails.resize(most);
        }
    }

    long valid() const
    {
        return static_cast<long>(std::count(live_.claps.begin(), live_.claps.end(), core::ClapState::Valid));
    }

    bool finishedEarly() const override
    {
        return valid() >= count(thr::session::room_claps_needed) ||
               live_.claps.size() >= static_cast<std::size_t>(count(thr::session::room_claps_max));
    }

    // 手动结束要 500 Hz 与 1 kHz 都还能各留下 reverb.repeat_min_kept 下：少于它，T20 一定给不出
    Need need(const Segment&) const override
    {
        const auto n = static_cast<double>(valid());
        return {core::PhaseNeed::Claps, thr::reverb::repeat_min_kept - n, n};
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
        live_.clapDetails.clear();
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
// Manual 下各阶段的最低量：轻拍要 direct_ratio.min_clean_claps 下没削波、量得出的；两个字表各要 word_list_min_events
// 个音节（数的就是各自的分析找到的事件）；自然念要 natural_min_voice_s 秒有声
class PositionStep final : public StepScript
{
public:
    PositionStep(const StepContext& ctx, AnalysisHook hook)
        : StepScript(ctx, hook), claps_(std::make_unique<core::ClapAnalyser>(ctx.sampleRate)), natural_(ctx.sampleRate),
          clapEvery_(framesFor(thr::session::live_clap_every_s)),
          wordsEvery_(framesFor(thr::session::live_words_every_s))
    {
        addPasses(StepPhase::SoftClap, count(thr::session::soft_claps), thr::session::soft_clap_round_s);
        addPasses(StepPhase::Plosive, count(thr::session::word_list_rounds), thr::session::word_list_round_s);
        addPasses(StepPhase::Sibilant, count(thr::session::word_list_rounds), thr::session::word_list_round_s);
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
        speech.reserve(plosive_.size() + sibilant_.size() + naturalTake_.size());
        speech.insert(speech.end(), plosive_.begin(), plosive_.end());
        speech.insert(speech.end(), sibilant_.begin(), sibilant_.end());
        speech.insert(speech.end(), naturalTake_.begin(), naturalTake_.end());
        out.results.click = core::analyseClicks(speech, ctx_.sampleRate);
        analysed();
        // 三项分析各自从自己那段音频的开头计时，换算到这一步的时间轴（与证据波形同一条轴）。
        // 口水音分析的是三段拼起来的音频，按标记起点落在哪一段分别换算，不假定三段在轴上首尾相接
        moveMarks(out.results.plosive, [this](double) { return seconds(plosiveAt_); });
        moveMarks(out.results.sibilance, [this](double) { return seconds(sibilantAt_); });
        moveMarks(out.results.click, [this](double t) { return clickOffset(t); });
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
            softFrames_ += frames;
            if (manual() && (sinceLook_ += frames) >= clapEvery_)
            {
                sinceLook_ = 0;
                lookSoftClaps();
            }
            break;
        case StepPhase::Plosive:
            if (plosive_.empty())
                plosiveAt_ = heardFrames();
            append(plosive_, x, frames);
            countSyllables(plosive_, plosiveEvents_, frames, [](const std::vector<double>& s, double fs) {
                return core::analysePlosive(s, fs).measures.events;
            });
            break;
        case StepPhase::Sibilant:
            if (sibilant_.empty())
                sibilantAt_ = heardFrames();
            append(sibilant_, x, frames);
            countSyllables(sibilant_, sibilantEvents_, frames, [](const std::vector<double>& s, double fs) {
                return core::analyseSibilance(s, fs).measures.events;
            });
            break;
        default:
            if (naturalTake_.empty())
                naturalAt_ = heardFrames();
            append(naturalTake_, x, frames);
            if (manual())
                natural_.push(x, frames);
            break;
        }
    }

    Need need(const Segment& seg) const override
    {
        switch (seg.phase)
        {
        case StepPhase::SoftClap:
            return {core::PhaseNeed::Claps, thr::direct_ratio::min_clean_claps - cleanClaps_, cleanClaps_};
        case StepPhase::Plosive:
            return syllables(plosiveEvents_);
        case StepPhase::Sibilant:
            return syllables(sibilantEvents_);
        default:
            break;
        }
        const double voiced = natural_.activeSeconds();
        return {core::PhaseNeed::VoiceSeconds, thr::session::natural_min_voice_s - voiced, voiced};
    }

    static Need syllables(std::int64_t events)
    {
        const auto n = static_cast<double>(events);
        return {core::PhaseNeed::Syllables, thr::session::word_list_min_events - n, n};
    }

    void segmentBegins(const Segment&) override { sinceLook_ = 0; }

    // 轻拍：只数尾巴已经收完的几下，削波的、量不出直达声的不算
    void lookSoftClaps()
    {
        const auto a = claps_->finishDirectRatio();
        analysed();
        const double heard = static_cast<double>(softFrames_) / ctx_.sampleRate;
        double clean = 0.0;
        for (const auto& c : a.claps)
            if (!c.clipped && c.usable && c.timeSec + thr::direct_ratio::segment_after_s <= heard)
                clean += 1.0;
        cleanClaps_ = clean;
    }

    // 字表：每隔 live_words_every_s 用这一遍字表自己的分析数一次音节，够了就不再数
    template<class F>
    void countSyllables(const std::vector<double>& take, std::int64_t& events, std::size_t frames, F analyse)
    {
        if (!manual() || events >= count(thr::session::word_list_min_events))
            return;
        sinceLook_ += frames;
        if (sinceLook_ < wordsEvery_)
            return;
        sinceLook_ = 0;
        events = analyse(take, ctx_.sampleRate);
        analysed();
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

    // 重来时丢掉这一阶段收到的音频，与进度、证据波形一起退回；这一段的起点在重新收到样本时再记。
    // 现在只有轻拍阶段会重来（endOfPhase 只对它要求重来），字表与自然朗读的分支目前跑不到，
    // 是给以后这些阶段也允许重来时准备的；到那时要补“重来之后标记与包络仍对得上”的用例
    void resetPhase(StepPhase phase) override
    {
        switch (phase)
        {
        case StepPhase::SoftClap:
            claps_ = std::make_unique<core::ClapAnalyser>(ctx_.sampleRate);
            directRatio_.reset();
            softFrames_ = 0;
            cleanClaps_ = 0.0;
            break;
        case StepPhase::Plosive:
            plosive_.clear();
            plosiveEvents_ = 0;
            break;
        case StepPhase::Sibilant:
            sibilant_.clear();
            sibilantEvents_ = 0;
            break;
        default:
            naturalTake_.clear();
            natural_ = core::SpeechLevelAnalyser(ctx_.sampleRate);
            break;
        }
        sinceLook_ = 0;
    }

    double seconds(std::size_t frames) const { return static_cast<double>(frames) / ctx_.sampleRate; }

    // 拼起来的音频（爆破音字表、齿音字表、自然朗读）上的时刻 t 落在哪一段，就加那一段在这一步时间轴上的起点。
    // 三段现在总是首尾相接（这几个阶段不会重来），三个分支给出同一个平移量；不相接的情形目前跑不到，原因同 resetPhase
    double clickOffset(double t) const
    {
        const double p = seconds(plosive_.size());
        const double s = seconds(sibilant_.size());
        if (t < p)
            return seconds(plosiveAt_);
        if (t < p + s)
            return seconds(sibilantAt_) - p;
        return seconds(naturalAt_) - p - s;
    }

    // 标记的起止一起平移，平移量按起点算
    template<class R, class F>
    static void moveMarks(std::optional<R>& r, F offsetAt)
    {
        if (!r)
            return;
        for (auto& m : r->evidence.marks)
        {
            const double d = offsetAt(m.startSec);
            m.startSec += d;
            m.endSec += d;
        }
    }

    std::unique_ptr<core::ClapAnalyser> claps_;
    core::SpeechLevelAnalyser natural_; // 自然念的有声秒数（Manual）
    std::size_t clapEvery_, wordsEvery_;
    std::optional<core::DirectRatioAnalysis> directRatio_;
    std::vector<double> plosive_, sibilant_, naturalTake_;
    std::size_t plosiveAt_ = 0, sibilantAt_ = 0, naturalAt_ = 0; // 三段在这一步时间轴上的起点（帧）
    std::size_t softFrames_ = 0; // 轻拍阶段收到的帧
    std::size_t sinceLook_ = 0; // 距上次数拍手或音节收到的帧
    double cleanClaps_ = 0.0;
    std::int64_t plosiveEvents_ = 0, sibilantEvents_ = 0;
};

// ---- 方向：四个方向各唱一段；别的方向明显更响时追加确认，重唱当前方向与那个方向 ----
class DirectionStep final : public StepScript
{
public:
    static constexpr int kCurrent = 0; // 用户开始时面对的方向就是当前方向
    static constexpr int kConfirmTakes = 2; // 确认重唱的那一对：当前方向、更响的那个方向

    DirectionStep(const StepContext& ctx, AnalysisHook hook) : StepScript(ctx, hook), voice_(ctx.sampleRate)
    {
        addPhase(StepPhase::Direction, static_cast<int>(takes_.size()), thr::session::direction_take_s);
    }

    StepOutput finish() override
    {
        StepOutput out;
        out.results.direction = analysis_.result;
        out.direction = analysis_;
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
        if (manual())
            voice_.push(pick(left, right), frames);
    }

    // 每个方向（确认的每一遍）各自要 direction_min_voice_s 秒有声
    Need need(const Segment&) const override
    {
        const double voiced = voice_.activeSeconds();
        return {core::PhaseNeed::VoiceSeconds, thr::session::direction_min_voice_s - voiced, voiced};
    }

    // 手动开始时，确认的每一遍朝哪个方向在这一段开始时就给出：待开始的卡片要先告诉用户转到哪里
    void segmentBegins(const Segment& seg) override
    {
        voice_ = core::SpeechLevelAnalyser(ctx_.sampleRate);
        if (manual() && seg.phase == StepPhase::Confirm)
            live_.directionIndex = seg.round == 1 ? kCurrent : analysis_.repeatIndex;
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
            analysed();
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
    core::SpeechLevelAnalyser voice_; // 当前这一段的有声秒数（Manual）
};
} // namespace

std::unique_ptr<StepScript> makeScript(core::StepId id, const StepContext& ctx, AnalysisHook hook)
{
    switch (id)
    {
    case core::StepId::Connect: {
        StepContext automatic = ctx; // 接通只是一个观察窗，不分段、不等用户
        automatic.control = PhaseControl::Auto;
        return std::make_unique<ConnectStep>(automatic, hook);
    }
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
