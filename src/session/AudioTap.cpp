// SPDX-License-Identifier: GPL-3.0-or-later
#include "session/AudioTap.h"

#include <cmath>
#include <thread>

namespace miciri::session
{
namespace
{
std::size_t nextPowerOfTwo(std::size_t n)
{
    std::size_t p = 1;
    while (p < n)
        p <<= 1;
    return p;
}
} // namespace

void AudioTap::arm(double sampleRate, int channels, double seconds)
{
    disarm();
    channels_ = std::max(1, channels);
    const double frames = std::ceil(std::max(0.0, sampleRate) * std::max(0.0, seconds));
    const std::size_t want = nextPowerOfTwo(std::max<std::size_t>(1, static_cast<std::size_t>(frames)) *
                                            static_cast<std::size_t>(channels_));
    if (ring_.size() < want)
        ring_.assign(want, 0.0f);
    mask_ = ring_.size() - 1;
    write_.store(0, std::memory_order_relaxed);
    read_.store(0, std::memory_order_relaxed);
    arrived_.store(0, std::memory_order_relaxed);
    overflow_.store(false, std::memory_order_relaxed);
    offline_.store(false, std::memory_order_relaxed);
    mismatch_.store(false, std::memory_order_relaxed);
    armed_.store(true, std::memory_order_seq_cst); // 发布上面所有的写：音频线程以获取语义读到“在录”后才碰环
}

void AudioTap::disarm() noexcept
{
    armed_.store(false, std::memory_order_seq_cst);
    while (inside_.load(std::memory_order_seq_cst))
        std::this_thread::yield();
}

std::size_t AudioTap::pop(float* out, std::size_t maxFrames) noexcept
{
    const auto ch = static_cast<std::size_t>(channels_);
    const std::size_t r = read_.load(std::memory_order_relaxed);
    const std::size_t w = write_.load(std::memory_order_acquire);
    const std::size_t frames = std::min((w - r) / ch, maxFrames);
    const std::size_t count = frames * ch;
    for (std::size_t i = 0; i < count; ++i)
        out[i] = ring_[(r + i) & mask_];
    read_.store(r + count, std::memory_order_release);
    return frames;
}
} // namespace miciri::session
