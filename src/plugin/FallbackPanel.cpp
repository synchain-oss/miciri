// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from synchain-oss/rinne; see upstream/plugin.json
//
// 兜底面板的布局与绘制。控件带 componentID，便于测试与无障碍工具定位。
#include "FallbackPanel.h"

#include <utility>

namespace miciri::webview
{
namespace
{
constexpr juce::uint32 kTitleTextArgb = 0xff21212a;
constexpr juce::uint32 kBodyTextArgb = 0xff52525c;
constexpr float kTitleFontPx = 20.0f;
constexpr float kDetailsFontPx = 11.0f;
constexpr int kPad = 16;
constexpr int kTitleH = 30;
constexpr int kMessageH = 64;
constexpr int kDetailsH = 56;
constexpr int kButtonH = 32;
constexpr int kGap = 8;
constexpr int kInstallInsetX = 24;
constexpr int kRetryInsetX = 96;
} // namespace

FallbackPanel::FallbackPanel(Options options) : options_(std::move(options))
{
    title_.setComponentID("fallback.title");
    title_.setText(options_.title, juce::dontSendNotification);
    title_.setJustificationType(juce::Justification::centred);
    title_.setFont(juce::Font(juce::FontOptions(kTitleFontPx, juce::Font::bold)));
    title_.setColour(juce::Label::textColourId, juce::Colour(kTitleTextArgb));
    addAndMakeVisible(title_);

    message_.setComponentID("fallback.message");
    message_.setText(options_.message, juce::dontSendNotification);
    message_.setJustificationType(juce::Justification::centredTop);
    message_.setColour(juce::Label::textColourId, juce::Colour(kBodyTextArgb));
    addAndMakeVisible(message_);

    details_.setComponentID("fallback.details");
    details_.setText(options_.details, juce::dontSendNotification);
    details_.setJustificationType(juce::Justification::centredTop);
    details_.setFont(
        juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), kDetailsFontPx, juce::Font::plain)));
    details_.setColour(juce::Label::textColourId, juce::Colour(kBodyTextArgb));
    details_.setMinimumHorizontalScale(1.0f);
    addChildComponent(details_);
    details_.setVisible(options_.details.isNotEmpty());

    if (options_.showInstall)
    {
        install_.setComponentID("fallback.install");
        install_.setButtonText("Install WebView2 Runtime");
        install_.onClick = [this] {
            if (options_.onInstall)
                options_.onInstall();
        };
        addAndMakeVisible(install_);
    }

    retry_.setComponentID("fallback.retry");
    retry_.setButtonText("Retry");
    retry_.onClick = [this] {
        if (options_.onRetry)
            options_.onRetry();
    };
    addAndMakeVisible(retry_);
}

FallbackPanel::~FallbackPanel() = default;

void FallbackPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(kFallbackPanelArgb));
}

void FallbackPanel::resized()
{
    auto b = getLocalBounds().reduced(kPad);
    title_.setBounds(b.removeFromTop(kTitleH));
    message_.setBounds(b.removeFromTop(kMessageH));
    if (details_.isVisible())
        details_.setBounds(b.removeFromTop(kDetailsH));
    b.removeFromTop(kGap);
    if (options_.showInstall)
    {
        install_.setBounds(b.removeFromTop(kButtonH).reduced(kInstallInsetX, 0));
        b.removeFromTop(kGap);
    }
    retry_.setBounds(b.removeFromTop(kButtonH).reduced(kRetryInsetX, 0));
}
} // namespace miciri::webview
