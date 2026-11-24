// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from synchain-oss/rinne; see upstream/plugin.json
//
// 平台 WebView 的差异收口：Windows 用 WebView2（含运行时探测、用户数据目录），macOS 用系统 WebKit。
// 另含开局画面：窗口一出现、页面首帧到来之前，原生侧先画一层和外壳页面同形的底（深色外圈、圆角渐变、柔光球），
// 页面就绪后揭开 WebView 时只多出内容，不跳色、不闪白。
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <cmath>
#include <cstddef>

#include "StartupText.h"

namespace miciri::webview
{
// 占位渐变的一个色标。色标和角度与外壳页面的 --page-gradient（web/shared/tokens.css）逐一相同，
// 页面冒烟 smoke-tokens 对拍这里的数组，改一边不改另一边会变红。
struct ShellBackdropStop
{
    juce::uint32 rgb;
    float pos;
};

inline constexpr float kShellBackdropAngleDeg = 157.0f;

inline constexpr ShellBackdropStop kShellBackdropStops[] = {
    {0xb5acc9, 0.00f},
    {0xccbfd5, 0.32f},
    {0xe3d2e0, 0.64f},
    {0xfde8ed, 1.00f},
};

// 外壳背后的柔光球：位置按页面写法记成到外壳某条边的距离（shell-glass.css 的 .sc-halo--N），
// 直径、模糊与颜色取自 tokens.css 的 --halo-*，同样由 smoke-tokens 对拍。页面里是模糊过的实心圆，这里用径向渐变近似。
struct ShellHalo
{
    float insetX; // fromRight 为真时是到右边的距离，否则到左边
    float insetY; // fromBottom 为真时是到底边的距离，否则到顶边
    bool fromRight;
    bool fromBottom;
    juce::uint32 rgb;
    float alpha;
};

inline constexpr float kShellHaloDiameter = 320.0f;
inline constexpr float kShellHaloBlur = 52.0f;

// 页面里的柔光球是 filter: blur 的实心圆，这里用三段径向渐变近似它的衰减：只要求颜色、位置、直径与模糊量
// （上面几项，由 smoke-tokens 对拍）和页面一致，下面三个衰减参数是看着页面调的近似，不要求和页面逐像素相同。
inline constexpr float kShellHaloCoreAlpha = 0.85f; // 半径减一个模糊量处（还在实心部分里）的不透明度比例
inline constexpr float kShellHaloEdgeAlpha = 0.5f; // 半径处（模糊带的中线）的不透明度比例
inline constexpr float kShellHaloFadeBlurs = 2.0f; // 从半径往外再过几个模糊量淡到全透明

inline constexpr ShellHalo kShellHalos[] = {
    {-90.0f, -80.0f, false, false, 0xb5acc9, 0.62f},
    {-70.0f, -110.0f, true, true, 0xf8cedb, 0.50f},
    {480.0f, 250.0f, false, false, 0xc4b0de, 0.46f},
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

inline juce::Colour opaqueRgb(juce::uint32 rgb)
{
    return juce::Colour(static_cast<juce::uint8>((rgb >> 16) & 0xffu), static_cast<juce::uint8>((rgb >> 8) & 0xffu),
                        static_cast<juce::uint8>(rgb & 0xffu));
}

// 一个柔光球：中心到“半径减模糊”几乎实色，到半径处一半，再往外淡到 0（比例见上面的 kShellHalo* 衰减参数）。
inline void paintShellHalo(juce::Graphics& g, juce::Point<float> centre, float zoom, const ShellHalo& halo)
{
    const float radius = kShellHaloDiameter * 0.5f;
    const float outer = (radius + kShellHaloFadeBlurs * kShellHaloBlur) * zoom;
    if (!(outer > 0.0f))
        return;
    const auto colour = opaqueRgb(halo.rgb).withAlpha(halo.alpha);
    juce::ColourGradient gradient(colour, centre.x, centre.y, colour.withAlpha(0.0f), centre.x + outer, centre.y, true);
    gradient.addColour(static_cast<double>((radius - kShellHaloBlur) * zoom / outer),
                       colour.withMultipliedAlpha(kShellHaloCoreAlpha));
    gradient.addColour(static_cast<double>(radius * zoom / outer), colour.withMultipliedAlpha(kShellHaloEdgeAlpha));
    g.setGradientFill(gradient);
    g.fillEllipse(centre.x - outer, centre.y - outer, outer * 2.0f, outer * 2.0f);
}

// 开局画面：外圈铺深色，设计盒比例的圆角矩形里按渐变填充，再在圆角里叠三个柔光球。
inline void paintShellPlaceholder(juce::Graphics& g, juce::Rectangle<float> area, float designW, float designH)
{
    g.setColour(opaqueRgb(kShellRimRgb));
    g.fillRect(area);
    const auto shell = shellPlaceholderBounds(area, designW, designH);
    if (shell.isEmpty())
        return;
    const float zoom = shellFitZoom(area.getWidth(), area.getHeight(), designW, designH);
    juce::Path outline;
    outline.addRoundedRectangle(shell, kShellCornerRadiusPx * zoom);
    g.setGradientFill(shellBackdropGradient(shell));
    g.fillPath(outline);

    juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(outline);
    const float r = kShellHaloDiameter * 0.5f;
    for (const auto& halo : kShellHalos)
    {
        const juce::Point<float> centre{halo.fromRight ? designW - halo.insetX - r : halo.insetX + r,
                                        halo.fromBottom ? designH - halo.insetY - r : halo.insetY + r};
        paintShellHalo(g, shell.getTopLeft() + centre * zoom, zoom, halo);
    }
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

    using Engine = WebEngine;
#if JUCE_WINDOWS
    static constexpr Engine kEngine = Engine::webView2;
#else
    static constexpr Engine kEngine = Engine::systemWebKit;
#endif

    static juce::String runtimeDiagnosticsField(const RuntimeInfo& info, Engine engine = kEngine);

    static juce::String backgroundColourDiagnostics(const RuntimeInfo& info, Engine engine = kEngine);

    static constexpr bool revealGateEnabled(Engine engine = kEngine) { return engine == Engine::webView2; }

    static constexpr bool offersRuntimeDownload(Engine engine = kEngine) { return engine == Engine::webView2; }

    static juce::String userDataFolderDisplay(const juce::File& folder, bool basenameOnly);
};
} // namespace miciri::webview
