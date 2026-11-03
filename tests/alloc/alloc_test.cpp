// SPDX-License-Identifier: GPL-3.0-or-later
//
// 分配计数器：替换全局 operator new/delete，在“上膛”的作用域里统计本线程的分配次数。
// 两条用例：
//   - 反例：作用域里故意分配一次，计数必须大于零，证明钩子真的接上了、真的会变红；
//   - 正例：processBlock 的各条路径（单声道、立体声、单精度、双精度、旁路、NaN 与 Inf 输入）一次分配都不许有。
// 断言放在作用域外：Catch2 的断言自己会分配，不能在上膛时执行。
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <new>

#include "plugin/MiciriProcessor.h"

namespace
{
thread_local bool g_armed = false;
std::atomic<long> g_allocations{0};

void note() noexcept
{
    if (g_armed)
        g_allocations.fetch_add(1, std::memory_order_relaxed);
}

void* allocate(std::size_t size)
{
    note();
    void* p = std::malloc(size != 0 ? size : 1);
    if (p == nullptr)
        throw std::bad_alloc();
    return p;
}

void* allocateAligned(std::size_t size, std::size_t alignment)
{
    note();
    const std::size_t n = size != 0 ? size : 1;
#if defined(_MSC_VER)
    void* p = _aligned_malloc(n, alignment);
#else
    void* p = nullptr;
    if (posix_memalign(&p, alignment < sizeof(void*) ? sizeof(void*) : alignment, n) != 0)
        p = nullptr;
#endif
    if (p == nullptr)
        throw std::bad_alloc();
    return p;
}

void releaseAligned(void* p) noexcept
{
#if defined(_MSC_VER)
    _aligned_free(p);
#else
    std::free(p);
#endif
}

class ArmedScope
{
public:
    ArmedScope()
    {
        g_allocations.store(0);
        g_armed = true;
    }
    ~ArmedScope() { g_armed = false; }
    ArmedScope(const ArmedScope&) = delete;
    ArmedScope& operator=(const ArmedScope&) = delete;
    long count() const { return g_allocations.load(); }
};
} // namespace

void* operator new(std::size_t size)
{
    return allocate(size);
}
void* operator new[](std::size_t size)
{
    return allocate(size);
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    note();
    return std::malloc(size != 0 ? size : 1);
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    note();
    return std::malloc(size != 0 ? size : 1);
}
void* operator new(std::size_t size, std::align_val_t alignment)
{
    return allocateAligned(size, static_cast<std::size_t>(alignment));
}
void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return allocateAligned(size, static_cast<std::size_t>(alignment));
}
void operator delete(void* p) noexcept
{
    std::free(p);
}
void operator delete[](void* p) noexcept
{
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept
{
    std::free(p);
}
void operator delete(void* p, const std::nothrow_t&) noexcept
{
    std::free(p);
}
void operator delete[](void* p, const std::nothrow_t&) noexcept
{
    std::free(p);
}
void operator delete(void* p, std::align_val_t) noexcept
{
    releaseAligned(p);
}
void operator delete[](void* p, std::align_val_t) noexcept
{
    releaseAligned(p);
}
void operator delete(void* p, std::size_t, std::align_val_t) noexcept
{
    releaseAligned(p);
}
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept
{
    releaseAligned(p);
}

namespace
{
void configure(miciri::MiciriProcessor& p, int channels, int blockSize)
{
    const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add(set);
    layout.outputBuses.add(set);
    REQUIRE(p.setBusesLayout(layout));
    p.prepareToPlay(48000.0, blockSize);
}
} // namespace

TEST_CASE("canary: the hook sees an allocation made inside the armed scope", "[alloc]")
{
    long seen = 0;
    {
        ArmedScope scope;
        volatile int* leak = new int[16];
        delete[] leak;
        seen = scope.count();
    }
    CHECK(seen >= 1);
}

TEST_CASE("canary: nothing is counted while the scope is not armed", "[alloc]")
{
    g_allocations.store(0);
    volatile int* p = new int[16];
    delete[] p;
    CHECK(g_allocations.load() == 0);
}

TEST_CASE("processBlock makes no allocation on any path", "[alloc]")
{
    const int channels = GENERATE(1, 2);
    miciri::MiciriProcessor p;
    constexpr int kBlock = 512;
    constexpr int kRounds = 200;
    configure(p, channels, kBlock);

    // 缓冲、MIDI 缓冲都在上膛之前建好。
    juce::AudioBuffer<float> f(channels, kBlock);
    juce::AudioBuffer<double> d(channels, kBlock);
    juce::MidiBuffer midi;
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < kBlock; ++i)
        {
            const double v = std::sin(0.01 * static_cast<double>(i)) * 0.5;
            f.getWritePointer(ch)[i] = static_cast<float>(v);
            d.getWritePointer(ch)[i] = v;
        }
    f.getWritePointer(0)[3] = std::numeric_limits<float>::quiet_NaN();
    f.getWritePointer(0)[4] = std::numeric_limits<float>::infinity();
    d.getWritePointer(0)[3] = std::numeric_limits<double>::quiet_NaN();
    d.getWritePointer(0)[4] = -std::numeric_limits<double>::infinity();

    long allocations = 0;
    {
        ArmedScope scope;
        for (int r = 0; r < kRounds; ++r)
        {
            p.processBlock(f, midi);
            p.processBlock(d, midi);
            p.processBlockBypassed(f, midi);
            p.processBlockBypassed(d, midi);
        }
        allocations = scope.count();
    }
    CHECK(allocations == 0);
    CHECK(p.heartbeat() == static_cast<std::uint64_t>(4 * kRounds));
}
