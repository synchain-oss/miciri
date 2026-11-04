// SPDX-License-Identifier: GPL-3.0-or-later
//
// 宿主探针：只在测试构建（CMake 选项 MICIRI_HOST_PROBE=ON）里接进插件，发布版不含任何一行。
// 做宿主测试的人读不到 WebView，也没有参数可读，所以由插件自己把计数写成一个 JSON 文件，外部脚本去读。
//
// 本文件与 HostProbe.cpp 不依赖 JUCE 与宿主：计数、全 0 判断、JSON 序列化与原子写文件都能单独编进单元测试，
// 这样默认（OFF）的构建也能防止它们烂掉。
// 接进插件的那几行在 MiciriProcessor 与 MiciriEditor 里，用 MICIRI_HOST_PROBE 包着。
//
// 线程约定：
//   - 音频线程只调 Counters::noteBlock 与 isAllZero：relaxed 原子加一、读块内容，不分配、不加锁、不做 I/O。
//   - 其余的事（prepare、编辑器开关、写文件）都在消息线程或宿主的准备线程。
//     写文件用一把进程内的锁串行化，音频线程永远碰不到它。
#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace miciri::probe
{
// JSON 的 schema 版本；字段改名或语义变化时才升。
inline constexpr int kSchemaVersion = 1;

// 消息线程上的写文件节拍。
inline constexpr int kWriteIntervalMs = 250;

// 一次读出的全部计数。blocksTotal 等派生量由四个基础计数算出，同一份快照里恒满足
// blocksTotal == blocksNonRealtime + blocksRealtime == blocksAllZero + blocksNonZero。
struct Snapshot
{
    double sampleRate = 0.0;
    int maxBlockSize = 0;
    int numInputChannels = 0;
    int numOutputChannels = 0;
    std::uint64_t prepareCount = 0;
    std::uint64_t blocksTotal = 0;
    std::uint64_t blocksNonRealtime = 0;
    std::uint64_t blocksRealtime = 0;
    std::uint64_t blocksAllZero = 0;
    std::uint64_t blocksNonZero = 0;
    std::uint64_t editorOpens = 0;
    std::uint64_t editorCloses = 0;
    std::uint64_t analysisCount = 0;
    bool lastIsNonRealtime = false;
};

namespace detail
{
template<class Sample>
struct SampleBits;

template<>
struct SampleBits<float>
{
    using Type = std::uint32_t;
    static constexpr Type kMagnitude = 0x7fffffffu; // 去掉符号位
};

template<>
struct SampleBits<double>
{
    using Type = std::uint64_t;
    static constexpr Type kMagnitude = 0x7fffffffffffffffull;
};
} // namespace detail

// 整块是不是全 0：+0 与 -0 都算 0，NaN、Inf、非规格化数都算有内容。
// 按比特判断而不是和 0 比大小：音频线程上开着“非规格化数清零”，浮点比较会把非规格化数当成 0。
// 没有样本（0 个声道或 0 个样本）时为真。不分配、不加锁。
template<class Sample>
bool isAllZero(const Sample* const* channels, int numChannels, int numSamples) noexcept
{
    using Bits = typename detail::SampleBits<Sample>::Type;
    static_assert(sizeof(Sample) == sizeof(Bits), "样本与同宽整数必须等宽");
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const Sample* data = channels[ch];
        if (data == nullptr)
            continue;
        Bits acc = 0;
        for (int i = 0; i < numSamples; ++i)
        {
            Bits bits;
            std::memcpy(&bits, data + i, sizeof bits);
            acc |= bits & detail::SampleBits<Sample>::kMagnitude;
        }
        if (acc != 0)
            return false;
    }
    return true;
}

// 计数器：全是 relaxed 原子。每个音频块只做一次 fetch_add（按“离线与否 x 是否全 0”落进四个格子之一），
// 所以读出时各个派生量互相一致，不会出现“总数对不上”。
class Counters
{
public:
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free, "块计数必须无锁");
    static_assert(std::atomic<double>::is_always_lock_free, "采样率必须无锁");
    static_assert(std::atomic<int>::is_always_lock_free, "整数计数必须无锁");
    static_assert(std::atomic<bool>::is_always_lock_free, "标志必须无锁");

    // 音频线程。
    void noteBlock(bool nonRealtime, bool allZero) noexcept
    {
        blocks_[cell(nonRealtime, allZero)].fetch_add(1, std::memory_order_relaxed);
        lastNonRealtime_.store(nonRealtime, std::memory_order_relaxed);
    }

    // 以下在消息线程或宿主的准备线程。
    void notePrepare(double sampleRate, int maxBlockSize, int numInputChannels, int numOutputChannels) noexcept
    {
        sampleRate_.store(sampleRate, std::memory_order_relaxed);
        maxBlockSize_.store(maxBlockSize, std::memory_order_relaxed);
        numInputChannels_.store(numInputChannels, std::memory_order_relaxed);
        numOutputChannels_.store(numOutputChannels, std::memory_order_relaxed);
        prepareCount_.fetch_add(1, std::memory_order_relaxed);
    }
    void noteEditorOpened() noexcept { editorOpens_.fetch_add(1, std::memory_order_relaxed); }
    void noteEditorClosed() noexcept { editorCloses_.fetch_add(1, std::memory_order_relaxed); }
    // 分析线程每做一次分析调一次；离线渲染时它必须保持为 0。
    void noteAnalysis() noexcept { analysisCount_.fetch_add(1, std::memory_order_relaxed); }

    Snapshot snapshot() const noexcept
    {
        std::uint64_t cells[kCells];
        for (int i = 0; i < kCells; ++i)
            cells[i] = blocks_[i].load(std::memory_order_relaxed);
        Snapshot s;
        s.sampleRate = sampleRate_.load(std::memory_order_relaxed);
        s.maxBlockSize = maxBlockSize_.load(std::memory_order_relaxed);
        s.numInputChannels = numInputChannels_.load(std::memory_order_relaxed);
        s.numOutputChannels = numOutputChannels_.load(std::memory_order_relaxed);
        s.prepareCount = prepareCount_.load(std::memory_order_relaxed);
        s.blocksNonRealtime = cells[cell(true, false)] + cells[cell(true, true)];
        s.blocksRealtime = cells[cell(false, false)] + cells[cell(false, true)];
        s.blocksAllZero = cells[cell(false, true)] + cells[cell(true, true)];
        s.blocksNonZero = cells[cell(false, false)] + cells[cell(true, false)];
        s.blocksTotal = s.blocksNonRealtime + s.blocksRealtime;
        s.editorOpens = editorOpens_.load(std::memory_order_relaxed);
        s.editorCloses = editorCloses_.load(std::memory_order_relaxed);
        s.analysisCount = analysisCount_.load(std::memory_order_relaxed);
        s.lastIsNonRealtime = lastNonRealtime_.load(std::memory_order_relaxed);
        return s;
    }

private:
    static constexpr int kCells = 4;
    static constexpr int cell(bool nonRealtime, bool allZero) noexcept
    {
        return (nonRealtime ? 2 : 0) + (allZero ? 1 : 0);
    }

    std::atomic<std::uint64_t> blocks_[kCells]{};
    std::atomic<std::uint64_t> prepareCount_{0};
    std::atomic<std::uint64_t> editorOpens_{0};
    std::atomic<std::uint64_t> editorCloses_{0};
    std::atomic<std::uint64_t> analysisCount_{0};
    std::atomic<double> sampleRate_{0.0};
    std::atomic<int> maxBlockSize_{0};
    std::atomic<int> numInputChannels_{0};
    std::atomic<int> numOutputChannels_{0};
    std::atomic<bool> lastNonRealtime_{false};
};

// 进程内唯一的一份计数：同一进程里的所有插件实例合计，写进同一个文件。
// 常量初始化，没有运行时的构造；处理器构造时会先碰它一次，音频线程不会是第一个调用者。
inline Counters& counters() noexcept
{
    static Counters instance;
    return instance;
}

// 序列化成一个 JSON 对象（schema 见 docs/HOST_PROBE.md）。数字按 C 区域格式化，宿主改了本进程的区域设置也不影响。
std::string toJson(const Snapshot& snapshot, long long pid);

// 本进程的 pid。
long long currentPid() noexcept;

// 把 content 写到 target：先写同目录的临时文件，再原子改名覆盖。读的一方永远看不到半截文件。
// 任何失败（目录不存在、没权限、被读者占着）都只返回 false，不抛异常、不留临时文件。
bool writeFileAtomic(const std::filesystem::path& target, const std::string& content) noexcept;

// 把计数写进 JSON 文件。tick 给定时器用（内容没变就不写），flush 给 prepare、release 和析构用（无条件写一次）。
// 写失败不记录为“已写”，下一次 tick 会重试。
class Reporter
{
public:
    Reporter(std::filesystem::path target, const Counters& counters);

    // 环境变量 MICIRI_HOST_PROBE_JSON 里的路径；没设或为空返回空。
    static std::optional<std::filesystem::path> pathFromEnvironment();
    // 按环境变量建一个；没设就返回空指针，什么都不创建、什么都不写。
    static std::unique_ptr<Reporter> fromEnvironment(const Counters& counters) noexcept;

    bool tick() noexcept;
    bool flush() noexcept;

    const std::filesystem::path& target() const noexcept { return target_; }
    // 成功写文件的次数（给单元测试看）。
    std::uint64_t writesDone() const noexcept { return writes_.load(std::memory_order_relaxed); }

private:
    bool write(bool onlyIfChanged) noexcept;

    std::filesystem::path target_;
    const Counters& counters_;
    std::string last_;
    std::atomic<std::uint64_t> writes_{0};
};
} // namespace miciri::probe

// 分析钩子：后续的分析代码每做一次分析调一次。发布构建里编译成空函数，不引用计数器。
namespace miciri::probe
{
#ifdef MICIRI_HOST_PROBE
inline void noteAnalysis() noexcept
{
    counters().noteAnalysis();
}
#else
inline void noteAnalysis() noexcept {}
#endif
} // namespace miciri::probe
