// SPDX-License-Identifier: GPL-3.0-or-later
//
// 自动操作脚本的解析与调度（只在测试构建里编进插件）。只依赖 juce_core。
#include "HostProbeScript.h"

#include <array>
#include <cmath>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

#include "BridgeApi.h"

namespace miciri::probe
{
namespace
{
constexpr std::array<std::string_view, 5> kOpNames{"setMicType", "startStep", "cancelStep", "reportChange",
                                                   "dismissChannelPrompt"};

constexpr bool isBridgeFunction(std::string_view name)
{
    for (const auto& f : bridge::kFunctions)
        if (f == name)
            return true;
    return false;
}

constexpr bool allBridgeFunctions()
{
    for (const auto& n : kOpNames)
        if (!isBridgeFunction(n))
            return false;
    return true;
}
static_assert(allBridgeFunctions(), "脚本只认桥上已有的函数（BridgeApi.h 的 kFunctions）");

std::optional<Op> opFromName(const juce::String& name)
{
    for (std::size_t i = 0; i < kOpNames.size(); ++i)
        if (name == juce::String(kOpNames[i].data(), kOpNames[i].size()))
            return static_cast<Op>(i);
    return std::nullopt;
}

Script failed(const juce::String& why)
{
    Script s;
    s.error = why.toStdString();
    return s;
}

bool isNumber(const juce::var& v)
{
    return v.isInt() || v.isInt64() || v.isDouble();
}

juce::String where(int i)
{
    return "actions[" + juce::String(i) + "]";
}
} // namespace

std::string_view opName(Op op) noexcept
{
    return kOpNames[static_cast<std::size_t>(op)];
}

bool opTakesArg(Op op) noexcept
{
    return op == Op::SetMicType || op == Op::StartStep || op == Op::ReportChange;
}

Script parseScript(const std::string& text)
{
    juce::var root;
    const auto parsed = juce::JSON::parse(juce::String::fromUTF8(text.data(), static_cast<int>(text.size())), root);
    if (parsed.failed())
        return failed("not valid JSON: " + parsed.getErrorMessage());
    if (root.getDynamicObject() == nullptr) // isObject() 对数组也为真，只认真正的对象
        return failed("the script must be a JSON object");
    const auto schema = root.getProperty("schema", {});
    if (!(schema.isInt() || schema.isInt64()) || static_cast<juce::int64>(schema) != kScriptSchema)
        return failed("schema must be " + juce::String(kScriptSchema));
    const auto* list = root.getProperty("actions", {}).getArray();
    if (list == nullptr)
        return failed("actions must be an array");
    if (static_cast<std::size_t>(list->size()) > kScriptMaxActions)
        return failed("too many actions (at most " + juce::String(static_cast<int>(kScriptMaxActions)) + ")");

    Script script;
    double lastAt = 0.0;
    for (int i = 0; i < list->size(); ++i)
    {
        const auto& item = list->getReference(i);
        const auto* obj = item.getDynamicObject();
        if (obj == nullptr)
            return failed(where(i) + " must be an object");
        const auto at = item.getProperty("at", {});
        if (!isNumber(at))
            return failed(where(i) + ".at must be a number of seconds");
        const double atSec = static_cast<double>(at);
        if (!std::isfinite(atSec) || atSec < 0.0)
            return failed(where(i) + ".at must be finite and not negative");
        if (atSec < lastAt)
            return failed(where(i) + ".at is earlier than the action before it");
        const auto name = item.getProperty("do", {});
        if (!name.isString())
            return failed(where(i) + ".do must be a string");
        const auto op = opFromName(name.toString());
        if (!op)
            return failed(where(i) + ": unknown action \"" + name.toString() + "\"");
        const bool hasArg = obj->hasProperty("arg");
        const auto arg = item.getProperty("arg", {});
        if (opTakesArg(*op) && (!hasArg || !arg.isString()))
            return failed(where(i) + ": " + name.toString() + " needs a string arg");
        if (!opTakesArg(*op) && hasArg)
            return failed(where(i) + ": " + name.toString() + " takes no arg");
        script.actions.push_back({atSec, *op, hasArg ? arg.toString().toStdString() : std::string()});
        lastAt = atSec;
    }
    return script;
}

Script loadScript(const std::filesystem::path& file) noexcept
{
    try
    {
        std::error_code ec;
        const auto size = std::filesystem::file_size(file, ec);
        if (ec)
            return failed("cannot read the script file");
        if (size > kScriptMaxBytes)
            return failed("the script file is larger than " + juce::String(static_cast<juce::int64>(kScriptMaxBytes)) +
                          " bytes");
        std::ifstream in(file, std::ios::binary);
        if (!in)
            return failed("cannot open the script file");
        std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        return parseScript(text);
    }
    catch (...)
    {
        Script s;
        s.error = "cannot read the script file";
        return s;
    }
}

void SampleClock::prepare(std::uint64_t samplesSoFar, double sampleRate) noexcept
{
    if (samplesSoFar >= foldedSamples_ && rate_ > 0.0)
        foldedSec_ += static_cast<double>(samplesSoFar - foldedSamples_) / rate_;
    foldedSamples_ = samplesSoFar;
    rate_ = sampleRate > 0.0 && std::isfinite(sampleRate) ? sampleRate : 0.0;
}

std::optional<double> SampleClock::seconds(std::uint64_t samplesSoFar) const noexcept
{
    if (samplesSoFar == 0 || rate_ <= 0.0 || samplesSoFar < foldedSamples_)
        return std::nullopt;
    return foldedSec_ + static_cast<double>(samplesSoFar - foldedSamples_) / rate_;
}

Scheduler::Scheduler(std::vector<Action> actions) : actions_(std::move(actions))
{
    done_.reserve(actions_.size());
}

std::size_t Scheduler::advance(std::optional<double> processedSec, const Execute& execute)
{
    if (!processedSec)
        return 0;
    const double now = *processedSec;
    std::size_t executed = 0;
    while (!finished())
    {
        const std::size_t i = done_.size();
        const Action& a = actions_[i];
        if (now < a.atSec)
            break;
        // 记录先放在局部：执行当中（执行者可能写输出文件）waiting() 只报真正回过 busy 的那个，不报正在执行的这个
        ActionRecord r;
        if (waiting_)
            r = *waiting_;
        else
        {
            r.index = i;
            r.action = a;
            r.firstTriedSec = now;
        }
        Reply reply = execute(a);
        if (!reply.ok && reply.reason == "busy")
        {
            ++r.busyReplies; // 上一步还在跑或还在收尾：下一拍再试，后面的动作都等它
            waiting_ = std::move(r);
            break;
        }
        waiting_.reset();
        r.reply = std::move(reply);
        r.doneSec = now;
        done_.push_back(std::move(r));
        ++executed;
    }
    return executed;
}

juce::var toVar(const Reply& reply)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("ok", reply.ok);
    if (!reply.ok)
        o->setProperty("reason", juce::String(reply.reason));
    return juce::var(o);
}

juce::var toVar(const ActionRecord& r)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("index", static_cast<int>(r.index));
    o->setProperty("at", r.action.atSec);
    const auto name = opName(r.action.op);
    o->setProperty("do", juce::String(name.data(), name.size()));
    if (opTakesArg(r.action.op))
        o->setProperty("arg", juce::String(r.action.arg));
    o->setProperty("firstTriedSec", r.firstTriedSec);
    o->setProperty("doneSec", r.doneSec);
    o->setProperty("busyReplies", r.busyReplies);
    o->setProperty("reply", toVar(r.reply));
    return juce::var(o);
}

juce::var actionsVar(const Scheduler& scheduler)
{
    juce::Array<juce::var> list;
    for (const auto& r : scheduler.done())
        list.add(toVar(r));
    return juce::var(list);
}
} // namespace miciri::probe
