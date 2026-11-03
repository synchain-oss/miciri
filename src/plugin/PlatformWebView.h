// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from synchain-oss/rinne; see upstream/plugin.json
//
// 平台 WebView 的差异收口：Windows 用 WebView2（含运行时探测、用户数据目录），macOS 用系统 WebKit。
// 另含开窗前的占位底色：页面首帧到来之前先画一层和页面背景一致的底，避免闪白。
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <cmath>
#include <cstddef>

namespace miciri::webview
{
// 占位渐变的一个色标。色标和角度要与页面最外层背景保持一致，否则首帧前后会跳色。
struct ShellBackdropStop
{
    juce::uint32 rgb;
    float pos;
};

inline constexpr float kShellBackdropAngleDeg = 157.0f;

inline constexpr ShellBackdropStop kShellBackdropStops[] = {
    {0xdfe3ea, 0.00f},
    {0xf1f3f7, 1.00f},
};

inline juce::Colour shellBackdropMid()
{
    constexpr auto count = sizeof(kShellBackdropStops) / sizeof(kShellBackdropStops[0]);
    for (std::size_t i = 0; i + 1 < count; ++i)
    {
        const auto& a = kShellBackdropStops[i];
        const auto& b = kShellBackdropStops[i + 1];

        if (a.pos <= 0.5f && 0.5f <= b.pos && b.pos > a.pos)
        {
            const float t = (0.5f - a.pos) / (b.pos - a.pos);
            const auto mix = [t](juce::uint32 ca, juce::uint32 cb, int shift) -> juce::uint8 {
                const auto va = static_cast<float>((ca >> shift) & 0xffu);
                const auto vb = static_cast<float>((cb >> shift) & 0xffu);
                return static_cast<juce::uint8>(juce::roundToInt(va + t * (vb - va)));
            };
            return juce::Colour(mix(a.rgb, b.rgb, 16), mix(a.rgb, b.rgb, 8), mix(a.rgb, b.rgb, 0));
        }
    }
    return juce::Colour(static_cast<juce::uint8>((kShellBackdropStops[0].rgb >> 16) & 0xffu),
                        static_cast<juce::uint8>((kShellBackdropStops[0].rgb >> 8) & 0xffu),
                        static_cast<juce::uint8>(kShellBackdropStops[0].rgb & 0xffu));
}

inline juce::ColourGradient shellBackdropGradient(juce::Rectangle<float> area)
{
    if (area.isEmpty())
    {
        const auto mid = shellBackdropMid();
        return juce::ColourGradient::vertical(mid, 0.0f, mid, 1.0f);
    }
    const auto rad = juce::degreesToRadians(kShellBackdropAngleDeg);
    const auto sinA = std::sin(rad);
    const auto cosA = std::cos(rad);
    const auto len = std::abs(area.getWidth() * sinA) + std::abs(area.getHeight() * cosA);
    const auto centre = area.getCentre();
    const juce::Point<float> dir{sinA, -cosA};
    const auto start = centre - dir * (len * 0.5f);
    const auto end = centre + dir * (len * 0.5f);

    const auto colourOf = [](const ShellBackdropStop& s) {
        return juce::Colour(static_cast<juce::uint8>((s.rgb >> 16) & 0xffu),
                            static_cast<juce::uint8>((s.rgb >> 8) & 0xffu), static_cast<juce::uint8>(s.rgb & 0xffu));
    };
    constexpr auto count = sizeof(kShellBackdropStops) / sizeof(kShellBackdropStops[0]);
    juce::ColourGradient gradient{colourOf(kShellBackdropStops[0]),
                                  start.x,
                                  start.y,
                                  colourOf(kShellBackdropStops[count - 1]),
                                  end.x,
                                  end.y,
                                  false};
    for (std::size_t i = 1; i + 1 < count; ++i)
        gradient.addColour(static_cast<double>(kShellBackdropStops[i].pos), colourOf(kShellBackdropStops[i]));
    return gradient;
}

inline constexpr float kShellCornerRadiusPx = 20.0f;
inline constexpr juce::uint32 kShellRimRgb = 0x191820;

inline float shellFitZoom(float width, float height, float designW, float designH) noexcept
{
    if (!(width > 0.0f) || !(height > 0.0f) || !(designW > 0.0f) || !(designH > 0.0f))
        return 1.0f;
    const double f = std::fmin(static_cast<double>(width) / designW, static_cast<double>(height) / designH);
    if (!(f > 0.0) || !std::isfinite(f))
        return 1.0f;
    return static_cast<float>(std::floor(f * 1e4) / 1e4);
}

inline juce::Rectangle<float> shellPlaceholderBounds(juce::Rectangle<float> area, float designW, float designH)
{
    const float zoom = shellFitZoom(area.getWidth(), area.getHeight(), designW, designH);
    return juce::Rectangle<float>(designW * zoom, designH * zoom).withCentre(area.getCentre());
}

// 占位绘制：外圈铺深色，设计盒比例的圆角矩形里按渐变填充。
inline void paintShellPlaceholder(juce::Graphics& g, juce::Rectangle<float> area, float designW, float designH)
{
    g.setColour(juce::Colour(static_cast<juce::uint8>((kShellRimRgb >> 16) & 0xffu),
                             static_cast<juce::uint8>((kShellRimRgb >> 8) & 0xffu),
                             static_cast<juce::uint8>(kShellRimRgb & 0xffu)));
    g.fillRect(area);
    const auto shell = shellPlaceholderBounds(area, designW, designH);
    if (shell.isEmpty())
        return;
    const float zoom = shellFitZoom(area.getWidth(), area.getHeight(), designW, designH);
    g.setGradientFill(shellBackdropGradient(shell));
    g.fillRoundedRectangle(shell, kShellCornerRadiusPx * zoom);
}

// 无状态的静态工具集合：运行时探测、选项装配、诊断文案。
class PlatformWebView
{
public:
#if JUCE_WEB_BROWSER

    static juce::WebBrowserComponent::Options makeWebViewOptions(juce::WebBrowserComponent::Options options,
                                                                 const juce::File& userDataFolder);
#endif

    static juce::File makeUserDataFolder();

    static int processId();

    static juce::String probeUserDataFolder(const juce::File& folder);

    static juce::String plainAsciiDetail(const juce::String& text, int maxLength = 120);
    // WebView2 运行时的三种状态；系统 WebKit 恒为 ok。

    enum class RuntimeStatus
    {
        ok,
        missing,
        tooOld
    };

    struct RuntimeInfo
    {
        RuntimeStatus status = RuntimeStatus::missing;
        juce::String version;
    };
    // 低于这个主版本的运行时不保证支持页面用到的特性，按过旧处理。

    static constexpr int kMinRuntimeMajor = 86;

    static const char* runtimeDownloadUrl();

    static RuntimeInfo runtimeInfo();

    static bool runtimeAvailable();

    static int majorVersionOf(const juce::String& version);

    enum class BackgroundColourSupport
    {
        available,
        unavailable,
        unknown
    };

    static constexpr int kBackgroundColourMinRuntimeMajor = 87;

    static BackgroundColourSupport backgroundColourSupport(const RuntimeInfo& info);

    enum class Engine
    {
        webView2,
        systemWebKit
    };
#if JUCE_WINDOWS
    static constexpr Engine kEngine = Engine::webView2;
#else
    static constexpr Engine kEngine = Engine::systemWebKit;
#endif

    static juce::String missingRuntimeMessage(Engine engine = kEngine);
    static juce::String tooOldRuntimeMessage(Engine engine = kEngine);

    static juce::String envNotStartedMessage(Engine engine = kEngine);

    static juce::String runtimeDiagnosticsField(const RuntimeInfo& info, Engine engine = kEngine);

    static juce::String backgroundColourDiagnostics(const RuntimeInfo& info, Engine engine = kEngine);

    static constexpr bool revealGateEnabled(Engine engine = kEngine) { return engine == Engine::webView2; }

    static constexpr bool offersRuntimeDownload(Engine engine = kEngine) { return engine == Engine::webView2; }

    static juce::String userDataFolderDisplay(const juce::File& folder, bool basenameOnly);
};
} // namespace miciri::webview
