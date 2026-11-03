// SPDX-License-Identifier: GPL-3.0-or-later
//
// 音频线程与消息线程之间的电平通道：音频线程只写原子量，消息线程定时读取。
// 音频线程一侧不加锁、不分配、不读墙钟；NaN 与 Inf 输入不会让读数失效：
//   - 峰值只在 |x| 大于当前值时更新，NaN 的比较为假，所以被忽略；Inf 会把峰值顶到无穷，由读取端夹取；
//   - 均方只累计有限样本，NaN 与 Inf 不进均方；
//   - 削波锁存按 |x| >= 阈值判定，Inf 为真、NaN 为假。
#pragma once

#include <atomic>
#include <cmath>

namespace miciri::plugin
{
// 读取端显示用的夹取范围（dBFS）。浮点样本可以超过 0 dBFS，所以上限留一些余量。
inline constexpr double kMeterFloorDbfs = -120.0;
inline constexpr double kMeterCeilDbfs = 24.0;

struct ChannelMeter
{
    std::atomic<double> peak{0.0}; // 自上次读取以来的最大绝对值（线性）
    std::atomic<double> meanSquare{0.0}; // 最近一块的均方（线性）
    std::atomic<bool> clipLatch{false}; // 一旦出现削波就保持，直到用户清除

    // 音频线程：把一块样本并入通道。
    template<class Sample>
    void accumulate(const Sample* data, int numSamples, double clipThreshold) noexcept
    {
        double blockPeak = 0.0;
        double sumSquares = 0.0;
        bool clipped = false;
        for (int i = 0; i < numSamples; ++i)
        {
            const double x = static_cast<double>(data[i]);
            const double a = std::fabs(x);
            if (a > blockPeak)
                blockPeak = a;
            if (a >= clipThreshold)
                clipped = true;
            if (std::isfinite(x))
                sumSquares += x * x;
        }
        double current = peak.load(std::memory_order_relaxed);
        while (blockPeak > current && !peak.compare_exchange_weak(current, blockPeak, std::memory_order_relaxed))
        {
        }
        meanSquare.store(numSamples > 0 ? sumSquares / static_cast<double>(numSamples) : 0.0,
                         std::memory_order_relaxed);
        if (clipped)
            clipLatch.store(true, std::memory_order_relaxed);
    }
};

// 线性幅度转 dBFS，夹到显示范围；非正数和 NaN 给下限。
inline double toDbfs(double linear) noexcept
{
    if (!(linear > 0.0))
        return kMeterFloorDbfs;
    const double db = 20.0 * std::log10(linear);
    if (!(db > kMeterFloorDbfs))
        return kMeterFloorDbfs;
    return db < kMeterCeilDbfs ? db : kMeterCeilDbfs;
}

struct MeterReadout
{
    double levelDbfs[2] = {kMeterFloorDbfs, kMeterFloorDbfs};
    double peakDbfs[2] = {kMeterFloorDbfs, kMeterFloorDbfs};
    bool clipLatch[2] = {false, false};
};
} // namespace miciri::plugin
