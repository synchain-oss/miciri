// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from synchain-oss/rinne; see upstream/plugin.json
//
// 兜底面板：WebView 起不来（缺运行时、运行时过旧、页面缺失、加载超时、前端启动失败）时盖在编辑器上，
// 用原生控件说明原因，给出重试，缺运行时时给出安装入口，并附一行不含路径的诊断。
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>

namespace miciri::webview
{
inline constexpr juce::uint32 kFallbackPanelArgb = 0xffe6e9ee;

class FallbackPanel final : public juce::Component
{
public:
    struct Options
    {
        juce::String title;
        juce::String message;
        juce::String details; // 诊断行；空则不显示
        bool showInstall = false; // 运行时缺失或过旧时显示“安装”按钮
        std::function<void()> onInstall;
        std::function<void()> onRetry;
    };

    explicit FallbackPanel(Options options);
    ~FallbackPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    Options options_;
    juce::Label title_;
    juce::Label message_;
    juce::Label details_;
    juce::TextButton install_;
    juce::TextButton retry_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FallbackPanel)
};
} // namespace miciri::webview
