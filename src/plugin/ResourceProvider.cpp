// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from synchain-oss/rinne; see upstream/plugin.json
//
// 资源查找、导航目标校验和日志文本整形。页面里来的文字一律当不可信数据，写日志前先去掉控制字符。

#include "ResourceProvider.h"

#include <string>
#include <vector>

namespace miciri::webview
{
namespace
{
struct Origin
{
    std::string scheme;
    std::string authority;
};

char asciiLower(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string asciiLower(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (const char c : s)
        out.push_back(asciiLower(c));
    return out;
}

bool isSchemeChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.';
}

std::optional<Origin> parseOrigin(const std::string& text)
{
    for (const char ch : text)
    {
        const auto c = static_cast<unsigned char>(ch);
        if (c <= 0x20 || c == 0x7f)
            return std::nullopt;
    }

    const auto sep = text.find("://");
    if (sep == std::string::npos || sep == 0)
        return std::nullopt;

    const auto authorityStart = sep + 3;
    const auto authorityEnd = text.find_first_of("/?#", authorityStart);

    Origin origin;
    origin.scheme = asciiLower(text.substr(0, sep));
    origin.authority = asciiLower(text.substr(
        authorityStart, authorityEnd == std::string::npos ? std::string::npos : authorityEnd - authorityStart));

    if (origin.authority.empty() || origin.scheme.front() < 'a' || origin.scheme.front() > 'z')
        return std::nullopt;
    for (const char c : origin.scheme)
        if (!isSchemeChar(c))
            return std::nullopt;
    for (const char ch : origin.authority)
    {
        const auto c = static_cast<unsigned char>(ch);
        if (c >= 0x80 || c == '@' || c == '\\')
            return std::nullopt;
    }
    return origin;
}
} // namespace

ResourceProvider::ResourceProvider(Source source) : source_(source) {}

bool ResourceProvider::isBlankPage(const juce::String& url)
{
    return url.toStdString() == "about:blank";
}

// 只放行和资源根同源（协议加主机）的导航，其余一律拒绝；about:blank 例外。
bool ResourceProvider::isRootNavigation(const juce::String& url, const juce::String& resourceRoot)
{
    if (isBlankPage(url))
        return true;

    const std::string target = url.toStdString();

    const auto root = parseOrigin(resourceRoot.toStdString());
    const auto wanted = parseOrigin(target);
    return root.has_value() && wanted.has_value() && root->scheme == wanted->scheme &&
           root->authority == wanted->authority;
}

namespace
{
bool breaksLogLine(juce::juce_wchar c)
{
    return c < 0x20 || c == 0x7f || (c >= 0x80 && c <= 0x9f) || c == 0x2028 || c == 0x2029;
}

juce::String shapeUntrusted(const juce::String& text, int maxChars, bool asciiOnly)
{
    juce::String out;
    int kept = 0;
    for (auto it = text.begin(); it != text.end() && kept < maxChars; ++it, ++kept)
    {
        const auto c = *it;
        const bool replace = breaksLogLine(c) || (asciiOnly && c > 0x7e);
        out += juce::String::charToString(replace ? static_cast<decltype(c)>('?') : c);
    }
    return out;
}
} // namespace

juce::String ResourceProvider::urlForLog(const juce::String& url, int maxChars)
{
    return shapeUntrusted(url, maxChars, false);
}

juce::String ResourceProvider::asciiForLog(const juce::String& text, int maxChars)
{
    return shapeUntrusted(text, maxChars, true);
}

juce::String ResourceProvider::normalizePath(const juce::String& url)
{
    juce::String path = url.upToFirstOccurrenceOf("?", false, false).upToFirstOccurrenceOf("#", false, false);

    if (const int schemeEnd = path.indexOf("://"); schemeEnd >= 0)
    {
        const int pathStart = path.indexOfChar(schemeEnd + 3, '/');
        path = (pathStart >= 0) ? path.substring(pathStart + 1) : juce::String("index.html");
    }

    if (path.startsWith("/"))
        path = path.substring(1);

    return path.isEmpty() ? juce::String("index.html") : path;
}

// 按页面路径查资源：路径先规整，再映射成扁平名，含上跳或反斜杠的路径直接判未命中。
std::optional<ResourceProvider::Hit> ResourceProvider::lookup(const juce::String& url) const
{
    const auto fileName = flatName(normalizePath(url));
    if (fileName.isEmpty())
        return std::nullopt;

    if (source_.originalFilenames == nullptr || source_.resourceNames == nullptr || source_.getNamedResource == nullptr)
        return std::nullopt;

    for (int i = 0; i < source_.resourceCount; ++i)
    {
        if (juce::String(source_.originalFilenames[i]) != fileName)
            continue;
        int size = 0;
        if (const char* data = source_.getNamedResource(source_.resourceNames[i], size))
        {
            const auto ext = fileName.fromLastOccurrenceOf(".", false, false);
            return Hit{data, size, mimeForExtension(ext)};
        }
    }
    return std::nullopt;
}

#if JUCE_WEB_BROWSER
std::optional<juce::WebBrowserComponent::Resource> ResourceProvider::provide(const juce::String& url) const
{
    const auto hit = lookup(url);
    if (!hit)
        return std::nullopt;
    std::vector<std::byte> bytes(reinterpret_cast<const std::byte*>(hit->data),
                                 reinterpret_cast<const std::byte*>(hit->data) + hit->size);
    return juce::WebBrowserComponent::Resource{std::move(bytes), juce::String(hit->mime)};
}
#endif

// 构建时把 web/ 下的文件按相对路径复制成扁平名（斜杠换双下划线）再编进二进制，这里做同样的映射。
juce::String ResourceProvider::flatName(const juce::String& path)
{
    if (path.contains("..") || path.contains("\\") || path.contains("//"))
        return {};
    return path.replace("/", "__");
}

const char* ResourceProvider::mimeForExtension(const juce::String& ext)
{
    const auto e = ext.toLowerCase();
    if (e == "html" || e == "htm")
        return "text/html";
    if (e == "css")
        return "text/css";
    if (e == "js" || e == "mjs")
        return "text/javascript";
    if (e == "json")
        return "application/json";
    if (e == "svg")
        return "image/svg+xml";
    if (e == "woff2")
        return "font/woff2";
    if (e == "woff")
        return "font/woff";
    if (e == "ttf")
        return "font/ttf";
    if (e == "png")
        return "image/png";
    return "application/octet-stream";
}
} // namespace miciri::webview
