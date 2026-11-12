// SPDX-License-Identifier: GPL-3.0-or-later
//
// 喷麦、齿音、口水音三项共用的小件：巴特沃斯滤波、帧功率、汉宁窗短时谱、毫秒换样本数、证据标记截断。
// 只给 voice 模块内部用。不依赖 JUCE，double 精度，没有全局状态。
#pragma once

#include <cstddef>
#include <vector>

#include "level/Dsp.h"
#include "result/Results.h"

namespace miciri::core::voice
{
// 偶数阶巴特沃斯高通或低通，二阶节级联（截止频率处预畸变的双线性变换）。
// 低通截止不低于奈奎斯特频率时没有东西可滤，返回空级联（直通）。
dsp::BiquadCascade butterworth(int order, double hz, double sampleRate, bool highpass);
// 带通：同阶高通与低通串联。只用于一个倍频程以上的宽带，此时通带内与直接设计的带通一致。
dsp::BiquadCascade bandpass(int order, double loHz, double hiHz, double sampleRate);
std::vector<double> filtered(dsp::BiquadCascade cascade, const std::vector<double>& x);

// 整段输入先把 NaN、Inf 当 0，再抽取到不超过 maxRate；rate 写回分析采样率。
std::vector<double> prepare(const std::vector<double>& samples, double sampleRate, double maxRate, double& rate);

// 毫秒换成最接近的整数个样本（可为负，用于“尖峰之前”的窗口）
long long offsetSamples(double ms, double rate);

// 帧均方的 dB：帧长 n、帧移 hop，帧数 (长度 - n) / hop + 1
std::vector<double> framePowerDb(const std::vector<double>& x, std::size_t n, std::size_t hop);

// 对称汉宁窗的短时功率谱 |X[k]|²，不缩放；bandLevelDb 把频带内的功率和换成该频带成分的均方电平（dBFS）
struct Spectrogram
{
    std::vector<std::vector<double>> power;
    double binHz = 0.0;
    double hopSeconds = 0.0;
    double frameSeconds = 0.0;
    std::size_t frameLength = 0;
    double windowPower = 0.0; // 窗函数的平方和
};
Spectrogram spectrogram(const std::vector<double>& x, double rate, double frameMs, double hopMs);
// loHz ≤ f < hiHz 的各 bin 之和
double bandSum(const std::vector<double>& power, double binHz, double loHz, double hiHz);
double bandLevelDb(double powerSum, const Spectrogram& s);

// 只留 value 最大的 maxMarks 个，再按时间排序
void keepStrongest(std::vector<EvidenceMark>& marks, std::size_t maxMarks);
} // namespace miciri::core::voice
