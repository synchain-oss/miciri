// SPDX-License-Identifier: GPL-3.0-or-later
//
// 真页面在真 WebView 里完成启动：编辑器装上编进插件的那份页面资源，开窗后页面要走完
// “脚本运行、外壳画出、调到原生拿到快照、应用快照”，宿主探针记一次 uiReady，不切兜底面板。
// 页面在浏览器预览里用的是假桥，从不加载 JUCE 的前端桥脚本；只有这里经过真桥，少打包一个文件这类问题在这里会变红。
// 只在 Windows 上跑（WebView2）；没有显示器或没有 WebView2 运行时时 SKIP。macOS 的系统 WebKit 在宿主里人工验证。
#include <catch2/catch_test_macros.hpp>

#include <juce_gui_extra/juce_gui_extra.h>

#if JUCE_WINDOWS && defined(MICIRI_HAVE_WEB_ASSETS)
#include <memory>

#include <MiciriWebData.h>

#include "plugin/HostProbe.h"
#include "plugin/MiciriEditor.h"
#include "plugin/MiciriProcessor.h"
#include "plugin/PlatformWebView.h"
#include "plugin/UiDefaultsStore.h"

TEST_CASE("the real page finishes starting up through the real bridge", "[plugin][editor][boot]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
        SKIP("没有显示器，无法开窗");
    if (!miciri::webview::PlatformWebView::runtimeAvailable())
        SKIP("没有 WebView2 运行时");

    miciri::uidefaults::setStorageDirForTests(
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("miciri-editor-test-absent"));

    miciri::MiciriProcessor processor;
    processor.prepareToPlay(48000.0, 256);
    juce::AudioBuffer<float> audio(2, 256);
    audio.clear();
    juce::MidiBuffer midi;

    miciri::webview::WebViewHost::Config config;
    config.resourceSource = {MiciriWebData::namedResourceListSize, MiciriWebData::originalFilenames,
                             MiciriWebData::namedResourceList, &MiciriWebData::getNamedResource};

    const auto before = miciri::probe::counters().snapshot();
    auto editor = std::make_unique<miciri::MiciriEditor>(processor, config);
    editor->setVisible(true);
    editor->addToDesktop(0);

    // 冷启动的 WebView2 要几秒；看门狗 15 秒后会切兜底面板，所以等到页面就绪或兜底出现为止，最多 40 秒。
    constexpr int kSliceMs = 100;
    constexpr int kMaxWaitMs = 40000;
    for (int waited = 0; waited < kMaxWaitMs; waited += kSliceMs)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(kSliceMs);
        processor.processBlock(audio, midi);
        if (miciri::probe::counters().snapshot().uiReady > before.uiReady || editor->showingFallback())
            break;
    }
    const auto after = miciri::probe::counters().snapshot();

    CHECK_FALSE(editor->showingFallback());
    CHECK(editor->bridgeReady());
    CHECK(after.uiReady - before.uiReady == 1);
    CHECK(after.uiFallback == before.uiFallback);
    CHECK(after.bridgeRoundTrips - before.bridgeRoundTrips >= 1);

    editor.reset();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(kSliceMs);
}
#endif
