// SPDX-License-Identifier: GPL-3.0-or-later
//
// 宿主探针在消息线程上的那一半：只在测试构建（MICIRI_HOST_PROBE=ON）里编进插件，发布版不含这个文件。
//   - 计数 JSON（见 HostProbe.h）：约每 250 ms 写一次（内容没变不写），prepare、release、析构各再写一次。
//   - 自动操作（见 HostProbeScript.h）：按插件已处理的样本秒，依次调用桥上已有的几个函数，
//     直接作用在处理器持有的 Session 上，不需要编辑器。
//   - 输出目录：每一步结束写一份 step 文件（快照、这一步的证据、动作日志），release 与析构写 final 文件。
// 三样都没配置时不创建，处理器里什么都不发生。环境变量与文件格式见 docs/HOST_PROBE.md。
//
// 线程：音频线程只给处理器的样本计数做一次 relaxed 加法，不碰这里的任何东西。定时器（消息线程）、
// prepareToPlay 与 releaseResources（宿主的准备线程）、析构之间用一把锁串行化。
#pragma once

#include <juce_events/juce_events.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "HostProbe.h"
#include "HostProbeScript.h"
#include "session/Session.h"

namespace miciri
{
class MiciriProcessor;

class ProbeRunner final : private juce::Timer
{
public:
    // 有脚本或输出目录时的节拍：动作按这个粒度到期，计数 JSON 仍按 probe::kWriteIntervalMs 写。
    static constexpr int kActionIntervalMs = 50;

    // samples 是处理器的每实例样本计数（音频线程累加）。三样都没配置时返回空。
    static std::unique_ptr<ProbeRunner> create(MiciriProcessor& processor, const std::atomic<std::uint64_t>& samples);
    ~ProbeRunner() override;

    void prepared() noexcept; // prepareToPlay 之后
    void released() noexcept; // releaseResources

private:
    struct Watch
    {
        core::StepId id = core::StepId::Connect;
        std::uint64_t runsAtStart = 0;
        bool cancelIssued = false;
    };

    ProbeRunner(MiciriProcessor& processor, const std::atomic<std::uint64_t>& samples,
                std::unique_ptr<probe::Reporter> reporter, std::optional<std::filesystem::path> scriptPath,
                std::optional<std::filesystem::path> outDir);

    void timerCallback() override;

    // 以下都在持有 mutex_ 时调
    std::optional<double> processedSec() const;
    probe::Reply execute(const probe::Action& action);
    void pollStep();
    void finishStep(const session::SessionView& view);
    juce::var snapshot() const;
    juce::var evidenceReply(core::StepId id) const;
    juce::var scriptVar() const;
    juce::var header(const char* kind) const;
    void finalReport(const char* reason);
    void enqueue(const std::string& fileName, const juce::var& content);
    void flushPending();

    MiciriProcessor& processor_;
    const std::atomic<std::uint64_t>& samples_;
    std::unique_ptr<probe::Reporter> reporter_; // 计数 JSON；没配置时为空
    std::optional<std::filesystem::path> outDir_;
    bool scripted_ = false; // 给了脚本（不论能不能用）
    std::string scriptError_;
    std::size_t scriptActions_ = 0;
    probe::Scheduler scheduler_;
    std::optional<Watch> watch_; // 正在进行、结束时要写 step 文件的那一步
    int stepFiles_ = 0; // step 文件的序号
    std::vector<std::pair<std::filesystem::path, std::string>> pending_; // 写失败、下一拍再试的文件
    juce::String hostName_;
    int intervalMs_ = probe::kWriteIntervalMs;
    int msSinceReport_ = 0;
    std::mutex mutex_;
};
} // namespace miciri
