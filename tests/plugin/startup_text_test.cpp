// SPDX-License-Identifier: GPL-3.0-or-later
//
// 兜底面板文案跟随界面语言，启动各阶段写进诊断行的格式。
#include <catch2/catch_test_macros.hpp>

#include "plugin/StartupText.h"

using miciri::webview::FallbackKind;
using miciri::webview::StartupTrace;
using miciri::webview::WebEngine;

namespace
{
constexpr FallbackKind kAllKinds[] = {
    FallbackKind::missingRuntime, FallbackKind::runtimeTooOld, FallbackKind::pageMissing,  FallbackKind::bootError,
    FallbackKind::envNotStarted,  FallbackKind::navStalled,    FallbackKind::bridgeStalled};

bool hasCjk(const juce::String& s)
{
    for (const auto c : s)
        if (c >= 0x4e00 && c <= 0x9fff)
            return true;
    return false;
}
} // namespace

TEST_CASE("the fallback panel follows the UI language", "[plugin][startup]")
{
    CHECK(miciri::webview::isChineseUi("zh"));
    CHECK(miciri::webview::isChineseUi("zh-CN"));
    CHECK_FALSE(miciri::webview::isChineseUi("en"));
    CHECK_FALSE(miciri::webview::isChineseUi(""));

    for (const auto engine : {WebEngine::webView2, WebEngine::systemWebKit})
    {
        for (const auto kind : kAllKinds)
        {
            const auto zh = miciri::webview::fallbackText(kind, true, engine);
            const auto en = miciri::webview::fallbackText(kind, false, engine);
            INFO("kind " << static_cast<int>(kind) << " engine " << static_cast<int>(engine));
            CHECK(hasCjk(zh.message));
            CHECK(hasCjk(zh.retry));
            CHECK(hasCjk(zh.install));
            CHECK_FALSE(hasCjk(en.message));
            CHECK(en.retry == "Retry");
            CHECK(en.install == "Install WebView2 Runtime");
            CHECK(zh.message != en.message);
        }
    }
    // 每种原因各有自己的一句，不会两种原因共用同一段话
    for (const auto a : kAllKinds)
        for (const auto b : kAllKinds)
            if (a != b)
                CHECK(miciri::webview::fallbackText(a, true, WebEngine::webView2).message !=
                      miciri::webview::fallbackText(b, true, WebEngine::webView2).message);
    // 按引擎点名：WebView2 的文案提 WebView2，系统 WebKit 的提 WebKit
    CHECK(miciri::webview::fallbackText(FallbackKind::missingRuntime, true, WebEngine::webView2)
              .message.contains("WebView2"));
    CHECK(miciri::webview::fallbackText(FallbackKind::missingRuntime, false, WebEngine::systemWebKit)
              .message.contains("WebKit"));
}

TEST_CASE("the startup trace names every stage and shows the ones never reached", "[plugin][startup]")
{
    StartupTrace trace;
    CHECK(trace.line() == "script - > frame - > bridge - > ready -");

    CHECK(trace.note(StartupTrace::Stage::pageScript, 412));
    CHECK(trace.note(StartupTrace::Stage::firstFrame, 530));
    CHECK(trace.line() == "script 412 ms > frame 530 ms > bridge - > ready -");

    // 只记第一次：重复上报不改已经记下的时刻
    CHECK_FALSE(trace.note(StartupTrace::Stage::pageScript, 999));
    CHECK(trace.at(StartupTrace::Stage::pageScript) == 412);
    // 负数夹到 0（计时回绕或时钟抖动时不写出负的毫秒）
    CHECK(trace.note(StartupTrace::Stage::bridge, -5));
    CHECK(trace.at(StartupTrace::Stage::bridge) == 0);
    CHECK(trace.at(StartupTrace::Stage::uiReady) == -1);

    trace.reset();
    CHECK(trace.line() == "script - > frame - > bridge - > ready -");

    CHECK(StartupTrace::fromPageName("script") == StartupTrace::Stage::pageScript);
    CHECK(StartupTrace::fromPageName("ready") == StartupTrace::Stage::uiReady);
    // 首帧与桥就绪由原生侧自己记，页面不能冒充
    CHECK_FALSE(StartupTrace::fromPageName("bridge").has_value());
    CHECK_FALSE(StartupTrace::fromPageName("frame").has_value());
    CHECK_FALSE(StartupTrace::fromPageName("").has_value());
}
