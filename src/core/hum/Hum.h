// SPDX-License-Identifier: GPL-3.0-or-later
//
// 电流声：Welch 谱（2 秒汉宁窗、50% 重叠）里找市电基频（50 或 60 Hz）的谐波。
// 先由最强谐波精确估 f0，再逐个谐波查“峰比 ±5 Hz 局部中值高”且“多数段里都在”；
// 前 10 次谐波里至少 3 个稳定才判电流声。检出的谐波延伸到 1 kHz 以上判嗡嗡声（buzz），
// 2 kHz 以上的稳定窄峰判啸叫。采样率高于分析上限时先整数倍抽取。
// 逐块喂样本：每凑满一段就算一段的谱，结果与分块方式无关。每段先去均值再加窗。
// 每段的功率谱留到 finish()：内存随录音时长线性增长（48 kHz 上约每秒 0.4 MB），按“安静步”这种
// 几秒到几十秒的长度用；整段分析几分钟以上的文件时要留意。
#pragma once

#include <cstddef>
#include <vector>

#include "level/Dsp.h"
#include "result/Results.h"

namespace miciri::core
{
class HumAnalyser
{
public:
    explicit HumAnalyser(double sampleRate);
    void push(const double* samples, std::size_t count);
    HumResult finish() const;

private:
    double sampleRate_;
    dsp::AnalysisInput input_;
    std::size_t segmentLength_;
    std::size_t hop_;
    double windowPower_ = 0.0;
    std::vector<double> window_;
    std::vector<double> decimated_;
    std::vector<double> pending_;
    std::vector<std::vector<double>> segments_; // 每段的功率谱密度
    std::size_t samplesIn_ = 0;
};
HumResult analyseHum(const std::vector<double>& samples, double sampleRate);
} // namespace miciri::core
