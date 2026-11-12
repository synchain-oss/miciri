// SPDX-License-Identifier: GPL-3.0-or-later
//
// 合成的拍手录音：每一下是一个直达脉冲加一段指数衰减的白噪声尾巴（已知 T60），垫在白噪声底噪上。
// 白噪声的直达与尾巴在任何频带里的能量比都相同，所以 D/R 有解析真值；衰减斜率就是 T60。
#pragma once

#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

#include "core/level/Synth.h"

namespace miciri::testing
{
struct ClapTrain
{
    double fs = 48000.0;
    int claps = 8;
    double leadSec = 1.0;
    double gapSec = 2.0;
    double direct = 0.5; // 直达脉冲幅度（单个样本）
    double tailGain = 0.02; // 尾巴起点的噪声 RMS
    double tailDelayMs = 1.0; // 尾巴相对直达的起点
    double noiseRms = 1e-5;
    std::uint64_t seed = 7;
    std::function<double(int)> t60 = [](int) { return 0.4; };
    // 可选：对第 i 下的尾巴逐样本施加的增益（时间从直达算起，秒），用来模拟门限
    std::function<double(int, double)> tailShape = nullptr;
};

inline double clapTime(const ClapTrain& c, int i)
{
    return c.leadSec + c.gapSec * i;
}

// 尾巴相对直达之后 fromSec 起的总能量（样本平方和）的期望
inline double tailEnergyFrom(const ClapTrain& c, double t60, double fromSec)
{
    const double decayPerSample = std::exp(-6.0 * std::log(10.0) / (t60 * c.fs)); // 能量每样本的衰减
    const double start = std::max(fromSec, c.tailDelayMs * 1e-3);
    const double first = c.tailGain * c.tailGain * std::pow(decayPerSample, (start - c.tailDelayMs * 1e-3) * c.fs);
    return first / (1.0 - decayPerSample);
}

inline std::vector<double> makeClaps(const ClapTrain& c)
{
    const double total = clapTime(c, c.claps - 1) + c.gapSec;
    std::vector<double> x = whiteNoise(static_cast<std::size_t>(total * c.fs), c.noiseRms, c.seed);
    Rng rng(c.seed + 1000);
    for (int i = 0; i < c.claps; ++i)
    {
        const auto at = static_cast<std::size_t>(clapTime(c, i) * c.fs);
        x[at] += c.direct;
        const double t60 = c.t60(i);
        const auto delay = static_cast<std::size_t>(c.tailDelayMs * 1e-3 * c.fs);
        const auto length = static_cast<std::size_t>(std::min(1.5, 3.0 * t60) * c.fs);
        for (std::size_t n = 0; n < length && at + delay + n < x.size(); ++n)
        {
            const double t = static_cast<double>(n) / c.fs;
            double v = c.tailGain * std::exp(-3.0 * std::log(10.0) * t / t60) * rng.gauss();
            if (c.tailShape)
                v *= c.tailShape(i, t + c.tailDelayMs * 1e-3);
            x[at + delay + n] += v;
        }
    }
    return x;
}
} // namespace miciri::testing
