// SPDX-License-Identifier: GPL-3.0-or-later
//
// 音频线程到工作线程的样本通道：单生产者单消费者的环，样本按帧交错存成 float。
// 音频线程在 process() 里只做这些：看一眼是否在录（不录时就是一次原子读，到此为止）、
// 把这一块写进预先分配好的环、记下推了多少帧。不分配、不加锁、不读墙钟、不抛异常。
// 环装不下时整块丢弃并置溢出标志，不写半块；宿主报离线渲染、声道数和开录时不同，都只置标志不写入。
// 这些标志由工作线程读，据此把当前步作废。
//
// 停录与重新分配：disarm() 先撤掉“在录”，再等音频线程离开 process()（最多一块的时间），
// 之后消息线程才可以重新分配环。两边用顺序一致的原子量握手：音频线程先声明“在里面”再复查“在录”，
// 消息线程先撤“在录”再看“在里面”，两者不会同时错过对方。
#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace miciri::session
{
class AudioTap
{
public:
    static_assert(std::atomic<std::size_t>::is_always_lock_free, "环的读写位置必须无锁");
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free, "帧计数必须无锁");
    static_assert(std::atomic<bool>::is_always_lock_free, "标志必须无锁");

    // ---- 消息线程（工作线程不在跑的时候）----
    // 准备能装 seconds 秒、channels 路的环，清掉所有标志与计数，然后开始收。只能在 disarm 之后调用；
    // 已有的环够大就沿用，不重新分配。
    void arm(double sampleRate, int channels, double seconds);
    // 停止收样本并等音频线程离开 process()。任何线程都可以调，可以重复调。
    void disarm() noexcept;
    bool armed() const noexcept { return armed_.load(std::memory_order_acquire); }

    // ---- 音频线程 ----
    template<class Sample>
    void process(const Sample* const* data, int numChannels, int frames, bool nonRealtime) noexcept
    {
        if (frames <= 0 || !armed_.load(std::memory_order_acquire))
            return;
        inside_.store(true, std::memory_order_seq_cst);
        if (armed_.load(std::memory_order_seq_cst))
            write(data, numChannels, frames, nonRealtime);
        inside_.store(false, std::memory_order_seq_cst);
    }

    // ---- 工作线程 ----
    // 取出最多 maxFrames 帧（交错，每帧 channels() 个 float），返回取到的帧数。
    std::size_t pop(float* out, std::size_t maxFrames) noexcept;
    int channels() const noexcept { return channels_; }
    bool overflowed() const noexcept { return overflow_.load(std::memory_order_acquire); }
    bool offline() const noexcept { return offline_.load(std::memory_order_acquire); }
    bool layoutMismatch() const noexcept { return mismatch_.load(std::memory_order_acquire); }
    // 开录以来宿主送来的帧数，含因环满丢掉的（不含离线与声道不符的块）。工作线程拿它和墙钟比，判断是不是快于实时
    std::uint64_t framesArrived() const noexcept { return arrived_.load(std::memory_order_acquire); }

private:
    template<class Sample>
    void write(const Sample* const* data, int numChannels, int frames, bool nonRealtime) noexcept
    {
        if (nonRealtime)
        {
            offline_.store(true, std::memory_order_release);
            return;
        }
        if (numChannels != channels_ || data == nullptr)
        {
            mismatch_.store(true, std::memory_order_release);
            return;
        }
        arrived_.fetch_add(static_cast<std::uint64_t>(frames), std::memory_order_acq_rel);
        const std::size_t need = static_cast<std::size_t>(frames) * static_cast<std::size_t>(channels_);
        const std::size_t w = write_.load(std::memory_order_relaxed);
        const std::size_t r = read_.load(std::memory_order_acquire);
        if (ring_.size() - (w - r) < need)
        {
            overflow_.store(true, std::memory_order_release);
            return;
        }
        std::size_t at = w;
        for (int i = 0; i < frames; ++i)
            for (int c = 0; c < channels_; ++c)
                ring_[at++ & mask_] = data[c] != nullptr ? static_cast<float>(data[c][i]) : 0.0f;
        write_.store(w + need, std::memory_order_release);
    }

    std::vector<float> ring_; // 长度是 2 的幂
    std::size_t mask_ = 0;
    int channels_ = 1;
    std::atomic<std::size_t> write_{0}; // 只增不减，取模用 mask_
    std::atomic<std::size_t> read_{0};
    std::atomic<std::uint64_t> arrived_{0};
    std::atomic<bool> armed_{false};
    std::atomic<bool> inside_{false};
    std::atomic<bool> overflow_{false};
    std::atomic<bool> offline_{false};
    std::atomic<bool> mismatch_{false};
};
} // namespace miciri::session
