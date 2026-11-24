// SPDX-License-Identifier: GPL-3.0-or-later
//
// 编辑器启动的两样纯数据：兜底面板的中英文案，和启动各阶段的毫秒记录（写进诊断行）。
// 不碰组件、不碰 WebView，单元测试直接断言。
#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <optional>

namespace miciri::webview
{
// 兜底面板为什么出现。看门狗超时按导航状态再分三种：导航没开始、开始了没完成、完成了但界面没起来。
enum class FallbackKind
{
    missingRuntime,
    runtimeTooOld,
    pageMissing,
    bootError,
    envNotStarted,
    navStalled,
    bridgeStalled
};

// 平台网页引擎：Windows 是 WebView2，macOS 是系统 WebKit。文案里点名的东西不同。
enum class WebEngine
{
    webView2,
    systemWebKit
};

struct FallbackText
{
    juce::String message;
    juce::String retry;
    juce::String install;
};

// 界面语言代码是不是中文。兜底面板跟随界面语言：中文界面显示中文，其余显示英文。
bool isChineseUi(const juce::String& lang);

FallbackText fallbackText(FallbackKind kind, bool chinese, WebEngine engine);

// 诊断信息从 udf 行往下的几行：udf 行（数据目录有问题时，方括号说明紧跟在这一行末尾）、启动阶段行、
// 页面报告的出错位置（有才写）。每条说明跟着它所属的那一行，用户贴回来时不会读串。
juce::String diagnosticsTail(const juce::String& udfLine, const juce::String& udfIssue, const juce::String& stageLine,
                             const juce::String& bootError);

// 一次加载尝试里各阶段第一次出现的时刻（相对这次尝试开始的毫秒数）。
// 阶段按发生的先后排：页面脚本开始运行、页面画出外壳、页面第一次调到原生（桥通了）、页面应用了首个快照（启动完成）。
// 诊断行把四个都写出来，没到的写“-”，用户贴回来就能看出卡在哪一步。
class StartupTrace
{
public:
    enum class Stage
    {
        pageScript,
        firstFrame,
        bridge,
        uiReady
    };
    static constexpr int kStageCount = 4;

    void reset() noexcept { ms_.fill(-1); }

    // 只记第一次；返回这一次是否记下了（重复上报返回 false）。
    bool note(Stage stage, int elapsedMs) noexcept;

    // 没到过返回 -1。
    int at(Stage stage) const noexcept { return ms_[static_cast<std::size_t>(stage)]; }

    // 形如 "script 412 ms > frame 530 ms > bridge - > ready -"，只含 ASCII。
    juce::String line() const;

    // 页面上报的阶段名（启动信号脚本里的名字）；不认识的返回空。
    static std::optional<Stage> fromPageName(const juce::String& name);

private:
    std::array<int, kStageCount> ms_{-1, -1, -1, -1};
};
} // namespace miciri::webview
