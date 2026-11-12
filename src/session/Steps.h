// SPDX-License-Identifier: GPL-3.0-or-later
//
// 各步的编排：一步由若干“段”组成（阶段 × 第几遍），每段按采样计数定长；音频按到达顺序喂进来，
// 喂满一段就进下一段。阶段结束时可以要求重来这一阶段（测试中的提示，比如轻拍削波了），
// 这时先丢掉 notice_s 秒的音频作为倒数，再从这一阶段的第一遍重来；一步最多自动重来 restarts_max 次，之后照常往下走，
// 由分析给出再测与原因。所有时长都是采样计数，与墙钟无关。
//
// 本文件不涉及线程：Session 的工作线程把样本喂给 StepScript，单元测试也可以直接喂。
// 分析都在这里调 core 的函数；每调一次分析就调一次 hook（宿主探针的分析计数）。
#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include "result/Results.h"

namespace miciri::session
{
using AnalysisHook = void (*)() noexcept;

// 正在进行的那一步的实时信息，对应桥快照的 live
struct Live
{
    core::StepPhase phase = core::StepPhase::None;
    int round = 0; // 从 1 起；没在跑时为 0
    int rounds = 0;
    double phaseElapsedSec = 0.0; // 当前这一遍的已用与总时长
    double phaseDurationSec = 0.0;
    std::vector<core::ClapState> claps; // 房间步：每一下拍手的判定，按先后
    int directionIndex = 0;
    core::FlowNotice notice = core::FlowNotice::None;
    double noticeSec = 0.0; // 提示剩余的秒数，到 0 就重来
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
};

// 一步跑完的产出：只填这一步产生的那几项结果
struct StepOutput
{
    core::AllResults results;
    std::optional<double> speechLevelDbfsA;
    std::optional<bool> signalSeen;
};

class StepScript
{
public:
    virtual ~StepScript() = default;

    // 按到达顺序喂样本；right 在单声道时为空。done() 之后再喂的样本被忽略
    void feed(const double* left, const double* right, std::size_t frames);
    bool done() const { return done_; }
    // 收完之后做整段分析，只能调一次
    virtual StepOutput finish() = 0;

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
    StepScript(const StepContext& ctx, AnalysisHook hook);

    // 在计划末尾追加 rounds 遍、每遍 seconds 秒的阶段
    void addPhase(core::StepPhase phase, int rounds, double seconds);
    void analysed() const;
    std::size_t framesFor(double seconds) const;
    const double* pick(const double* left, const double* right) const; // 单声道分析用的那一路

    // 子类的钩子：样本落在哪一段；阶段收满时检查（返回非 None 的提示就重来这一阶段）；能否提前收尾
    virtual void take(const Segment& seg, const double* left, const double* right, std::size_t frames) = 0;
    virtual core::FlowNotice endOfPhase(core::StepPhase) { return core::FlowNotice::None; }
    virtual void resetPhase(core::StepPhase) {}
    virtual bool finishedEarly() const { return false; }

    StepContext ctx_;
    Live live_;

private:
    void updateLive();
    void restartPhase(core::FlowNotice notice);

    AnalysisHook hook_;
    std::vector<Segment> plan_;
    std::size_t segment_ = 0;
    std::size_t inSegment_ = 0;
    std::size_t consumed_ = 0; // 计划内已收的帧（不含倒数）
    std::size_t noticeLeft_ = 0;
    long restarts_ = 0; // 这一步已经重来的次数
    bool done_ = false;
};

std::unique_ptr<StepScript> makeScript(core::StepId id, const StepContext& ctx, AnalysisHook hook);
// 一步的计划时长（准备页写“约 N 秒”）；方向步不含可能追加的确认
double plannedSeconds(core::StepId id);
} // namespace miciri::session
