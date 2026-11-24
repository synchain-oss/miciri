// SPDX-License-Identifier: GPL-3.0-or-later
//
// 兜底面板文案与启动阶段记录的实现。中文串一律经 fromUTF8 构造：juce::String 的 const char* 构造只收 ASCII。
#include "StartupText.h"

namespace miciri::webview
{
namespace
{
juce::String zh(const char* utf8)
{
    return juce::String::fromUTF8(utf8);
}

juce::String messageZh(FallbackKind kind, WebEngine engine)
{
    const bool wv2 = (engine == WebEngine::webView2);
    switch (kind)
    {
    case FallbackKind::missingRuntime:
        return wv2 ? zh("没有找到 Microsoft Edge WebView2 运行时，完整界面加载不了。\n"
                        "安装一次运行时，再重新打开插件窗口。")
                   : zh("系统网页视图（WebKit）不可用，完整界面加载不了。\n"
                        "更新 macOS 后再重新打开插件窗口。");
    case FallbackKind::runtimeTooOld:
        return wv2 ? zh("已安装的 Microsoft Edge WebView2 运行时太旧，这个插件用不了。\n"
                        "更新到常青版（Evergreen）运行时，再重新打开插件窗口。")
                   : zh("系统网页视图（WebKit）太旧，这个插件用不了。\n"
                        "更新 macOS 后再重新打开插件窗口。");
    case FallbackKind::pageMissing:
        return zh("这个版本的插件里缺少界面页面。\n"
                  "请重新安装插件；如果还是这样，请把下面的诊断信息反馈给我们。");
    case FallbackKind::bootError:
        return zh("插件界面启动失败（页面脚本出错）。\n"
                  "点“重试”；如果一直这样，请把下面的诊断信息反馈给我们。");
    case FallbackKind::envNotStarted:
        return wv2 ? zh("WebView2 环境没有启动（页面导航一直没开始）。\n"
                        "常见原因：用户数据目录不可写，或被另一个用不同网页视图设置打开它的程序占着；\n"
                        "也可能是宿主拦住了 msedgewebview2.exe 子进程。")
                   : zh("系统网页视图（WebKit）没有启动（页面导航一直没开始）。\n"
                        "常见原因：宿主拦住了 WebKit 的网页内容进程，或系统内存不足。\n"
                        "点“重试”，或关掉插件窗口再打开。");
    case FallbackKind::navStalled:
        return zh("页面开始加载了，但一直没有加载完。\n"
                  "点“重试”，或关掉插件窗口再打开。");
    case FallbackKind::bridgeStalled:
    default:
        return zh("页面加载完了，但插件界面一直没有完成启动。\n"
                  "点“重试”；如果一直这样，请把下面的诊断信息反馈给我们。");
    }
}

juce::String messageEn(FallbackKind kind, WebEngine engine)
{
    const bool wv2 = (engine == WebEngine::webView2);
    switch (kind)
    {
    case FallbackKind::missingRuntime:
        return wv2 ? "Microsoft Edge WebView2 Runtime was not found, so the full UI cannot load.\n"
                     "Install the runtime once, then reopen this plugin window."
                   : "The system web view (WebKit) is not available, so the full UI cannot load.\n"
                     "Update macOS, then reopen this plugin window.";
    case FallbackKind::runtimeTooOld:
        return wv2 ? "The installed Microsoft Edge WebView2 Runtime is too old for this plugin.\n"
                     "Update to the Evergreen runtime, then reopen this plugin window."
                   : "The system web view (WebKit) is too old for this plugin.\n"
                     "Update macOS, then reopen this plugin window.";
    case FallbackKind::pageMissing:
        return "The UI page is missing from this build of the plugin.\n"
               "Reinstall the plugin; if it keeps failing, report the diagnostics below.";
    case FallbackKind::bootError:
        return "The plugin UI failed to start (front-end script error).\n"
               "Click Retry; if it keeps failing, report the diagnostics below.";
    case FallbackKind::envNotStarted:
        return wv2 ? "The WebView2 environment did not start (no navigation ever began).\n"
                     "This is usually the user-data folder being unwritable or held by another program that\n"
                     "opened it with different web view settings, or the host blocking the msedgewebview2.exe\n"
                     "child process."
                   : "The system web view (WebKit) did not start (no navigation ever began).\n"
                     "This is usually the host blocking the WebKit web content process, or the\n"
                     "system running low on memory. Click Retry, or close and reopen this plugin window.";
    case FallbackKind::navStalled:
        return "The page started loading but never finished.\n"
               "Click Retry, or close and reopen this plugin window.";
    case FallbackKind::bridgeStalled:
    default:
        return "The page loaded, but the plugin UI never finished starting up.\n"
               "Click Retry; if it keeps failing, report the diagnostics below.";
    }
}

constexpr std::array<const char*, StartupTrace::kStageCount> kLineNames{"script", "frame", "bridge", "ready"};
} // namespace

bool isChineseUi(const juce::String& lang)
{
    return lang.startsWithIgnoreCase("zh");
}

FallbackText fallbackText(FallbackKind kind, bool chinese, WebEngine engine)
{
    FallbackText t;
    t.message = chinese ? messageZh(kind, engine) : messageEn(kind, engine);
    t.retry = chinese ? zh("重试") : juce::String("Retry");
    t.install = chinese ? zh("安装 WebView2 运行时") : juce::String("Install WebView2 Runtime");
    return t;
}

bool StartupTrace::note(Stage stage, int elapsedMs) noexcept
{
    auto& slot = ms_[static_cast<std::size_t>(stage)];
    if (slot >= 0)
        return false;
    slot = elapsedMs < 0 ? 0 : elapsedMs;
    return true;
}

juce::String StartupTrace::line() const
{
    juce::String out;
    for (std::size_t i = 0; i < ms_.size(); ++i)
    {
        if (i > 0)
            out << " > ";
        out << kLineNames[i] << ' ' << (ms_[i] >= 0 ? juce::String(ms_[i]) + " ms" : juce::String("-"));
    }
    return out;
}

std::optional<StartupTrace::Stage> StartupTrace::fromPageName(const juce::String& name)
{
    if (name == "script")
        return Stage::pageScript;
    if (name == "ready")
        return Stage::uiReady;
    return std::nullopt;
}
} // namespace miciri::webview
