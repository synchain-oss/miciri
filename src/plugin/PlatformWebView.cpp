// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from synchain-oss/rinne; see upstream/plugin.json
//
// 平台 WebView 差异的实现部分（用户数据目录、可写性探针、各平台的提示文案）。

#include "PlatformWebView.h"

#if JUCE_WINDOWS

extern "C" unsigned long __stdcall GetCurrentProcessId();
#else
#include <unistd.h>
#endif

namespace miciri::webview
{
namespace
{
int currentProcessId()
{
#if JUCE_WINDOWS
    return static_cast<int>(GetCurrentProcessId());
#else
    return static_cast<int>(::getpid());
#endif
}
} // namespace

int PlatformWebView::processId()
{
    return currentProcessId();
}

// 用户数据目录固定在本机应用数据下：所有实例、所有宿主进程共用这一个，不按进程拆开。
//
// WebView2 的浏览器进程组按用户数据目录共享：同一个目录就接入同一组 msedgewebview2.exe，请求来自同一进程还是
// 另一个进程都一样。REAPER 与 Cubase 同时开着 Miciri 时，后开的进程接入已经在跑的浏览器进程。失败条件只有一个：
// 环境选项与那组进程已有的不一致（控制器创建返回 ERROR_INVALID_STATE），和“来自另一个进程”无关。
// 本插件的环境选项是常量（JUCE 用默认选项创建环境，目录又是固定的），所有进程拿到的完全一致，所以可以安全共享。
// 在本机用独立的小程序对拍过：两个进程同一目录、默认选项，同时创建与先后创建都成功，共用一个浏览器进程；
// 第二个进程换一个浏览器参数，环境创建成功、控制器创建失败（0x8007139F）。
//
// 不按进程拆开的理由：每个目录各起一整套浏览器进程，进程组不再复用，每多开一个宿主就多一整套进程、内存与句柄；
// 还会留下残留目录，清理时要判断所属进程是否已退出，删错一个正在用的目录会让别的宿主里的窗口出问题。
// 目录放在 LocalAppData 而不是 Temp（磁盘清理可能在会话中途删掉它），也不是 Roaming（缓存会随漫游配置同步）。
// 目录里只有网页缓存，见 docs/PRIVACY.md。万一仍然创建失败，构造期的可写性探针与“导航从未开始”的说明面板会如实说出来。
juce::File PlatformWebView::makeUserDataFolder()
{
#if JUCE_WINDOWS

    return juce::File::getSpecialLocation(juce::File::windowsLocalAppData)
        .getChildFile("Synchain")
        .getChildFile("Miciri")
        .getChildFile("WebView2");
#else
    return {};
#endif
}

juce::String PlatformWebView::plainAsciiDetail(const juce::String& text, int maxLength)
{
    const auto trimmed = text.trim();
    if (trimmed.isEmpty() || trimmed.length() > maxLength)
        return {};
    for (const auto ch : trimmed)
    {
        if (ch < 0x20 || ch > 0x7e)
            return {};
    }
    return trimmed;
}

// 构造期的可写性探针：返回的文字不含路径，可以放进兜底面板。
juce::String PlatformWebView::probeUserDataFolder(const juce::File& folder)
{
    if (folder == juce::File())
        return {};

    const auto result = folder.createDirectory();
    if (result.failed())
    {
        const auto detail = plainAsciiDetail(result.getErrorMessage());
        return detail.isEmpty() ? juce::String("user-data folder not creatable")
                                : "user-data folder not creatable: " + detail;
    }

    const auto probe = folder.getChildFile(".miciri-write-probe");
    if (!probe.replaceWithText("ok"))
        return "user-data folder not writable";
    probe.deleteFile();
    return {};
}

#if JUCE_WEB_BROWSER
juce::WebBrowserComponent::Options PlatformWebView::makeWebViewOptions(juce::WebBrowserComponent::Options options,
                                                                       const juce::File& userDataFolder)
{
#if JUCE_WINDOWS
    using WBC = juce::WebBrowserComponent;
    WBC::Options::WinWebView2 wv2;
    wv2 = wv2.withUserDataFolder(userDataFolder);

    wv2 = wv2.withBackgroundColour(shellBackdropMid());
    options = options.withBackend(WBC::Options::Backend::webview2).withWinWebView2Options(wv2);
#else
    juce::ignoreUnused(userDataFolder);
#endif
    return options;
}
#endif

const char* PlatformWebView::runtimeDownloadUrl()
{
    return "https://go.microsoft.com/fwlink/p/?LinkId=2124703";
}

int PlatformWebView::majorVersionOf(const juce::String& version)
{
    const auto head = version.trim().upToFirstOccurrenceOf(".", false, false).trim();
    if (head.isEmpty() || !head.containsOnly("0123456789"))
        return -1;
    return head.getIntValue();
}

PlatformWebView::BackgroundColourSupport PlatformWebView::backgroundColourSupport(const RuntimeInfo& info)
{
    if (info.status == RuntimeStatus::missing)
        return BackgroundColourSupport::unknown;

    const int major = majorVersionOf(info.version);
    if (major < 0)
        return BackgroundColourSupport::unknown;

    return major >= kBackgroundColourMinRuntimeMajor ? BackgroundColourSupport::available
                                                     : BackgroundColourSupport::unavailable;
}

juce::String PlatformWebView::missingRuntimeMessage(Engine engine)
{
    if (engine == Engine::webView2)
        return "Microsoft Edge WebView2 Runtime was not found, so the full UI cannot load.\n"
               "Install the runtime once, then reopen this plugin window.";
    return "The system web view (WebKit) is not available, so the full UI cannot load.\n"
           "Update macOS, then reopen this plugin window.";
}

juce::String PlatformWebView::tooOldRuntimeMessage(Engine engine)
{
    if (engine == Engine::webView2)
        return "The installed Microsoft Edge WebView2 Runtime is too old for this plugin.\n"
               "Update to the Evergreen runtime, then reopen this plugin window.";
    return "The system web view (WebKit) is too old for this plugin.\n"
           "Update macOS, then reopen this plugin window.";
}

juce::String PlatformWebView::envNotStartedMessage(Engine engine)
{
    if (engine == Engine::webView2)
        return "The WebView2 environment did not start (no navigation ever began).\n"
               "This is usually the user-data folder being unwritable or held by another program that\n"
               "opened it with different web view settings, or the host blocking the msedgewebview2.exe\n"
               "child process.";

    return "The system web view (WebKit) did not start (no navigation ever began).\n"
           "This is usually the host blocking the WebKit web content process, or the\n"
           "system running low on memory. Click Retry, or close and reopen this plugin window.";
}

juce::String PlatformWebView::runtimeDiagnosticsField(const RuntimeInfo& info, Engine engine)
{
    if (engine == Engine::webView2)
        return "WebView2 " + (info.version.isNotEmpty() ? info.version : juce::String("not found"));

    return "WebKit (system)";
}

juce::String PlatformWebView::backgroundColourDiagnostics(const RuntimeInfo& info, Engine engine)
{
    if (engine != Engine::webView2)
        return {};

    using Support = BackgroundColourSupport;
    const auto support = backgroundColourSupport(info);
    const juce::String version = info.version.isNotEmpty() ? info.version : juce::String("unknown");

    const juce::String argb =
        juce::String::toHexString(static_cast<int>(shellBackdropMid().getARGB())).paddedLeft('0', 8);

    const juce::String floor = juce::String(kBackgroundColourMinRuntimeMajor);
    if (support == Support::available)
        return "webview2 default background: available -- ICoreWebView2Controller2 inferred present "
               "(from runtime " +
               version + " >= major " + floor + ", not directly observed), JUCE puts argb " + argb;
    if (support == Support::unavailable)
        return "webview2 default background: UNAVAILABLE -- ICoreWebView2Controller2 inferred absent "
               "(from runtime " +
               version + " < major " + floor + ", not directly observed), JUCE drops argb " + argb + " silently";
    if (info.status == RuntimeStatus::missing)

        return "webview2 default background: unknown (no WebView2 runtime detected)";
    return "webview2 default background: unknown (runtime version " + version + " not parsable)";
}

juce::String PlatformWebView::userDataFolderDisplay(const juce::File& folder, bool basenameOnly)
{
    if (folder == juce::File())
        return "(not used)";
    return basenameOnly ? folder.getFileName() : folder.getFullPathName();
}
} // namespace miciri::webview
