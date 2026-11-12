// SPDX-License-Identifier: GPL-3.0-or-later
//
// 证据波形：一步里被分析的那一路的最小、最大包络，加上喷麦、齿音、口水音的事件，给报告详情画图用。
// 只在工作线程生成，按步存在 Session 里（每步只留最近一次做完的那份），只在内存：不写盘、不上传、不进日志。
//
// 时间轴：相对这一步开始的采样计时，只算分析实际用到的样本。阶段重来时倒数的那段和被丢掉的那次不算，
// 所以包络与 EvidenceMark 的时间对得上（位置步把各阶段的标记换算到这条轴上，见 Steps.cpp）。
//
// 包络：先按“基础格”记，基础格是不短于 session.evidence_hop_min_s 的最少整数个采样；做完一步把相邻的 k
// 个基础格并成一格， k 取让格数不超过 session.evidence_cells_max 的最小整数。所以 hopSec 是基础格的整数倍，不小于
// max(evidence_hop_min_s, durationSec / evidence_cells_max)，比它多不到一个基础格；每一格都恰好是 [i·hopSec,
// (i+1)·hopSec) 里的样本。 每格的值是千分之一满刻度的整数，夹在 -1000..1000；NaN 不计入，整格都是 NaN 时记 0。
// 记录时的临时表每秒约 500 格、每格 4 字节（最长的位置步约一分钟，约 120 KB）；存进 Session 的每步不超过
// evidence_cells_max 格（16 位整数，约 32 KB）加 evidence_events_max 个事件。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "result/Results.h"

namespace miciri::session
{
// 顺序与桥契约的 EVIDENCE_EVENT_KINDS 相同（会话测试对拍）
enum class EvidenceEventKind : std::uint8_t
{
    Plosive, // 喷麦：levelDb 是起音段低频峰值相对同音节元音稳态低频电平，dB
    Sibilance, // 齿音：这个擦音 6-12 kHz 减底噪后的均方电平，dBFS（高频项关闭时是 3-6 kHz）
    Mouth, // 口水音：约 click.highpass_hz 高通后的峰值，dBFS
};

struct EvidenceEvent
{
    EvidenceEventKind kind = EvidenceEventKind::Plosive;
    double timeSec = 0.0;
    double durSec = 0.0;
    double levelDb = 0.0;
};

struct EvidencePack
{
    core::StepId step = core::StepId::Connect;
    std::uint64_t rev = 0; // 这一步的证据第几次更新，由 Session 存进来时给
    double durationSec = 0.0;
    double hopSec = 0.0;
    std::vector<std::int16_t> min, max; // 千分之一满刻度
    std::vector<EvidenceEvent> events; // 按时间排序
};

class EnvelopeRecorder
{
public:
    explicit EnvelopeRecorder(double sampleRate);

    void push(const double* x, std::size_t frames);
    // 阶段从这里开始；rewind() 回到最近一次 mark()，之后收到的样本不算（阶段重来）
    void mark() { saved_ = now_; }
    void rewind();
    std::size_t frames() const { return now_.frames; }
    // 合并成不超过 evidence_cells_max 格，填 pack 的 durationSec、hopSec、min、max
    void finish(EvidencePack& pack) const;

private:
    struct Cursor
    {
        std::size_t cells = 0; // 已经收满的基础格
        std::size_t frames = 0;
        std::size_t inCell = 0;
        double lo = 0.0, hi = 0.0; // 当前未收满那一格的最小、最大
        bool any = false; // 当前那一格有没有不是 NaN 的样本
    };
    void flush();

    double sampleRate_;
    std::size_t base_; // 基础格的采样数
    std::vector<std::int16_t> lo_, hi_;
    Cursor now_, saved_;
};

// 一步的结果里喷麦、齿音、口水音的标记换成事件：按时间排序，最多 evidence_events_max 个（留最早的），其余种类的标记不要
std::vector<EvidenceEvent> evidenceEvents(const core::AllResults& results);

// getEvidence 的回执载荷，字段名与顺序按桥契约的 EVIDENCE_PACK、EVIDENCE_ENVELOPE、EVIDENCE_EVENT
std::string evidenceJson(const EvidencePack& pack);
} // namespace miciri::session
