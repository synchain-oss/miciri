// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from synchain-oss/rinne; see upstream/plugin.json
//
// 开窗遮挡闸：WebView2 在首帧前会露出白底，所以先把 WebView 挪到可视区外，
// 等页面报告首帧（再压一拍合成）或超时才挪回来。纯状态机，不碰 JUCE 组件，便于单测。
#pragma once

#include <juce_graphics/juce_graphics.h>

#include <cstdint>

namespace miciri::webview
{
// 状态机的输入：开始导航、桥就绪、首帧信号、导航完成、定时器节拍、兜底面板出现；
// 输出：parked() 表示 WebView 此刻应在可视区外。
class RevealGate
{
public:
    static constexpr int kRevealFallbackMs = 3000;

    static constexpr int kRevealSettleTicks = 1;
    static constexpr int kRevealSettleMs = 64;

    void beginLoadAttempt() noexcept
    {
        parked_ = false;
        holding_ = false;
        revealed_ = false;
        settling_ = false;
        settleTicksSeen_ = 0;
        settleAtMs_ = 0;
        navFinishedSeen_ = false;
        revealReason_ = "";
    }

    void holdBeforeNavigation() noexcept
    {
        if (revealed_ || parked_)
            return;
        parked_ = true;
        holding_ = true;
    }

    void onNavigationStarted(std::uint32_t nowMs) noexcept
    {
        if (revealed_ || (parked_ && !holding_))
            return;
        parked_ = true;
        holding_ = false;
        parkedAtMs_ = nowMs;
    }

    bool onBridgeReady(std::uint32_t nowMs) noexcept
    {
        if (!holding_)
            return false;
        onNavigationStarted(nowMs);
        return true;
    }

    void onFirstFrame(std::uint32_t nowMs) noexcept
    {
        if (holding_)
            return;
        if (!parked_)
        {
            revealed_ = true;
            return;
        }
        if (!settling_)
        {
            settling_ = true;
            settleTicksSeen_ = 0;
            settleAtMs_ = nowMs;
        }
    }

    void onNavigationFinished() noexcept { navFinishedSeen_ = true; }

    void onTick(std::uint32_t nowMs) noexcept
    {
        if (!parked_ || holding_)
            return;
        if (settling_)
        {
            const bool ticksDone = (++settleTicksSeen_ >= kRevealSettleTicks);
            const bool msDone = (static_cast<std::int32_t>(nowMs - settleAtMs_) >= kRevealSettleMs);
            if (ticksDone && msDone)
                reveal("firstFrame");
            return;
        }
        if (static_cast<std::int32_t>(nowMs - parkedAtMs_) >= kRevealFallbackMs)
            reveal("timeout");
    }

    void onFallbackShown() noexcept { reveal("fallback"); }

    bool parked() const noexcept { return parked_; }

    bool holdingBeforeNavigation() const noexcept { return holding_; }

    const char* lastRevealReason() const noexcept { return revealReason_; }

    bool navigationFinishedSeen() const noexcept { return navFinishedSeen_; }

private:
    void reveal(const char* why) noexcept
    {
        revealed_ = true;
        settling_ = false;
        holding_ = false;
        if (!parked_)
            return;
        parked_ = false;
        revealReason_ = why;
    }

    bool parked_ = false;
    bool holding_ = false;
    bool revealed_ = false;
    bool settling_ = false;
    bool navFinishedSeen_ = false;
    int settleTicksSeen_ = 0;
    std::uint32_t settleAtMs_ = 0;
    std::uint32_t parkedAtMs_ = 0;
    const char* revealReason_ = "";
};

// 挪走时的位置：向右移两个窗口宽度，仍保持原尺寸（尺寸变了会让页面重排）。
inline juce::Rectangle<int> parkedBounds(juce::Rectangle<int> visible) noexcept
{
    const int step = juce::jmax(1, visible.getWidth());
    return visible.withX(visible.getX() + step * 2);
}

inline juce::String windowClosedLine(std::uint32_t nowMs, std::uint32_t startMs, const RevealGate& gate,
                                     bool gateEnabled, bool fallbackShown)
{
    const char* state = fallbackShown                    ? "fallback"
                        : !gateEnabled                   ? "reveal gate off"
                        : gate.holdingBeforeNavigation() ? "parked before navigation"
                        : gate.parked()                  ? "parked"
                                                         : "revealed";
    return "window closed after " + juce::String(static_cast<std::int32_t>(nowMs - startMs)) + " ms (" + state + ")";
}
} // namespace miciri::webview
