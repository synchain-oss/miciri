// SPDX-License-Identifier: GPL-3.0-or-later
//
// 噪声：A 加权后 100 ms 帧功率的 P10 作底噪，A 加权 SNR 是唯一的分档依据；全频电平只作信息。
// 采样率高于分析上限时先整数倍抽取。逐块喂样本，结果与分块方式无关（滤波器状态和帧累加都按样本顺序）。
#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "level/Dsp.h"
#include "result/Results.h"

namespace miciri::core
{
struct NoiseContext
{
    // 说话时 A 加权的活跃电平（dBFS），来自 SpeechLevelAnalyser。没有它就算不出 SNR：
    // snrDb 保持 0、档位为 fair 并要求再测，所以读 snrDb 之前先看 confidence。
    std::optional<double> speechLevelDbfsA;
};

class NoiseAnalyser
{
public:
    NoiseAnalyser(double sampleRate, NoiseContext ctx = {});
    void push(const double* samples, std::size_t count);
    NoiseResult finish() const;

private:
    NoiseContext ctx_;
    dsp::AnalysisInput input_;
    dsp::BiquadCascade weighting_;
    dsp::FrameMeanSquare weighted_;
    dsp::FrameMeanSquare plain_;
    std::vector<double> decimated_;
};
NoiseResult analyseNoise(const std::vector<double>& samples, double sampleRate, const NoiseContext& ctx = {});

// 说话的 A 加权活跃电平：50 ms 帧，取落在第 95 百分位帧电平 15 dB 以内、且比帧 P10 高 10 dB 以上的帧的平均功率。
// 参照第 95 百分位而不是最响的一帧，是为了不让一声咳嗽或碰麦把“说话电平”拽高。
class SpeechLevelAnalyser
{
public:
    explicit SpeechLevelAnalyser(double sampleRate);
    void push(const double* samples, std::size_t count);
    std::optional<double> finish() const; // 没有活跃帧时为空

private:
    dsp::AnalysisInput input_;
    dsp::BiquadCascade weighting_;
    dsp::FrameMeanSquare frames_;
    std::vector<double> decimated_;
};
std::optional<double> speechLevelDbfsA(const std::vector<double>& samples, double sampleRate);
} // namespace miciri::core
