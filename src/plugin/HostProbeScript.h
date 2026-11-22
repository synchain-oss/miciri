// SPDX-License-Identifier: GPL-3.0-or-later
//
// 宿主探针的自动操作脚本：只在测试构建（MICIRI_HOST_PROBE=ON）里接进插件，发布版不含任何一行。
// 宿主测试工具读不到也点不了 WebView，所以由脚本按“插件已处理的样本秒”依次调用桥上已有的几个函数，
// 探针把每一步的结果写成文件（见 HostProbeRunner.h 与 docs/HOST_PROBE.md）。
//
// 本文件只做两件事：解析脚本（用 juce_core 的 JSON），以及按样本秒顺序调度动作。动作怎么执行由调用方给，
// 所以单元测试不需要处理器与宿主，默认（OFF）的构建也照常编译、运行。
// 线程：全部在消息线程（探针的定时器）上，音频线程不碰。
#pragma once

#include <juce_core/juce_core.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace miciri::probe
{
inline constexpr int kScriptSchema = 1;
// 脚本文件与动作数的上限：只为挡住拿错文件，正常的测试脚本远小于此
inline constexpr std::uintmax_t kScriptMaxBytes = 1u << 20;
inline constexpr std::size_t kScriptMaxActions = 10000;

// 脚本认的动作，名字与桥函数相同（HostProbeScript.cpp 用 static_assert 钉住它们都在桥的函数表里）
enum class Op : std::uint8_t
{
    SetMicType,
    StartStep,
    CancelStep,
    ReportChange,
    DismissChannelPrompt,
};
std::string_view opName(Op op) noexcept;
// 这个动作要不要参数（桥函数的那一个字符串参数）
bool opTakesArg(Op op) noexcept;

struct Action
{
    double atSec = 0.0; // 插件已处理的样本秒，从第一个有样本的块起算
    Op op = Op::CancelStep;
    std::string arg; // 不带参数的动作为空
};

// 回执，与桥的 {ok: true} / {ok: false, reason} 相同
struct Reply
{
    bool ok = true;
    std::string reason;
};

struct Script
{
    std::vector<Action> actions;
    std::string error; // 非空：脚本不可用，一个动作也不执行（actions 为空）
    bool ok() const noexcept { return error.empty(); }
};

// 解析脚本文本（UTF-8）。任何一处不合规就整份作废：error 写明第一处原因，actions 为空。
Script parseScript(const std::string& text);
// 读文件再解析；读不到、超过上限也记进 error。不抛异常。
Script loadScript(const std::filesystem::path& file) noexcept;

// 插件已处理的样本秒：还没收到有样本的块、或还不知道采样率时没有值（时钟还没开始，任何动作都不到期）。
std::optional<double> processedSeconds(std::uint64_t samples, double sampleRate) noexcept;

// 一个动作的执行记录
struct ActionRecord
{
    std::size_t index = 0; // 在脚本里的下标
    Action action;
    double firstTriedSec = 0.0; // 第一次尝试时已处理的样本秒
    double doneSec = 0.0; // 拿到非 busy 回执、算执行完时已处理的样本秒
    int busyReplies = 0; // 之前回了几次 busy
    Reply reply;
};

// 顺序调度：动作按脚本顺序执行，到了 atSec 才执行；回执是 busy（上一步还在跑或还在收尾）的那个留到下一拍再试，
// 后面的动作都等它。其余回执原样记下，接着往下走。
class Scheduler
{
public:
    using Execute = std::function<Reply(const Action&)>;

    Scheduler() = default;
    explicit Scheduler(std::vector<Action> actions);

    // 每一拍调一次。processedSec 没有值时什么都不做。返回这一拍执行完的动作数。
    std::size_t advance(std::optional<double> processedSec, const Execute& execute);

    const std::vector<ActionRecord>& done() const noexcept { return done_; }
    // 正在等的那个动作（到期了但回了 busy）；没有就为空
    const std::optional<ActionRecord>& waiting() const noexcept { return waiting_; }
    // 还没执行完的动作数（含正在等的那个）
    std::size_t remaining() const noexcept { return actions_.size() - done_.size(); }
    bool finished() const noexcept { return remaining() == 0; }

private:
    std::vector<Action> actions_;
    std::vector<ActionRecord> done_;
    std::optional<ActionRecord> waiting_;
};

// 写进输出文件的样子（字段见 docs/HOST_PROBE.md）
juce::var toVar(const Reply& reply);
juce::var toVar(const ActionRecord& record);
juce::var actionsVar(const Scheduler& scheduler);
} // namespace miciri::probe
