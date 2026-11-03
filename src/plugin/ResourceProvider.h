// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from synchain-oss/rinne; see upstream/plugin.json
//
// 把编进二进制的页面资源提供给 WebView，并校验导航目标是否仍在本插件的资源根下。
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <optional>

namespace miciri::webview
{
class ResourceProvider
{
public:
    // 二进制数据的清单，形状与 juce_add_binary_data 生成的命名空间一致。资源在二进制里的名字是扁平的：
    // 页面路径里的斜杠换成双下划线（见 flatName）。

    struct Source
    {
        int resourceCount = 0;
        const char* const* originalFilenames = nullptr;
        const char* const* resourceNames = nullptr;
        const char* (*getNamedResource)(const char* resourceName, int& dataSizeInBytes) = nullptr;
    };

    struct Hit
    {
        const char* data = nullptr;
        int size = 0;
        const char* mime = "application/octet-stream";
    };

    explicit ResourceProvider(Source source);

    static juce::String normalizePath(const juce::String& url);

    static bool isRootNavigation(const juce::String& url, const juce::String& resourceRoot);

    static bool isBlankPage(const juce::String& url);

    static juce::String urlForLog(const juce::String& url, int maxChars = 120);

    static juce::String asciiForLog(const juce::String& text, int maxChars = 300);

    std::optional<Hit> lookup(const juce::String& url) const;

#if JUCE_WEB_BROWSER

    std::optional<juce::WebBrowserComponent::Resource> provide(const juce::String& url) const;
#endif

    static juce::String flatName(const juce::String& path);
    static const char* mimeForExtension(const juce::String& ext);

    const Source& source() const { return source_; }

private:
    Source source_;
};
} // namespace miciri::webview
