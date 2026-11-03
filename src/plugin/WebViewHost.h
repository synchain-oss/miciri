// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from synchain-oss/rinne; see upstream/plugin.json
//
// 网页编辑器的宿主：装配 WebView、接线桥函数与事件、管理缩放与语言，并在 WebView 起不来时换上兜底面板。
// 看门狗：页面在预算时间内没有让桥就绪就判超时，切到兜底面板，用户可以重试。
// 派生类（MiciriEditor）只需要覆盖三个钩子：buildSnapshot（快照）、emitTick（定时推送）、
// handleBridgeCall（核心四个函数以外的桥函数）。
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <atomic>
#include <functional>
#include <memory>

#include "BridgeBase.h"
#include "FallbackPanel.h"
#include "PlatformWebView.h"
#include "ResourceProvider.h"
#include "WebViewRevealGate.h"

namespace miciri::webview
{
class WebViewHost : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    using NativeCompletion = juce::WebBrowserComponent::NativeFunctionCompletion;

    struct Config
    {
        ResourceProvider::Source resourceSource; // 编进二进制的页面资源；没有页面时传空 Source
        juce::String entryPath = "app/shell/index.html"; // 入口页在资源根下的相对路径
    };

    WebViewHost(juce::AudioProcessor& processor, Config config);
    ~WebViewHost() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    float uiScale() const { return scale_.scale(); }
    const juce::String& lang() const { return lang_; }
    bool scaleLocked() const { return scale_.locked(); }
    void setScaleLocked(bool locked);
    bool bridgeReady() const { return bridgeReady_; }
    bool showingFallback() const { return fallback_ != nullptr; }

    // 定时器频率：电平事件的推送节拍，也是看门狗与遮挡闸的检查节拍。
    static constexpr int kTickHz = 30;

    // 看门狗预算：冷启动（本进程里还没有任何桥就绪过）放宽，热启动收紧；
    // 页面开始导航后再给一段时间等桥就绪，只顺延一次。
    static constexpr int kColdLoadBudgetMs = 15000;
    static constexpr int kWarmLoadBudgetMs = 5000;
    static constexpr int kAfterNavBudgetMs = 5000;
    static_assert(RevealGate::kRevealFallbackMs < kAfterNavBudgetMs,
                  "遮挡闸的超时必须先于看门狗触发，否则占位阶段会被兜底面板直接顶掉");

    // 页面侧的两个诊断事件，不属于桥契约：启动失败上报和首帧信号。
    static constexpr const char* kBootErrorEventId = "__miciri__bootError";
    static constexpr const char* kFirstFrameEventId = "__miciri__firstFrame";
    static constexpr const char* kPaintDeltaField = "paintDeltaMs";

protected:
    // 完整快照。基类只给最小内容，派生类覆盖。
    virtual juce::var buildSnapshot();

    // 桥就绪后每个定时器节拍调用一次（消息线程）。
    virtual void emitTick();

    // 桥函数入口：兜底面板显示时基类已统一回 not-ready，不会进到这里。
    // 基类处理 requestInitialState、setLang、setUiScale、commitUiScale，其余回 not-ready。
    virtual void handleBridgeCall(const juce::String& name, const juce::Array<juce::var>& args,
                                  NativeCompletion complete);

    void emitEvent(const char* eventId, const juce::var& payload);
    void emitState();
    void logDiag(const juce::String& line) const;

private:
    enum class FallbackReason
    {
        MissingRuntime,
        RuntimeTooOld,
        PageMissing,
        LoadTimeout,
        BootError
    };

    enum class NavState
    {
        notStarted,
        started,
        finished,
        networkError
    };

    class HostWebView;

    juce::WebBrowserComponent::Options makeOptions();
    juce::String entryUrl() const;
    void beginLoadAttempt();
    void showFallback(FallbackReason reason);
    void retryWebView();
    void resizeToDesignBox();
    void releaseReadyBridge();
    void applyRevealGate();
    void noteRevealed();
    void paintPlaceholder(juce::Graphics& g, juce::Rectangle<float> area) const;
    void dispatchBridgeCall(const juce::String& name, const juce::Array<juce::var>& args, NativeCompletion complete);
    juce::String buildDiagnostics(bool basenameOnly) const;

    void onNavigationStarted(const juce::String& url);
    void onNavigationFinished(const juce::String& url);
    void onNavigationError(const juce::String& errorInfo);
    void handleBootError(const juce::var& payload);
    void handleFirstFrame(const juce::var& payload);

    void timerCallback() override;

    Config config_;
    ResourceProvider provider_;
    bridge::UiScaleModel scale_;
    juce::String lang_;

    std::atomic<bool> bridgeReady_{false};
    juce::uint32 startMs_ = 0;
    juce::uint32 deadlineMs_ = 0;
    bool navBudgetApplied_ = false;
    NavState navState_ = NavState::notStarted;
    juce::String navDetail_;
    juce::String bootError_;
    PlatformWebView::RuntimeInfo runtime_;
    juce::File userDataFolder_;
    juce::String userDataFolderIssue_;
    bool countedAsReady_ = false;
    RevealGate revealGate_;
    bool revealLogged_ = false;

    std::unique_ptr<HostWebView> webView_;
    std::unique_ptr<FallbackPanel> fallback_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WebViewHost)
};
} // namespace miciri::webview
