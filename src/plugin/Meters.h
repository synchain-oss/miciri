// SPDX-License-Identifier: GPL-3.0-or-later
//
// 音频线程与消息线程之间的电平通道：音频线程只写原子量，消息线程定时读取。
// 音频线程一侧不加锁、不分配、不读墙钟；NaN 与 Inf 输入不会让读数失效：
//   - 峰值是真峰值（样本与 4 倍插值点里较大的那个），只在大于当前值时更新，NaN 的比较为假所以被忽略；
//     Inf 会把峰值顶到无穷，由读取端夹取；插值用的历史里 NaN 与 Inf 当 0；
//   - 均方只累计有限样本，NaN 与 Inf 不进均方；
//   - 削波计数按 |x| >= 阈值判定，Inf 为真、NaN 为假。
#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>

#include "level/Dsp.h"

namespace miciri::plugin
{
// 读取端显示用的夹取范围（dBFS）。浮点样本可以超过 0 dBFS，所以上限留一些余量。
inline constexpr double kMeterFloorDbfs = -120.0;
inline constexpr double kMeterCeilDbfs = 24.0;
// 削波灯在最后一次削波之后亮多久（毫秒）；点灯清除则立即熄。
inline constexpr std::uint32_t kClipLampHoldMs = 5000;

// 音频线程与消息线程共享的原子量必须无锁：个别平台上退化成带锁实现会让音频线程被消息线程阻塞。
static_assert(std::atomic<double>::is_always_lock_free, "电平的原子 double 必须无锁");
static_assert(std::atomic<std::uint64_t>::is_always_lock_free, "样本计数的原子整数必须无锁");
static_assert(std::atomic<std::uint32_t>::is_always_lock_free, "削波计数的原子整数必须无锁");

struct ChannelMeter
{
    std::atomic<double> peak{0.0}; // 自上次读取以来的真峰值（线性）
    std::atomic<double> sumSquares{0.0}; // 自上次读取以来的平方和（线性，只计有限样本）
    std::atomic<std::uint64_t> sampleCount{0}; // 与平方和对应的有限样本数（NaN 与 Inf 不计）
    std::atomic<std::uint32_t> clipBlocks{0}; // 出现过削波的块数，只增不减；灯亮多久由消息线程按墙钟定

    // 只在插值用，音频线程独占；prepare 以外的线程不碰
    core::dsp::StreamingTruePeak truePeak;

    // 不在音频线程调用：采样率或布局变化时重新定插值倍数并清空历史。
    void prepare(int oversampleFactor) noexcept
    {
        truePeak.prepare(oversampleFactor);
        peak.store(0.0, std::memory_order_relaxed);
        sumSquares.store(0.0, std::memory_order_relaxed);
        sampleCount.store(0, std::memory_order_relaxed);
    }

    // 音频线程：把一块样本并入通道。
    template<class Sample>
    void accumulate(const Sample* data, int numSamples, double clipThreshold) noexcept
    {
        double blockPeak = 0.0;
        double blockSquares = 0.0;
        std::uint64_t finiteCount = 0;
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
            {
                blockSquares += x * x;
                ++finiteCount;
            }
        }
        const double interpolated = truePeak.push(data, numSamples);
        if (interpolated > blockPeak)
            blockPeak = interpolated;
        raiseTo(peak, blockPeak);
        addTo(sumSquares, blockSquares);
        sampleCount.fetch_add(finiteCount, std::memory_order_relaxed);
        if (clipped)
            clipBlocks.fetch_add(1, std::memory_order_relaxed);
    }

    // 消息线程：取走自上次读取以来的真峰值与均方（线性），并清零。没有任何有限样本时均方为 0。
    // 平方和与样本数是两次独立的交换，音频线程恰好夹在中间时会有一块的偏差，对显示无碍。
    void take(double& peakOut, double& meanSquareOut) noexcept
    {
        peakOut = peak.exchange(0.0, std::memory_order_relaxed);
        const double squares = sumSquares.exchange(0.0, std::memory_order_relaxed);
        const std::uint64_t count = sampleCount.exchange(0, std::memory_order_relaxed);
        meanSquareOut = count > 0 ? squares / static_cast<double>(count) : 0.0;
    }

private:
    static void raiseTo(std::atomic<double>& target, double value) noexcept
    {
        double current = target.load(std::memory_order_relaxed);
        while (value > current && !target.compare_exchange_weak(current, value, std::memory_order_relaxed))
        {
        }
    }
    static void addTo(std::atomic<double>& target, double value) noexcept
    {
        double current = target.load(std::memory_order_relaxed);
        while (!target.compare_exchange_weak(current, current + value, std::memory_order_relaxed))
        {
        }
    }
};

// 削波灯（消息线程专用，不加锁）：削波块数变了就亮并重新计时，超过 kClipLampHoldMs 没有新的削波就熄；清除立即熄。
// 时间由调用方传入（单调毫秒，允许回绕），所以不依赖墙钟，测试可以直接给时间。
class ClipLamp
{
public:
    bool update(std::uint32_t clipBlocks, std::uint32_t nowMs) noexcept
    {
        if (clipBlocks != seen_)
        {
            seen_ = clipBlocks;
            lastMs_ = nowMs;
            lit_ = true;
        }
        else if (lit_ && nowMs - lastMs_ >= kClipLampHoldMs)
        {
            lit_ = false;
        }
        return lit_;
    }

    // 点灯清除：灯熄，并把此刻的削波块数记为已读，已经发生的削波不会在下次读取时再把灯点亮。
    void clear(std::uint32_t clipBlocks) noexcept
    {
        seen_ = clipBlocks;
        lit_ = false;
    }

private:
    std::uint32_t seen_ = 0;
    std::uint32_t lastMs_ = 0;
    bool lit_ = false;
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

// levelDbfs 是两次读取之间的均方根，peakDbfs 是同一段里的真峰值；clipLatch 沿用桥契约里的字段名，
// 意思是“削波灯亮着”：最后一次削波之后 kClipLampHoldMs 内为真，点灯清除后为假。
struct MeterReadout
{
    double levelDbfs[2] = {kMeterFloorDbfs, kMeterFloorDbfs};
    double peakDbfs[2] = {kMeterFloorDbfs, kMeterFloorDbfs};
    bool clipLatch[2] = {false, false};
};
} // namespace miciri::plugin
