// SPDX-License-Identifier: GPL-3.0-or-later
//
// 宿主探针在消息线程上的那一半（只在测试构建里编进插件）。见 HostProbeRunner.h。
#include "HostProbeRunner.h"

#include <array>
#include <initializer_list>
#include <string_view>

#include "BridgeApi.h"
#include "BridgeSnapshot.h"
#include "MiciriProcessor.h"
#include "session/Evidence.h"

namespace miciri
{
namespace
{
namespace fs = std::filesystem;

// 写失败、排队重试的文件最多留这么多份（输出目录一直写不进时不让内存涨下去）
constexpr std::size_t kPendingMax = 256;

constexpr const char* kBadStep = "bad-step";
constexpr const char* kBadArg = "bad-arg";
constexpr const char* kBusy = "busy";
constexpr const char* kDisabled = "disabled";
constexpr const char* kNoEvidence = "no-evidence";

constexpr bool isReason(std::string_view name)
{
    for (const auto& r : bridge::kReasons)
        if (r == name)
            return true;
    return false;
}
static_assert(isReason(kBadStep) && isReason(kBadArg) && isReason(kBusy) && isReason(kDisabled) &&
                  isReason(kNoEvidence),
              "回执与桥相同：原因必须出现在 BridgeApi.h 的 kReasons 里");

juce::String wire(std::string_view v)
{
    return juce::String(v.data(), v.size());
}

// 参数是某个枚举的线上名字：返回下标，不认识返回 -1（与桥的判断相同）
template<std::size_t N>
int wireIndex(const std::string& arg, const std::array<std::string_view, N>& names)
{
    for (std::size_t i = 0; i < N; ++i)
        if (names[i] == arg)
            return static_cast<int>(i);
    return -1;
}

juce::var makeObject(std::initializer_list<std::pair<const char*, juce::var>> fields)
{
    auto* obj = new juce::DynamicObject();
    for (const auto& f : fields)
        obj->setProperty(f.first, f.second);
    return juce::var(obj);
}

juce::var parseJson(const std::string& text)
{
    return juce::JSON::parse(juce::String::fromUTF8(text.data(), static_cast<int>(text.size())));
}

juce::var orNull(const std::optional<double>& v)
{
    return v ? juce::var(*v) : juce::var();
}

juce::String pidSuffix()
{
    return "-pid" + juce::String(probe::currentPid()) + ".json";
}
} // namespace

std::unique_ptr<ProbeRunner> ProbeRunner::create(MiciriProcessor& processor, const std::atomic<std::uint64_t>& samples)
{
    try
    {
        auto reporter = probe::Reporter::fromEnvironment(probe::counters());
        auto script = probe::scriptPathFromEnvironment();
        auto out = probe::outputDirFromEnvironment();
        if (reporter == nullptr && !script && !out)
            return nullptr;
        return std::unique_ptr<ProbeRunner>(
            new ProbeRunner(processor, samples, std::move(reporter), std::move(script), std::move(out)));
    }
    catch (...)
    {
        return nullptr; // 探针出错不能让插件加载失败
    }
}

ProbeRunner::ProbeRunner(MiciriProcessor& processor, const std::atomic<std::uint64_t>& samples,
                         std::unique_ptr<probe::Reporter> reporter, std::optional<fs::path> scriptPath,
                         std::optional<fs::path> outDir)
    : processor_(processor), samples_(samples), reporter_(std::move(reporter)), outDir_(std::move(outDir)),
      hostName_(juce::PluginHostType().getHostDescription())
{
    if (scriptPath)
    {
        scripted_ = true;
        auto script = probe::loadScript(*scriptPath);
        scriptError_ = script.error; // 不可用的脚本一个动作也不执行，原因写进输出文件
        scriptActions_ = script.actions.size();
        scheduler_ = probe::Scheduler(std::move(script.actions));
    }
    if (scripted_ || outDir_)
        intervalMs_ = kActionIntervalMs;
    startTimer(intervalMs_);
}

ProbeRunner::~ProbeRunner()
{
    stopTimer();
    const std::lock_guard<std::mutex> lock(mutex_);
    if (reporter_ != nullptr)
        reporter_->flush();
    finalReport("destroy");
}

void ProbeRunner::prepared() noexcept
{
    const std::lock_guard<std::mutex> lock(mutex_);
    if (reporter_ != nullptr)
        reporter_->flush();
}

void ProbeRunner::released() noexcept
{
    const std::lock_guard<std::mutex> lock(mutex_);
    if (reporter_ != nullptr)
        reporter_->flush();
    finalReport("release");
}

void ProbeRunner::timerCallback()
{
    const std::lock_guard<std::mutex> lock(mutex_);
    msSinceReport_ += intervalMs_;
    if (reporter_ != nullptr && msSinceReport_ >= probe::kWriteIntervalMs)
    {
        msSinceReport_ = 0;
        reporter_->tick();
    }
    if (!scripted_ && !outDir_)
        return;
    try
    {
        pollStep(); // 先收已经结束的那一步，再执行到期的动作
        scheduler_.advance(processedSec(), [this](const probe::Action& a) { return execute(a); });
        flushPending();
    }
    catch (...)
    {
        // 探针出错不影响插件：下一拍照常再来
    }
}

std::optional<double> ProbeRunner::processedSec() const
{
    return probe::processedSeconds(samples_.load(std::memory_order_relaxed), processor_.deviceInfo().sampleRate);
}

// 回执与编辑器里的桥处理相同（见 MiciriEditor.cpp 的 handleBridgeCall）；只是不推快照
probe::Reply ProbeRunner::execute(const probe::Action& a)
{
    auto& s = processor_.session();
    switch (a.op)
    {
    case probe::Op::SetMicType:
        if (!processor_.setMicType(wireIndex(a.arg, core::kMicTypeNames)))
            return {false, kBadArg};
        return {};
    case probe::Op::StartStep: {
        const int index = wireIndex(a.arg, core::kStepIdNames);
        if (index < 0)
            return {false, kBadStep};
        pollStep(); // 上一步若刚结束，先把它的文件记下，免得被新的一步顶掉
        const auto id = static_cast<core::StepId>(index);
        const auto runsBefore = s.view().runs[static_cast<std::size_t>(index)];
        switch (s.startStep(id, processor_.offlineRendering()))
        {
        case session::Session::Start::Busy:
            return {false, kBusy};
        case session::Session::Start::Disabled:
        case session::Session::Start::Failed:
            return {false, kDisabled};
        case session::Session::Start::Ok:
            break;
        }
        if (watch_) // 能开始新的一步，说明上一步已经收尾，只是还没轮到 pollStep 看见
            finishStep(s.view());
        watch_ = Watch{id, runsBefore, false};
        return {};
    }
    case probe::Op::CancelStep:
        if (watch_)
            watch_->cancelIssued = true;
        s.cancelStep();
        return {};
    case probe::Op::ReportChange: {
        const int index = wireIndex(a.arg, core::kChangeKindNames);
        if (index < 0)
            return {false, kBadArg};
        s.reportChange(static_cast<core::ChangeKind>(index));
        return {};
    }
    case probe::Op::DismissChannelPrompt:
        s.dismissChannelPrompt();
        return {};
    }
    return {false, kBadArg};
}

// 看一眼 Session：盯着的那一步不在进行了就写它的 step
// 文件；没在盯、但有一步在跑（比如编辑器开着时从界面开始的），就开始盯它
void ProbeRunner::pollStep()
{
    const auto view = processor_.session().view();
    std::optional<core::StepId> running;
    for (std::size_t i = 0; i < session::kStepCount; ++i)
        if (view.steps[i].status == core::StepStatus::Running)
            running = static_cast<core::StepId>(i);
    if (watch_ && running != watch_->id)
        finishStep(view);
    if (!watch_ && running)
        watch_ = Watch{*running, view.runs[static_cast<std::size_t>(*running)], false};
}

void ProbeRunner::finishStep(const session::SessionView& view)
{
    const Watch w = *watch_;
    watch_.reset();
    ++stepFiles_;
    if (!outDir_)
        return;
    const auto i = static_cast<std::size_t>(w.id);
    // 做完过的次数涨了就是做完；没涨时，探针发过取消的算取消，否则看状态（作废，或被界面取消）
    const char* outcome = view.runs[i] > w.runsAtStart                        ? "done"
                          : w.cancelIssued                                    ? "cancelled"
                          : view.steps[i].status == core::StepStatus::Invalid ? "invalid"
                                                                              : "cancelled";
    auto content = header("step");
    if (auto* o = content.getDynamicObject())
    {
        o->setProperty("seq", stepFiles_);
        o->setProperty("step", wire(core::kStepIdNames[i]));
        o->setProperty("outcome", outcome);
        o->setProperty("snapshot", snapshot());
        o->setProperty("evidence", evidenceReply(w.id));
    }
    enqueue("step-" + wire(core::kStepIdNames[i]).toStdString() + "-" +
                juce::String(stepFiles_).paddedLeft('0', 3).toStdString() + pidSuffix().toStdString(),
            content);
}

// 快照：与编辑器同一个拼装函数，只是不带界面字段（lang、uiScale、guideSeen）；
// 也没有墙钟的回调年龄，所以 connect.callbackAgeSec 是 Session 的占位值，fixes 里也没有“没有回调”
juce::var ProbeRunner::snapshot() const
{
    return buildBridgeSnapshot(processor_, hostName_, nullptr, std::nullopt);
}

// 与桥的 getEvidence 回执相同
juce::var ProbeRunner::evidenceReply(core::StepId id) const
{
    const auto pack = processor_.session().evidence(id);
    if (pack == nullptr)
        return probe::toVar(probe::Reply{false, kNoEvidence});
    return makeObject({{"ok", true}, {"evidence", parseJson(session::evidenceJson(*pack))}});
}

juce::var ProbeRunner::scriptVar() const
{
    const char* state = !scripted_ ? "none" : scriptError_.empty() ? "ok" : "error";
    auto v = makeObject({{"state", state},
                         {"actions", static_cast<int>(scriptActions_)},
                         {"remaining", static_cast<int>(scheduler_.remaining())}});
    if (!scriptError_.empty())
        v.getDynamicObject()->setProperty("error", juce::String(scriptError_));
    return v;
}

// step 与 final 文件共有的字段
juce::var ProbeRunner::header(const char* kind) const
{
    return makeObject({{"schema", probe::kSchemaVersion},
                       {"kind", kind},
                       {"pid", static_cast<juce::int64>(probe::currentPid())},
                       {"processedSec", orNull(processedSec())},
                       {"script", scriptVar()}});
}

void ProbeRunner::finalReport(const char* reason)
{
    if (!outDir_)
        return;
    try
    {
        pollStep(); // 刚结束的那一步先写它自己的文件
        auto content = header("final");
        if (auto* o = content.getDynamicObject())
        {
            o->setProperty("reason", reason);
            o->setProperty("stepFiles", stepFiles_);
            o->setProperty("snapshot", snapshot());
        }
        enqueue("final" + pidSuffix().toStdString(), content);
        flushPending();
    }
    catch (...)
    {
    }
}

// 动作日志放在最后（正在等的那个、已执行的全部），便于人读
void ProbeRunner::enqueue(const std::string& fileName, const juce::var& content)
{
    if (auto* o = content.getDynamicObject())
    {
        const auto& waiting = scheduler_.waiting();
        o->setProperty("waiting", waiting ? probe::toVar(*waiting) : juce::var());
        o->setProperty("actions", probe::actionsVar(scheduler_));
    }
    if (pending_.size() >= kPendingMax)
        pending_.erase(pending_.begin());
    pending_.emplace_back(*outDir_ / fs::u8path(fileName), juce::JSON::toString(content).toStdString() + "\n");
    flushPending();
}

// 按先后写；写不进的（目录不存在、被占着）连同它后面的留到下一拍，保证同名文件后写的覆盖先写的
void ProbeRunner::flushPending()
{
    std::size_t written = 0;
    while (written < pending_.size() && probe::writeFileSerialized(pending_[written].first, pending_[written].second))
        ++written;
    pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(written));
}
} // namespace miciri
