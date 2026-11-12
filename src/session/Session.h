// SPDX-License-Identifier: GPL-3.0-or-later
//
// Session：步骤状态机、采集、结果与过期规则。不依赖 JUCE，所有权归处理器；编辑器只读快照，可随时销毁重建。
//
// 线程：
//   - 音频线程只碰 tap()（见 AudioTap.h）。
//   - 控制函数（prepare、startStep、cancelStep、reportChange 等）在消息线程或宿主的准备线程上调，
//     由一把控制锁串行化；要停工作线程时先放开状态锁再 join，工作线程发布结果只拿状态锁，不会互等。
//   - 工作线程只在一步进行中存在：开始一步时创建，这一步收完、分析完就退出。空闲时没有线程、音频线程只做一次原子读。
//
// 保护：
//   - 验证宿主与宿主报离线渲染时不开始测试（startStep 回 Disabled）。
//   - 测试中宿主报离线渲染、或音频到得明显快于实时（有的宿主关掉“通知插件离线渲染”后离线渲染也报实时），
//     这一步作废（offline-render）。环满（overflow）、没有回调（callback-gap）、声道数变了（layout-changed）同样作废。
//   - 采样率变化：当前步作废（rate-changed），已有结果全部标过期。声道布局变化：按过期规则处理。
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "result/Results.h"
#include "session/AudioTap.h"
#include "session/Steps.h"

namespace miciri::session
{
inline constexpr std::size_t kStepCount = core::kStepIdNames.size();

// 结果区段，顺序与桥契约的 RESULT_SECTIONS 相同（会话测试对拍）
enum class Section : std::uint8_t
{
    NoSignal,
    Channels,
    Gain,
    Noise,
    Hum,
    DeviceProcessing,
    Reverb,
    DirectRatio,
    Plosive,
    Sibilance,
    Click,
    Direction,
    Distance,
    Count
};
inline constexpr std::size_t kSectionCount = static_cast<std::size_t>(Section::Count);
using SectionSet = std::array<bool, kSectionCount>;

// 过期规则（docs/contracts/RESULT.md）：用户说“我动过了”时，哪几项结果过期
SectionSet staleOnChange(core::ChangeKind kind);
// 声道布局变化时过期的几项
SectionSet staleOnLayoutChange();
// 哪一步产出这一项
core::StepId producerOf(Section s);
// 前面几步留给后面几步的量，跟着过期规则一起作废：增益一项过期，说话电平与底噪都是旧链路上量的，
// 不能再拿来算 SNR 或平顶容差（后面的步拿不到就按再测处理）
void forgetStale(StepContext& ctx, const SectionSet& stale);
// 距离标尺：由喷麦、齿音、口水音（太近）与 D/R、SNR（太远）合成；位置步没做过时为空。
// 未分档的项（见 Grading.h）按没测处理。
std::optional<core::DistanceResult> deriveDistance(const core::AllResults& results);

struct HostTraits
{
    bool validationHost = false; // 插件验证工具：不开始测试
    bool noMonoTracks = false; // 宿主的轨道都是立体声（没有单声道轨），影响声道提示的说法
};

struct StepState
{
    core::StepStatus status = core::StepStatus::Idle; // 这里只有 Idle、Running、Done、Invalid；过期在快照时由区段算出
    core::RetestReason invalidReason = core::RetestReason::None;
    double progressSec = 0.0;
};

// 一份一致的读数，给快照用
struct SessionView
{
    std::array<StepState, kStepCount> steps{};
    std::array<double, kStepCount> durationSec{};
    core::AllResults results;
    SectionSet stale{};
    Live live;
    bool running = false;
    bool offlineRender = false; // 本次或上次测试判出的离线渲染（宿主自己报的由处理器另给）
    bool channelPromptDismissed = false;
    bool noMonoTracks = false;
};

class Session
{
public:
    enum class Start : std::uint8_t
    {
        Ok,
        Busy, // 已有一步在跑
        Disabled, // 验证宿主、离线渲染，或还没 prepare
        Failed, // 分配采集环或起线程失败；状态已回滚
    };

    explicit Session(AnalysisHook hook = nullptr);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    AudioTap& tap() noexcept { return tap_; }

    // ---- 控制（消息线程或宿主的准备线程）----
    void setHostTraits(const HostTraits& traits);
    void prepare(double sampleRate, int channels);
    // hostOffline：宿主此刻报离线渲染（处理器从音频线程读到的最近一块）
    Start startStep(core::StepId id, bool hostOffline = false);
    void cancelStep();
    void reportChange(core::ChangeKind kind);
    void dismissChannelPrompt();

    // ---- 读（任何线程）----
    SessionView view() const;
    // 状态每变一次加一；编辑器据此决定要不要推快照
    std::uint64_t version() const noexcept { return version_.load(std::memory_order_acquire); }
    bool running() const;
    // 后面几步会用到的量（测试与快照用）
    StepContext context() const;

private:
    struct Worker;
    void signalStop();
    bool reapWorker();
    void run(Worker& w);
    void publishLive(const StepScript& script);
    void invalidate(core::StepId id, core::RetestReason why);
    void complete(core::StepId id, StepOutput&& out);
    void bump() noexcept { version_.fetch_add(1, std::memory_order_acq_rel); }

    AnalysisHook hook_;
    std::array<double, kStepCount> planned_{}; // 各步的计划时长，构造时算好
    AudioTap tap_;
    std::mutex control_; // 串行化控制函数；持有它时可以 join 工作线程
    mutable std::mutex state_; // 保护下面的状态；工作线程发布时只拿它
    std::unique_ptr<Worker> worker_;

    // ---- 以下由 state_ 保护 ----
    HostTraits traits_;
    double sampleRate_ = 0.0;
    int channels_ = 0;
    std::array<StepState, kStepCount> steps_{};
    std::optional<core::StepId> current_;
    StepState before_; // 当前步开始前的状态，取消时恢复
    core::AllResults results_;
    SectionSet stale_{};
    SectionSet changedDuring_{}; // 当前这一步进行中，用户上报的改动让哪几项过期；这一步的这几项产出照样过期
    Live live_;
    double runningPlannedSec_ = 0.0; // 进行中那一步此刻的计划时长（方向步追加确认后会变长）
    StepContext context_;
    bool offlineRender_ = false;
    bool channelPromptDismissed_ = false;
    std::atomic<std::uint64_t> version_{0};
};

// 快照 JSON：键是 steps、results、bands、live、connect、offlineRender，
// 字段名与顺序按桥契约（BridgeApi.h）。编辑器把它并进 miciri.state，再补上墙钟给的 callbackAgeSec。
std::string snapshotJson(const SessionView& view);
} // namespace miciri::session
