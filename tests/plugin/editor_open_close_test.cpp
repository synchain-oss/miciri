// SPDX-License-Identifier: GPL-3.0-or-later
//
// 编辑器反复开关 20 次不崩。每一轮都建编辑器、挂到桌面、跑一会儿消息循环（同时让处理器收几个音频块），再销毁。
// 没有页面资源，编辑器会显示兜底面板，这条路径同样要求不崩。
// 需要能开窗的环境：没有显示器时本测试 SKIP（退出码 4，ctest 登记为跳过）。CI 的 Windows 与 macOS 运行器可以开窗；
// 真实宿主里的开关、缩放与滚轮由人工在宿主里验证。
#include <catch2/catch_test_macros.hpp>

#include <memory>

#include <juce_gui_extra/juce_gui_extra.h>

#include "plugin/HostProbe.h"
#include "plugin/MiciriEditor.h"
#include "plugin/MiciriProcessor.h"
#include "plugin/UiDefaultsStore.h"

namespace miciri
{
juce::AudioProcessorEditor* MiciriProcessor::createEditor()
{
    return new MiciriEditor(*this, {});
}
} // namespace miciri

TEST_CASE("the editor opens and closes 20 times without crashing", "[plugin][editor]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
        SKIP("没有显示器，无法开窗");

    // 指向一个不存在的目录：只读不写，不碰用户真实的设置文件。
    miciri::uidefaults::setStorageDirForTests(
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("miciri-editor-test-absent"));

    miciri::MiciriProcessor processor;
    processor.prepareToPlay(48000.0, 256);
    juce::AudioBuffer<float> audio(2, 256);
    audio.clear();
    juce::MidiBuffer midi;

    const auto probeBefore = miciri::probe::counters().snapshot();

    constexpr int kCycles = 20;
    constexpr int kOpenMs = 60;
    constexpr int kSettleMs = 20;
    for (int i = 0; i < kCycles; ++i)
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        REQUIRE(editor != nullptr);
        editor->setVisible(true);
        editor->addToDesktop(0);
        processor.processBlock(audio, midi);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(kOpenMs);
        processor.processBlock(audio, midi);
        editor.reset();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(kSettleMs);
    }
    // 本目标带探针：每次建编辑器记一次打开，销毁记一次关闭。
    const auto probeAfter = miciri::probe::counters().snapshot();
    CHECK(probeAfter.editorOpens - probeBefore.editorOpens == kCycles);
    CHECK(probeAfter.editorCloses - probeBefore.editorCloses == kCycles);
    SUCCEED("20 次开关完成");
}
