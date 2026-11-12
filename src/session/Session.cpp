// SPDX-License-Identifier: GPL-3.0-or-later
#include "session/Session.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>
#include <vector>

#include "config/Thresholds.h"
#include "decide/Decide.h"
#include "level/Dsp.h"
#include "session/Grading.h"

namespace miciri::session
{
using core::RetestReason;
using core::StepId;
using core::StepStatus;
using Clock = std::chrono::steady_clock;

namespace
{
// 工作线程每次最多取多少帧、环空时睡多久。只影响取样的节奏，不影响任何测量（测量按采样计数）。
constexpr std::size_t kChunkFrames = 4096;
constexpr std::chrono::milliseconds kIdlePoll{10};

std::size_t index(StepId id)
{
    return static_cast<std::size_t>(id);
}

std::size_t index(Section s)
{
    return static_cast<std::size_t>(s);
}

double secondsSince(Clock::time_point t0, Clock::time_point t)
{
    return std::chrono::duration<double>(t - t0).count();
}

bool present(const core::AllResults& r, Section s)
{
    switch (s)
    {
    case Section::NoSignal:
        return r.noSignal.has_value();
    case Section::Channels:
        return r.channels.has_value();
    case Section::Gain:
        return r.gain.has_value();
    case Section::Noise:
        return r.noise.has_value();
    case Section::Hum:
        return r.hum.has_value();
    case Section::DeviceProcessing:
        return r.deviceProcessing.has_value();
    case Section::Reverb:
        return r.reverb.has_value();
    case Section::DirectRatio:
        return r.directRatio.has_value();
    case Section::Plosive:
        return r.plosive.has_value();
    case Section::Sibilance:
        return r.sibilance.has_value();
    case Section::Click:
        return r.click.has_value();
    case Section::Direction:
        return r.direction.has_value();
    case Section::Distance:
    case Section::Count:
        break;
    }
    return r.distance.has_value();
}

template<class T>
bool trusted(const std::optional<T>& r)
{
    return r && r->confidence.level == core::Reliability::Trusted;
}

SectionSet setOf(std::initializer_list<Section> items)
{
    SectionSet s{};
    for (Section i : items)
        s[index(i)] = true;
    return s;
}
} // namespace

SectionSet staleOnChange(core::ChangeKind kind)
{
    switch (kind)
    {
    case core::ChangeKind::Gain: // 齿音给的是固定增益下的绝对电平，增益一变 A/B 比较就失效
        return setOf({Section::Gain, Section::Noise, Section::Hum, Section::Sibilance});
    case core::ChangeKind::Position:
        return setOf({Section::Gain, Section::Noise, Section::DirectRatio, Section::Plosive, Section::Sibilance,
                      Section::Click, Section::Direction, Section::Distance});
    case core::ChangeKind::Room:
        break;
    }
    SectionSet all{};
    all.fill(true);
    return all;
}

SectionSet staleOnLayoutChange()
{
    return setOf({Section::Gain, Section::Noise, Section::Hum});
}

void forgetStale(StepContext& ctx, const SectionSet& stale)
{
    if (stale[static_cast<std::size_t>(Section::Gain)])
    {
        ctx.speechLevelDbfsA.reset();
        ctx.noiseFloorRms = 0.0;
    }
}

StepId producerOf(Section s)
{
    switch (s)
    {
    case Section::NoSignal:
    case Section::Channels:
        return StepId::Connect;
    case Section::Gain:
        return StepId::Gain;
    case Section::Noise:
    case Section::Hum:
    case Section::DeviceProcessing:
        return StepId::Quiet;
    case Section::Reverb:
        return StepId::Room;
    case Section::Direction:
        return StepId::Direction;
    default:
        return StepId::Position;
    }
}

std::optional<core::DistanceDecision> decideDistanceOf(const core::AllResults& r)
{
    using core::EvidenceLevel;
    if (!r.plosive && !r.directRatio)
        return std::nullopt; // 位置步没做过
    core::DistanceEvidence e;
    if (trusted(r.plosive) && kSeverityGraded.plosive)
        e.plosive = core::evidenceOf(r.plosive->verdict.severity);
    // 齿音与口水音未分档：severity 停在最低档不表示“没有证据”，按没测处理，不当成良好
    if (trusted(r.sibilance) && kSeverityGraded.sibilance)
        e.sibilance = core::evidenceOf(r.sibilance->verdict.severity);
    if (trusted(r.click) && kSeverityGraded.click)
        e.click = core::evidenceOf(r.click->verdict.severity);
    if (trusted(r.directRatio))
    {
        const auto& dr = *r.directRatio;
        e.lowDirectRatio = dr.verdict.band != core::DrBand::Low ? EvidenceLevel::None
                           : dr.measures.lowerBoundOnly         ? EvidenceLevel::Weak // 真值只会更高
                                                                : EvidenceLevel::Strong;
    }
    if (trusted(r.noise))
        e.lowSnr = r.noise->verdict.band == core::NoiseBand::Bad ? EvidenceLevel::Strong : EvidenceLevel::None;
    return core::decideDistance(e);
}

std::optional<core::DistanceResult> deriveDistance(const core::AllResults& r)
{
    const auto d = decideDistanceOf(r);
    return d ? std::optional<core::DistanceResult>{d->result} : std::nullopt;
}

void refreshDistance(core::AllResults& r, std::optional<core::DistanceResult>& previous, StepId completed)
{
    auto old = r.distance;
    r.distance = deriveDistance(r);
    if (!r.distance)
        previous.reset();
    else if (old && (completed == StepId::Position || r.distance->verdict.kind != old->verdict.kind ||
                     r.distance->confidence.level != old->confidence.level))
        previous = std::move(old);
}

struct Session::Worker
{
    StepId id = StepId::Connect;
    std::unique_ptr<StepScript> script;
    double sampleRate = 0.0;
    Clock::time_point armedAt;
    std::atomic<bool> stop{false};
    std::atomic<bool> exited{false}; // run() 已经返回：之后不再碰采集环与状态，join 不会等
    std::thread thread;
};

Session::Session(AnalysisHook hook) : hook_(hook)
{
    for (std::size_t i = 0; i < kStepCount; ++i)
        planned_[i] = plannedSeconds(static_cast<StepId>(i));
}

Session::~Session()
{
    const std::lock_guard<std::mutex> control(control_);
    signalStop();
    if (worker_ != nullptr && worker_->thread.joinable())
        worker_->thread.join(); // 只有卸载时会等一次整段分析
}

void Session::setHostTraits(const HostTraits& traits)
{
    const std::lock_guard<std::mutex> lock(state_);
    traits_ = traits;
    bump();
}

// 让工作线程停下、撤销采集，但不等它：整段分析不可中断，控制函数（含宿主的 prepareToPlay）不能被它拖住。
// 它收到停止后不再改状态；complete、invalidate、publishLive 也只认 current_。
void Session::signalStop()
{
    if (worker_ == nullptr)
        return;
    worker_->stop.store(true, std::memory_order_release);
    tap_.disarm();
}

// 回收工作线程，回收不了就返回 false：一步正在跑，或被叫停的那一步可能还在整段分析里（不等它）。
// 自己收尾的那一步（complete、invalidate 之后）马上就退出，等它一下。
// 只有这里之后才会重新 arm 采集环，所以旧线程不会和新的一步抢环。
bool Session::reapWorker()
{
    if (worker_ == nullptr)
        return true;
    if (!worker_->exited.load(std::memory_order_acquire) &&
        (worker_->stop.load(std::memory_order_acquire) || running()))
        return false;
    if (worker_->thread.joinable())
        worker_->thread.join();
    worker_.reset();
    return true;
}

void Session::prepare(double sampleRate, int channels)
{
    const std::lock_guard<std::mutex> control(control_);
    bool rateChanged = false;
    bool layoutChanged = false;
    {
        const std::lock_guard<std::mutex> lock(state_);
        rateChanged = sampleRate_ > 0.0 && sampleRate != sampleRate_;
        layoutChanged = channels_ > 0 && channels != channels_;
        if (!rateChanged && !layoutChanged && sampleRate_ > 0.0)
            return;
    }
    std::optional<StepId> voided;
    if (worker_ != nullptr)
    {
        voided = worker_->id;
        signalStop();
    }
    const std::lock_guard<std::mutex> lock(state_);
    if (voided && current_ == voided)
    {
        StepState& s = steps_[index(*voided)];
        s.status = StepStatus::Invalid;
        s.invalidReason = rateChanged ? RetestReason::RateChanged : RetestReason::LayoutChanged;
        current_.reset();
        live_ = Live{};
    }
    sampleRate_ = sampleRate;
    channels_ = std::max(1, channels);
    SectionSet hit{};
    if (layoutChanged)
        hit = staleOnLayoutChange();
    if (rateChanged)
        hit.fill(true);
    for (std::size_t i = 0; i < kSectionCount; ++i)
        if (hit[i] && present(results_, static_cast<Section>(i)))
            stale_[i] = true;
    forgetStale(context_, hit);
    if (layoutChanged)
    {
        // 声道提示跟着布局走：撤掉，之后的单声道分析用左路
        results_.channels.reset();
        previous_.channels.reset();
        stale_[index(Section::Channels)] = false;
        context_.channel = core::AnalysedChannel::Left;
        channelPromptDismissed_ = false;
    }
    bump();
}

Session::Start Session::startStep(StepId id, bool hostOffline)
{
    const std::lock_guard<std::mutex> control(control_);
    if (!reapWorker())
        return Start::Busy; // 一步正在跑，或刚取消、作废的那一步还在收尾

    auto worker = std::make_unique<Worker>();
    int channels = 1;
    {
        const std::lock_guard<std::mutex> lock(state_);
        if (current_)
            return Start::Busy;
        if (traits_.validationHost || hostOffline || sampleRate_ <= 0.0)
            return Start::Disabled;
        StepContext ctx = context_;
        ctx.sampleRate = sampleRate_;
        ctx.stereo = channels_ >= 2;
        worker->id = id;
        worker->sampleRate = sampleRate_;
        worker->script = makeScript(id, ctx, hook_);
        channels = channels_;
        before_ = steps_[index(id)];
        steps_[index(id)] = StepState{StepStatus::Running, RetestReason::None, 0.0};
        current_ = id;
        changedDuring_ = SectionSet{};
        offlineRender_ = false;
        live_ = worker->script->live();
        runningPlannedSec_ = worker->script->plannedSec();
    }
    // 分配环、起线程都可能失败：失败就回滚，不让这一步一直停在进行中，也不把异常抛给宿主
    try
    {
        tap_.arm(worker->sampleRate, channels, thr::session::capture_buffer_s);
        worker->armedAt = Clock::now();
        worker->thread = std::thread([this, w = worker.get()] {
            run(*w);
            w->exited.store(true, std::memory_order_release);
        });
    }
    catch (...)
    {
        tap_.disarm();
        const std::lock_guard<std::mutex> lock(state_);
        steps_[index(id)] = before_;
        current_.reset();
        live_ = Live{};
        bump();
        return Start::Failed;
    }
    worker_ = std::move(worker);
    bump();
    return Start::Ok;
}

void Session::cancelStep()
{
    const std::lock_guard<std::mutex> control(control_);
    signalStop();
    const std::lock_guard<std::mutex> lock(state_);
    if (!current_)
        return; // 已经收尾（或被作废）的那一步不再恢复
    steps_[index(*current_)] = before_;
    current_.reset();
    live_ = Live{};
    bump();
}

void Session::reportChange(core::ChangeKind kind)
{
    const std::lock_guard<std::mutex> lock(state_);
    const SectionSet hit = staleOnChange(kind);
    for (std::size_t i = 0; i < kSectionCount; ++i)
    {
        if (hit[i] && present(results_, static_cast<Section>(i)))
            stale_[i] = true;
        // 正在跑的那一步拿的是开始时的上下文，收到的音频也有一段在改动之前：它这几项的产出一到就是过期的
        if (hit[i] && current_)
            changedDuring_[i] = true;
    }
    forgetStale(context_, hit);
    bump();
}

void Session::dismissChannelPrompt()
{
    const std::lock_guard<std::mutex> lock(state_);
    channelPromptDismissed_ = true;
    bump();
}

bool Session::running() const
{
    const std::lock_guard<std::mutex> lock(state_);
    return current_.has_value();
}

StepContext Session::context() const
{
    const std::lock_guard<std::mutex> lock(state_);
    return context_;
}

SessionView Session::view() const
{
    SessionView v;
    v.durationSec = planned_;
    const std::lock_guard<std::mutex> lock(state_);
    v.steps = steps_;
    v.runs = runs_;
    v.evidenceRev = evidenceRev_;
    v.results = results_;
    v.previous = previous_;
    v.direction = direction_;
    v.stale = stale_;
    v.live = live_;
    v.running = current_.has_value();
    if (current_)
        v.durationSec[index(*current_)] = std::max(v.durationSec[index(*current_)], runningPlannedSec_);
    v.offlineRender = offlineRender_;
    v.channelPromptDismissed = channelPromptDismissed_;
    v.noMonoTracks = traits_.noMonoTracks;
    return v;
}

std::shared_ptr<const EvidencePack> Session::evidence(StepId id) const
{
    const std::lock_guard<std::mutex> lock(state_);
    return evidence_[index(id)];
}

// ---- 工作线程 ----

void Session::run(Worker& w)
{
    const auto channels = static_cast<std::size_t>(tap_.channels());
    std::vector<float> interleaved(kChunkFrames * channels);
    std::vector<double> left(kChunkFrames);
    std::vector<double> right(channels > 1 ? kChunkFrames : 0);
    auto lastAudio = w.armedAt;
    // 宿主送来的音频是否明显快于实时：宿主在离线渲染却没有告诉插件
    auto fasterThanRealtime = [&](Clock::time_point now) {
        const double audio = static_cast<double>(tap_.framesArrived()) / w.sampleRate;
        return audio > secondsSince(w.armedAt, now) * thr::session::realtime_ratio_max;
    };
    std::optional<RetestReason> fail;
    while (!fail)
    {
        if (w.stop.load(std::memory_order_acquire))
            break;
        if (tap_.offline())
            fail = RetestReason::OfflineRender;
        else if (tap_.overflowed()) // 环满：多半是音频来得太快，否则是本线程被拖住了
            fail = fasterThanRealtime(Clock::now()) ? RetestReason::OfflineRender : RetestReason::Overflow;
        else if (tap_.layoutMismatch())
            fail = RetestReason::LayoutChanged;
        if (fail)
            break;
        const std::size_t n = tap_.pop(interleaved.data(), kChunkFrames);
        const auto now = Clock::now();
        if (n == 0)
        {
            if (secondsSince(lastAudio, now) > thr::no_signal::no_callback_s)
                fail = RetestReason::CallbackGap;
            else
                std::this_thread::sleep_for(kIdlePoll);
            continue;
        }
        lastAudio = now;
        for (std::size_t i = 0; i < n; ++i)
        {
            left[i] = interleaved[i * channels];
            if (channels > 1)
                right[i] = interleaved[i * channels + 1];
        }
        w.script->feed(left.data(), channels > 1 ? right.data() : nullptr, n);
        // 收尾时一定查；中途要等观察窗够长，免得大缓冲的头一块误判
        if (fasterThanRealtime(now) &&
            (w.script->done() || secondsSince(w.armedAt, now) >= thr::session::realtime_check_min_s))
            fail = RetestReason::OfflineRender;
        else if (w.script->done())
            break;
        else
            publishLive(*w.script);
    }
    tap_.disarm();
    if (w.stop.load(std::memory_order_acquire))
        return; // 取消或重新准备：状态由控制一侧处理
    if (fail)
    {
        invalidate(w.id, *fail);
        return;
    }
    StepOutput out = w.script->finish();
    out.evidence = std::make_shared<EvidencePack>(w.script->evidence(out.results));
    out.evidence->step = w.id;
    if (!w.stop.load(std::memory_order_acquire))
        complete(w.id, std::move(out));
}

void Session::publishLive(const StepScript& script)
{
    const std::lock_guard<std::mutex> lock(state_);
    if (!current_)
        return;
    live_ = script.live();
    steps_[index(*current_)].progressSec = script.progressSec();
    runningPlannedSec_ = script.plannedSec();
    bump();
}

void Session::invalidate(StepId id, RetestReason why)
{
    const std::lock_guard<std::mutex> lock(state_);
    if (current_ != id)
        return;
    StepState& s = steps_[index(id)];
    s.status = StepStatus::Invalid;
    s.invalidReason = why;
    current_.reset();
    live_ = Live{};
    if (why == RetestReason::OfflineRender)
        offlineRender_ = true;
    bump();
}

void Session::complete(StepId id, StepOutput&& out)
{
    const std::lock_guard<std::mutex> lock(state_);
    if (current_ != id)
        return;
    core::AllResults& r = results_;
    // 换下来的旧结果留作“上次”，只在内存里
    auto take = [this](auto& to, auto& from, auto& before, Section s) {
        if (from)
        {
            if (to)
                before = std::move(to);
            to = std::move(from);
            stale_[index(s)] = changedDuring_[index(s)];
        }
    };
    core::AllResults& o = out.results;
    take(r.noSignal, o.noSignal, previous_.noSignal, Section::NoSignal);
    take(r.channels, o.channels, previous_.channels, Section::Channels);
    take(r.gain, o.gain, previous_.gain, Section::Gain);
    take(r.noise, o.noise, previous_.noise, Section::Noise);
    take(r.hum, o.hum, previous_.hum, Section::Hum);
    take(r.deviceProcessing, o.deviceProcessing, previous_.deviceProcessing, Section::DeviceProcessing);
    take(r.reverb, o.reverb, previous_.reverb, Section::Reverb);
    take(r.directRatio, o.directRatio, previous_.directRatio, Section::DirectRatio);
    take(r.plosive, o.plosive, previous_.plosive, Section::Plosive);
    take(r.sibilance, o.sibilance, previous_.sibilance, Section::Sibilance);
    take(r.click, o.click, previous_.click, Section::Click);
    take(r.direction, o.direction, previous_.direction, Section::Direction);
    refreshDistance(r, previous_.distance, id); // 距离每完成一步都重算，规则见 Session.h
    if (out.direction)
        direction_ = out.direction;
    if (id == StepId::Position)
        stale_[index(Section::Distance)] = changedDuring_[index(Section::Distance)];

    // 后面几步要用的量；这一步进行中增益一项过期了，量到的电平就不留
    const bool levelsHold = !changedDuring_[index(Section::Gain)];
    if (out.speechLevelDbfsA && levelsHold)
        context_.speechLevelDbfsA = out.speechLevelDbfsA;
    if (out.signalSeen && *out.signalSeen)
        context_.signalSeenBefore = true;
    if (id == StepId::Connect)
    {
        context_.noiseFloorRms = levelsHold && trusted(r.noSignal)
                                     ? std::sqrt(core::dsp::dbToPower(r.noSignal->measures.p10FrameDbfs))
                                     : 0.0;
        context_.channel = trusted(r.channels) ? r.channels->verdict.analysedChannel : core::AnalysedChannel::Left;
    }

    // 证据跟着这一步最新的结果走：只有做完才换
    ++runs_[index(id)];
    if (out.evidence)
    {
        out.evidence->rev = ++evidenceRev_[index(id)];
        evidence_[index(id)] = std::move(out.evidence);
    }

    StepState& s = steps_[index(id)];
    s.status = StepStatus::Done;
    s.invalidReason = RetestReason::None;
    Live kept;
    if (id == StepId::Room) // 房间步结束后保留，失败页要画每一下的判定
    {
        kept.claps = live_.claps;
        kept.clapDetails = live_.clapDetails;
    }
    current_.reset();
    live_ = std::move(kept);
    live_.notice = core::FlowNotice::Success;
    bump();
}
} // namespace miciri::session
