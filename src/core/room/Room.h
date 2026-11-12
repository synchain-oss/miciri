// SPDX-License-Identifier: GPL-3.0-or-later
//
// 房间 T20 与位置 D/R，都由拍手算。两者共用 ClapCapture：逐块喂样本，finish 时找拍手、逐下逐频带分析。
//
// T20：每一下在 500、1k、2k、4k 四个倍频带里各自判断。带通后的能量减去拍手前的噪声，Schroeder 反向积分，
// 衰减降到“噪声加余量”时截断，在 -5..-25 dB 段线性拟合。一下一个频带有效要同时满足：峰噪比够、拟合 R² 够、
// 没有门限。门限只在同一频带内判（尾部出现严格数字零；或频带仍比噪声高 gate_margin_db 时，扣掉拟合斜率
// 预期的自然衰减后 gate_tail_drop_ms 内又降了 gate_tail_drop_db），只作废那一下的那个频带。
// 重复性：每个频带去掉偏离中位数超过 repeat_outlier_frac 的几下，剩下不少于 repeat_min_kept 下才给值，
// 取剩下的中位数。主指标 Tmid 是 500 与 1k 的平均，两个频带都要有值。直达声削波不影响 T20。
//
// D/R：1–4 kHz 带，直达窗内的能量比上其后到截断点的能量，两者都减去噪声。只用没削波的几下，取中位数；
// 尾部高出噪声不到 tail_snr_min_db 时这一下只算下限。
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "result/Results.h"
#include "room/Claps.h"

namespace miciri::core
{
// 一下拍手在一个频带里的情况：ClapBandStatus，取值见 result/Enums.h（进桥名表，在那里定义）

struct ClapBand
{
    double t20Sec = 0.0; // 拟合不出时为 0
    double pnrDb = 0.0;
    double r2 = 0.0;
    ClapBandStatus status = ClapBandStatus::ShortDecay;
};
struct ReverbClap
{
    double timeSec = 0.0;
    double peakDbfs = 0.0;
    bool clipped = false;
    std::array<ClapBand, 4> bands{};
};
struct ReverbAnalysis
{
    ReverbResult result;
    std::vector<ReverbClap> claps; // 逐下的明细，给拍手逐下反馈用
};

struct DirectRatioClap
{
    double timeSec = 0.0;
    double peakDbfs = 0.0;
    bool clipped = false;
    bool usable = false; // 噪声参考齐全且直达能量为正
    double drDb = 0.0;
    double tailSnrDb = 0.0;
    bool lowerBound = false;
};
struct DirectRatioAnalysis
{
    DirectRatioResult result;
    std::vector<DirectRatioClap> claps;
};

class ClapAnalyser
{
public:
    explicit ClapAnalyser(double sampleRate) : capture_(sampleRate) {}
    void push(const double* samples, std::size_t count) { capture_.push(samples, count); }
    ReverbAnalysis finishReverb() const;
    DirectRatioAnalysis finishDirectRatio() const;

private:
    ClapCapture capture_;
};
ReverbAnalysis analyseReverb(const std::vector<double>& samples, double sampleRate);
DirectRatioAnalysis analyseDirectRatio(const std::vector<double>& samples, double sampleRate);
} // namespace miciri::core
