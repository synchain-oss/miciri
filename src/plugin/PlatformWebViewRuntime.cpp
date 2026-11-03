// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from synchain-oss/rinne; see upstream/plugin.json
//
// WebView2 运行时探测。单独放一个文件，是为了只有它引用 loader 的导出函数。

#include "PlatformWebView.h"

#if JUCE_WINDOWS

extern "C" {
long __stdcall GetAvailableCoreWebView2BrowserVersionString(const wchar_t* browserExecutableFolder,
                                                            wchar_t** versionInfo);
void __stdcall CoTaskMemFree(void* pv);
}
#endif

namespace miciri::webview
{
// 经 loader 查询本机已装的运行时版本；查不到记为 missing，主版本过低记为 tooOld。
PlatformWebView::RuntimeInfo PlatformWebView::runtimeInfo()
{
#if JUCE_WINDOWS
    RuntimeInfo info;

    wchar_t* version = nullptr;
    const long hr = GetAvailableCoreWebView2BrowserVersionString(nullptr, &version);
    const bool present = hr >= 0 && version != nullptr && version[0] != L'\0';
    if (present)
        info.version = juce::String(version);
    if (version != nullptr)
        CoTaskMemFree(version);

    if (!present)
        return info;

    const int major = majorVersionOf(info.version);
    info.status = (major >= 0 && major < kMinRuntimeMajor) ? RuntimeStatus::tooOld : RuntimeStatus::ok;
    return info;
#else
    return {RuntimeStatus::ok, {}};
#endif
}

bool PlatformWebView::runtimeAvailable()
{
    return runtimeInfo().status == RuntimeStatus::ok;
}
} // namespace miciri::webview
