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

// 用户数据目录固定在本机应用数据下，所有实例共用一个，宿主里多开时进程组可以复用同一个环境。
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
               "This is usually the user-data folder being unwritable or already in use by another\n"
               "process, or the host blocking the msedgewebview2.exe child process.";

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
