// SPDX-License-Identifier: GPL-3.0-or-later
//
// 人声测试用的合成信号：字表（元音音节，可在起音处叠低频爆发）、擦音、口水音式的单样本尖峰。
// 全部由固定种子生成，不读任何音频文件。
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/level/Synth.h"

namespace miciri::testing
{
// 汉宁形状的起落：前后各 rampSeconds，中间为 1
inline double ramp(std::size_t i, std::size_t length, double fs, double rampSeconds)
{
    const double t = static_cast<double>(i) / fs;
    const double rest = static_cast<double>(length - i) / fs;
    const double edge = std::min(t, rest);
    return edge >= rampSeconds ? 1.0 : 0.5 - 0.5 * std::cos(kPi * edge / rampSeconds);
}

// 元音：基频 f0 的谐波到 4 kHz 以下，幅度按 1/k，峰值约为 amplitude
inline void addVowel(std::vector<double>& x, double fs, double startSec, double seconds, double f0, double amplitude,
                     double rampSeconds = 0.01)
{
    const auto start = static_cast<std::size_t>(startSec * fs);
    const auto length = static_cast<std::size_t>(seconds * fs);
    const int harmonics = static_cast<int>(3900.0 / f0);
    for (std::size_t i = 0; i < length && start + i < x.size(); ++i)
    {
        double s = 0.0;
        for (int k = 1; k <= harmonics; ++k)
            s += std::sin(2.0 * kPi * k * f0 * static_cast<double>(i) / fs + 0.3 * k) / k;
        x[start + i] += amplitude * 0.5 * ramp(i, length, fs, rampSeconds) * s;
    }
}

// 喷麦式的低频爆发：一个 hz 的正弦乘汉宁窗，长 seconds
inline void addPop(std::vector<double>& x, double fs, double startSec, double seconds, double hz, double amplitude)
{
    const auto start = static_cast<std::size_t>(startSec * fs);
    const auto length = static_cast<std::size_t>(seconds * fs);
    for (std::size_t i = 0; i < length && start + i < x.size(); ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(length));
        x[start + i] += amplitude * w * std::sin(2.0 * kPi * hz * static_cast<double>(i) / fs);
    }
}

// 一遍字表：每个音节 0.3 s，间隔 0.8 s，底噪 -80 dBFS。popsDb[i] 是第 i 个音节起音处爆发的峰值（相对元音峰值，dB），
// 小于 -100 表示这个字不喷。
inline std::vector<double> wordList(double fs, const std::vector<double>& popsDb, double gain = 1.0)
{
    const double pitch = 1.1;
    std::vector<double> x =
        whiteNoise(static_cast<std::size_t>((0.5 + pitch * static_cast<double>(popsDb.size())) * fs), 1e-4, 11);
    for (std::size_t i = 0; i < popsDb.size(); ++i)
    {
        const double t = 0.5 + pitch * static_cast<double>(i);
        if (popsDb[i] > -100.0)
            addPop(x, fs, t, 0.04, 60.0, 0.3 * fromDb(popsDb[i]));
        addVowel(x, fs, t + 0.02, 0.3, 140.0, 0.3);
    }
    for (double& v : x)
        v *= gain;
    return x;
}

// 白噪声经两次一阶差分：能量集中在高频，低频几乎没有（像擦音）。整体 RMS 为 rms，带起落
inline void addHiss(std::vector<double>& x, double fs, double startSec, double seconds, double rms, std::uint64_t seed,
                    double rampSeconds = 0.01)
{
    const auto start = static_cast<std::size_t>(startSec * fs);
    const auto length = static_cast<std::size_t>(seconds * fs);
    const std::vector<double> w = whiteNoise(length + 2, 1.0, seed);
    for (std::size_t i = 0; i < length && start + i < x.size(); ++i)
    {
        const double d = (w[i + 2] - 2.0 * w[i + 1] + w[i]) / std::sqrt(6.0);
        x[start + i] += rms * ramp(i, length, fs, rampSeconds) * d;
    }
}

// 白噪声段（全频），带起落：作齿音时 3-12 kHz 的电平已知
inline void addNoiseBurst(std::vector<double>& x, double fs, double startSec, double seconds, double rms,
                          std::uint64_t seed)
{
    const auto start = static_cast<std::size_t>(startSec * fs);
    const auto length = static_cast<std::size_t>(seconds * fs);
    const std::vector<double> w = whiteNoise(length, rms, seed);
    for (std::size_t i = 0; i < length && start + i < x.size(); ++i)
        x[start + i] += ramp(i, length, fs, 0.01) * w[i];
}

// 口水音式的尖峰：单个样本
inline void addTick(std::vector<double>& x, double fs, double atSec, double amplitude)
{
    const auto i = static_cast<std::size_t>(atSec * fs);
    if (i < x.size())
        x[i] += amplitude;
}

// 从 loHz 到 hiHz、每 stepHz 一个正弦（幅度按 100 Hz / f 递减、相位随机），只在 [onSec, offSec) 内发声
inline void addSineBand(std::vector<double>& x, double fs, double loHz, double hiHz, double stepHz, double amplitude,
                        double onSec, double offSec, std::uint64_t seed)
{
    Rng rng(seed);
    const auto from = static_cast<std::size_t>(onSec * fs);
    const auto to = std::min(x.size(), static_cast<std::size_t>(offSec * fs));
    for (double hz = loHz; hz < hiHz; hz += stepHz)
    {
        const double a = amplitude * 100.0 / hz;
        const double phase = 2.0 * kPi * rng.uniform();
        const double dphi = 2.0 * kPi * hz / fs;
        for (std::size_t i = from; i < to; ++i)
            x[i] += a * ramp(i - from, to - from, fs, 0.01) * std::sin(dphi * static_cast<double>(i) + phase);
    }
}
} // namespace miciri::testing
