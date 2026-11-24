// SPDX-License-Identifier: GPL-3.0-or-later
//
// 真页面在真 WebView 里完成启动：编辑器装上编进插件的那份页面资源，开窗后页面要走完
// “脚本运行、外壳画出、调到原生拿到快照、应用快照”，宿主探针记一次 uiReady，不切兜底面板。
// 页面在浏览器预览里用的是假桥，从不加载 JUCE 的前端桥脚本；只有这里经过真桥，少打包一个文件这类问题在这里会变红。
// 另有两个反例：把外壳的启动脚本换成“应用快照后抛错”与“应用快照后不报 ready”，桥已经调通、界面却没起来，
// 宿主必须切兜底面板，诊断里的阶段停在 bridge 之后、ready 之前，uiReady 不计数。
// 只在 Windows 上跑（WebView2）；没有显示器或没有 WebView2 运行时时 SKIP。macOS 的系统 WebKit 在宿主里人工验证。
#include <catch2/catch_test_macros.hpp>

#include <juce_gui_extra/juce_gui_extra.h>

#if JUCE_WINDOWS && defined(MICIRI_HAVE_WEB_ASSETS)
#include <memory>
#include <string>

#include <MiciriWebData.h>

#include "plugin/HostProbe.h"
#include "plugin/MiciriEditor.h"
#include "plugin/MiciriProcessor.h"
#include "plugin/PlatformWebView.h"
#include "plugin/UiDefaultsStore.h"

namespace
{
using miciri::webview::StartupTrace;

// 外壳启动脚本的替换：from 换成 to，其余资源原样。函数指针接口拿不到闭包，所以用文件内的静态量。
struct ShellPatch
{
    juce::String from;
    juce::String to;
    int hits = 0;
    std::string text;
};
ShellPatch gPatch;

const char* patchedResource(const char* name, int& size)
{
    const char* data = MiciriWebData::getNamedResource(name, size);
    const char* original = MiciriWebData::getNamedResourceOriginalFilename(name);
    if (data == nullptr || original == nullptr || !juce::String(original).endsWith("__shell-main.js"))
        return data;
    // 替换后的文本每个用例只生成一次，之后复用同一块缓冲：先前返回给 WebView 的指针一直有效
    if (gPatch.text.empty())
    {
        const auto source = juce::String::fromUTF8(data, size);
        if (!source.contains(gPatch.from))
            return data;
        gPatch.text = source.replace(gPatch.from, gPatch.to).toStdString();
    }
    ++gPatch.hits;
    size = static_cast<int>(gPatch.text.size());
    return gPatch.text.data();
}

struct BootRun
{
    bool fallback = false;
    bool bridgeReady = false;
    bool uiReady = false;
    int uiReadyDelta = 0;
    int uiFallbackDelta = 0;
    int roundTripDelta = 0;
    int bridgeMs = -1;
    int readyMs = -1;
    juce::String stages;
};

// 开一个编辑器，等到页面报告启动完成或兜底面板出现，最多 maxWaitMs。
BootRun runEditor(bool patched, int maxWaitMs)
{
    miciri::uidefaults::setStorageDirForTests(
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("miciri-editor-test-absent"));

    miciri::MiciriProcessor processor;
    processor.prepareToPlay(48000.0, 256);
    juce::AudioBuffer<float> audio(2, 256);
    audio.clear();
    juce::MidiBuffer midi;

    miciri::webview::WebViewHost::Config config;
    config.resourceSource = {MiciriWebData::namedResourceListSize, MiciriWebData::originalFilenames,
                             MiciriWebData::namedResourceList,
                             patched ? &patchedResource : &MiciriWebData::getNamedResource};

    const auto before = miciri::probe::counters().snapshot();
    auto editor = std::make_unique<miciri::MiciriEditor>(processor, config);
    editor->setVisible(true);
    editor->addToDesktop(0);

    constexpr int kSliceMs = 100;
    for (int waited = 0; waited < maxWaitMs; waited += kSliceMs)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(kSliceMs);
        processor.processBlock(audio, midi);
        if (editor->uiReady() || editor->showingFallback())
            break;
    }
    const auto after = miciri::probe::counters().snapshot();

    BootRun run;
    run.fallback = editor->showingFallback();
    run.bridgeReady = editor->bridgeReady();
    run.uiReady = editor->uiReady();
    run.uiReadyDelta = static_cast<int>(after.uiReady - before.uiReady);
    run.uiFallbackDelta = static_cast<int>(after.uiFallback - before.uiFallback);
    run.roundTripDelta = static_cast<int>(after.bridgeRoundTrips - before.bridgeRoundTrips);
    run.bridgeMs = editor->startupTrace().at(StartupTrace::Stage::bridge);
    run.readyMs = editor->startupTrace().at(StartupTrace::Stage::uiReady);
    run.stages = editor->startupTrace().line();

    editor.reset();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(kSliceMs);
    return run;
}

bool canOpenWebView()
{
    return !juce::Desktop::getInstance().getDisplays().displays.isEmpty() &&
           miciri::webview::PlatformWebView::runtimeAvailable();
}
} // namespace

TEST_CASE("the real page finishes starting up through the real bridge", "[plugin][editor][boot]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (!canOpenWebView())
        SKIP("没有显示器或没有 WebView2 运行时，无法开窗");

    // 冷启动的 WebView2 要几秒；看门狗 15 秒后会切兜底面板，所以等到页面就绪或兜底出现为止，最多 40 秒。
    const auto run = runEditor(false, 40000);
    INFO(run.stages);
    CHECK_FALSE(run.fallback);
    CHECK(run.bridgeReady);
    CHECK(run.uiReady);
    CHECK(run.uiReadyDelta == 1);
    CHECK(run.uiFallbackDelta == 0);
    CHECK(run.roundTripDelta >= 1);
    CHECK(run.readyMs >= run.bridgeMs);
}

TEST_CASE("an error while applying the first snapshot switches to the fallback panel", "[plugin][editor][boot]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (!canOpenWebView())
        SKIP("没有显示器或没有 WebView2 运行时，无法开窗");

    gPatch = {"shell.setState(snap);", "shell.setState(snap); throw new Error(\"snapshot apply failed in test\");"};
    // 报错会立刻切兜底面板，远早于看门狗；等 25 秒只是给冷启动留足余量，修复前这里会一直等到底。
    const auto run = runEditor(true, 25000);
    INFO(run.stages);
    REQUIRE(gPatch.hits >= 1);
    CHECK(run.fallback);
    CHECK(run.bridgeMs >= 0);
    CHECK(run.readyMs == -1);
    CHECK(run.stages.endsWith("ready -"));
    CHECK_FALSE(run.uiReady);
    CHECK(run.uiReadyDelta == 0);
    CHECK(run.uiFallbackDelta == 1);
}

TEST_CASE("the watchdog keeps running until the page reports it is ready", "[plugin][editor][boot]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (!canOpenWebView())
        SKIP("没有显示器或没有 WebView2 运行时，无法开窗");

    // 页面拿到快照却始终不报 ready、也不报错：只有看门狗能发现（冷启动 15 秒，导航开始后至少再给 5 秒）。
    gPatch = {"signal.ready();", "void 0;"};
    const auto run = runEditor(true, 40000);
    INFO(run.stages);
    REQUIRE(gPatch.hits >= 1);
    CHECK(run.fallback);
    CHECK(run.bridgeMs >= 0);
    CHECK(run.readyMs == -1);
    CHECK_FALSE(run.uiReady);
    CHECK(run.uiReadyDelta == 0);
    CHECK(run.uiFallbackDelta == 1);
}
#endif
