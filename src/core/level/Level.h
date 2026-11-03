// SPDX-License-Identifier: GPL-3.0-or-later
//
// 电平类分析：无信号诊断、增益（峰值、削波、响度）、设备处理。都是纯函数或只持有自己的样本，不依赖 JUCE。
// 阈值一律读 miciri::thr（thresholds.json 生成），样本进入分析前 NaN 与 Inf 按 0 处理。
#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "result/Results.h"

namespace miciri::core
{
// ---- 无信号诊断 ----
struct NoSignalContext
{
    // 观察窗之前（同一次接通里）是否听到过信号：区分“从一开始全零”与“之前有信号后来全零”
    bool signalSeenBefore = false;
};
// samples 是用户被要求说话之后的观察窗，单声道。“没有回调”由墙钟判断，不在这里。
NoSignalResult analyseNoSignal(const std::vector<double>& samples, double sampleRate, const NoSignalContext& ctx = {});

// ---- 增益 ----
struct GainContext
{
    // 接通步读到的底噪 RMS（线性幅度）。模拟平顶的容差随它放宽；读不到（0）时只用相对容差，宁可漏报。
    double noiseFloorRms = 0.0;
};
// 逐块喂样本，finish() 出结果。模拟平顶要等全局峰值才能定门限，所以样本留在内部；
// 结果只取决于样本序列，与分块方式无关。单声道时 right 传空，结果里的右声道字段与左声道相同。
class GainAnalyser
{
public:
    GainAnalyser(double sampleRate, bool stereo, GainContext ctx = {});
    void push(const double* left, const double* right, std::size_t frames);
    GainResult finish() const;

private:
    double sampleRate_;
    bool stereo_;
    GainContext ctx_;
    std::vector<double> channel_[2];
};
GainResult analyseGain(const std::vector<double>& left, const std::vector<double>* right, double sampleRate,
                       const GainContext& ctx = {});

// ---- 设备处理 ----
struct DeviceProcessingContext
{
    // 底噪（dBFS，全频 RMS）。16 bit 且底噪低到会被量化成零时，零段不算证据；没给就用样本自己的 P10 估。
    std::optional<double> noiseFloorDbfs;
};
DeviceProcessingResult analyseDeviceProcessing(const std::vector<double>& samples, double sampleRate,
                                               const DeviceProcessingContext& ctx = {});
} // namespace miciri::core
