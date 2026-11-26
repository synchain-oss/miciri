// SPDX-License-Identifier: GPL-3.0-or-later
//
// 各步的编排：一步由若干“段”组成（阶段 × 第几遍），每段有按采样计数的时长上限；音频按到达顺序喂进来。
// 阶段结束时可以要求重来这一阶段（测试中的提示，比如轻拍削波了），一步最多重来 restarts_max 次，之后照常往下走，
// 由分析给出再测与原因。所有时长都是采样计数，与墙钟无关。
//
// 段怎么开始、怎么结束有两种（PhaseControl）：
//   - Auto：段首尾相接，喂满一段就进下一段；重来时先丢掉 notice_s 秒的音频作为倒数，再从这一阶段的第一遍重来。
//     宿主探针用它，时间轴与分析次数和手动开始之前一样。
//   - Manual：第一段随这一步开始，之后每段先“待开始”，用户 begin() 才开始收；收够这一段的最低量（need()）后用户可以
//   end()，
//     到时长上限照样自动结束。待开始时收到的音频全部丢掉。重来时回到这一阶段的待开始，带着提示，不倒数。
//     用户的点击会被话筒录进去：开始后先丢掉 click_guard_s 秒；样本先经过同样长的延迟再送去分析，
//     用户结束时延迟里剩下的那段丢掉，到上限或自动收尾时照常全收。字表的几遍合成一段，一口气念完再结束。
//
// 本文件不涉及线程：Session 的工作线程把样本喂给 StepScript，单元测试也可以直接喂。
// 分析都在这里调 core 的函数；每调一次分析就调一次 hook（宿主探针的分析计数）。
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "direction/Direction.h"
#include "result/Results.h"
#include "room/Room.h"
#include "session/Evidence.h"

namespace miciri::session
{
using AnalysisHook = void (*)() noexcept;

enum class PhaseControl : std::uint8_t
{
    Auto,
    Manual,
};

// 正在进行的那一步的实时信息，对应桥快照的 live
struct Live
{
    core::StepPhase phase = core::StepPhase::None;
    int round = 0; // 从 1 起；没在跑时为 0
    int rounds = 0;
    double phaseElapsedSec = 0.0; // 当前这一遍的已用与总时长
    double phaseDurationSec = 0.0;
    std::vector<core::ClapState> claps; // 房间步：每一下拍手的判定，按先后
    std::vector<core::ReverbClap> clapDetails; // 与 claps 一一对应：时间、峰值、是否削波、各频带的 T20 与状态
    int directionIndex = 0;
    core::FlowNotice notice = core::FlowNotice::None;
    double noticeSec = 0.0; // 提示剩余的秒数，到 0 就重来（Auto）；Manual 下恒为 0，提示留到用户再开始
    bool waiting = false; // 这一段在等用户开始（Manual）
    bool canEnd = false; // 这一段已收够最低量，用户可以结束（Manual）
    core::PhaseNeed need = core::PhaseNeed::None; // 最低量的单位
    double needLeft = 0.0; // 还差多少（秒或个），够了是 0
    double heard = 0.0; // 这一段已收到多少，单位同 need
};

// 前面几步留下、后面几步要用的量。开始一步时整份复制给它，之后不再变
struct StepContext
{
    double sampleRate = 48000.0;
    bool stereo = false;
    core::AnalysedChannel channel = core::AnalysedChannel::Left; // 单声道分析用哪一路（声道检查给出）
    bool signalSeenBefore = false; // 之前的接通里听到过信号
    std::optional<double> speechLevelDbfsA; // 增益步说话的 A 加权活跃电平，安静步算 SNR 用
    double noiseFloorRms = 0.0; // 接通步的底噪（线性），增益步的平顶容差用；读不到为 0
    PhaseControl control = PhaseControl::Auto; // 接通步总是 Auto
};

// 一步跑完的产出：只填这一步产生的那几项结果
struct StepOutput
{
    core::AllResults results;
    std::optional<double> speechLevelDbfsA;
    std::optional<bool> signalSeen;
    std::optional<core::DirectionAnalysis> direction; // 方向步：含确认的那一对与较小的优势，“最该改”要用
    std::shared_ptr<EvidencePack> evidence; // 证据波形（见 Evidence.h），工作线程在 finish() 之后生成
};

class StepScript
{
public:
    virtual ~StepScript() = default;

    // 按到达顺序喂样本；right 在单声道时为空。done() 之后再喂的样本被忽略
    void feed(const double* left, const double* right, std::size_t frames);
    bool done() const { return done_; }
    // 用户的意图，只在 Manual 下有效：开始在等开始的那一段；结束正在进行、已收够最低量的那一段。做不到时返回
    // false、什么都不改
    bool begin();
    bool end();
    // 收完之后做整段分析，只能调一次
    virtual StepOutput finish() = 0;
    // 证据波形：分析那一路的包络加 results 里的事件；时间轴是这一步分析实际用到的样本（倒数与重来前的不算）
    EvidencePack evidence(const core::AllResults& results) const;

    const Live& live() const { return live_; }
    double progressSec() const;
    double plannedSec() const;

protected:
    struct Segment
    {
        core::StepPhase phase = core::StepPhase::None;
        int round = 1;
        int rounds = 1;
        std::size_t frames = 0;
    };
    // 一段的最低量：单位、还差多少、已有多少（单位相同）；left ≤ 0 就是够了。一段里够了之后不再回退
    struct Need
    {
        core::PhaseNeed kind = core::PhaseNeed::None;
        double left = 0.0;
        double heard = 0.0;
    };
    StepScript(const StepContext& ctx, AnalysisHook hook);

    // 在计划末尾追加 rounds 遍、每遍 seconds 秒的阶段；每一遍是一段（方向步每个方向一段）
    void addPhase(core::StepPhase phase, int rounds, double seconds);
    // 字表这类“念几遍”的阶段：Auto 下同 addPhase；Manual 下几遍合成一段，上限是总时长
    void addPasses(core::StepPhase phase, int passes, double secondsEach);
    bool manual() const { return ctx_.control == PhaseControl::Manual; }
    // 这一段已送去分析的秒数
    double segmentSeconds() const;
    void analysed() const;
    std::size_t framesFor(double seconds) const;
    const double* pick(const double* left, const double* right) const; // 单声道分析用的那一路
    // 这一步的时间轴上已送去分析的帧（不含倒数、待开始与点击保护，重来时退回）；在 take() 里就是这一块第一个样本的位置
    std::size_t heardFrames() const { return consumed_; }

    // 子类的钩子：样本落在哪一段；阶段收满时检查（返回非 None 的提示就重来这一阶段）；能否提前收尾
    virtual void take(const Segment& seg, const double* left, const double* right, std::size_t frames) = 0;
    virtual core::FlowNotice endOfPhase(core::StepPhase) { return core::FlowNotice::None; }
    virtual void resetPhase(core::StepPhase) {}
    virtual bool finishedEarly() const { return false; }
    // 这一段的最低量（只在 Manual 下问）；一段换成下一段时先调 segmentBegins()，清掉按段记的量
    virtual Need need(const Segment&) const { return {}; }
    virtual void segmentBegins(const Segment&) {}

    StepContext ctx_;
    Live live_;

private:
    void updateLive();
    void restartPhase(core::FlowNotice notice);
    // 一段收尾：keep 为真时延迟里的样本照常送去分析（到上限、自动收尾），否则丢掉（用户结束）
    void closeSegment(const Segment& seg, bool keep, bool early);
    void delay(const Segment& seg, const double* left, const double* right, std::size_t frames);
    void forward(const Segment& seg, const double* left, const double* right, std::size_t frames);

    AnalysisHook hook_;
    EnvelopeRecorder envelope_; // 与 take() 收到的样本同步：pick() 的那一路，阶段重来时一起退回
    std::vector<Segment> plan_;
    std::size_t segment_ = 0;
    std::size_t inSegment_ = 0; // 这一段收到的帧（含还在延迟里的），按它算时长上限
    std::size_t segmentForwarded_ = 0; // 这一段已送去分析的帧
    std::size_t consumed_ = 0; // 已送去分析的帧（不含倒数、待开始与点击保护）
    std::size_t phaseStart_ = 0; // 这一阶段开始时的 consumed_，重来时退回这里
    std::size_t noticeLeft_ = 0;
    long restarts_ = 0; // 这一步已经重来的次数
    bool done_ = false;
    bool waiting_ = false;
    bool reached_ = false; // 这一段已收够最低量
    std::size_t guardFrames_ = 0; // 点击保护的帧数（Auto 为 0）
    std::size_t guardLeft_ = 0; // 开始后还要丢掉的帧
    std::vector<double> delayL_, delayR_; // 点击保护的延迟：[delayHead_, size) 是还没送去分析的样本
    std::size_t delayHead_ = 0;
};

std::unique_ptr<StepScript> makeScript(core::StepId id, const StepContext& ctx, AnalysisHook hook);
// 一步的计划时长（准备页写“约 N 秒”）；方向步不含可能追加的确认
double plannedSeconds(core::StepId id);
} // namespace miciri::session
