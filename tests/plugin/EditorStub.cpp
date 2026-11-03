// SPDX-License-Identifier: GPL-3.0-or-later
//
// 处理器测试用的 createEditor 空实现：真实的 createEditor 在 PluginEntry.cpp 里，会把整套 WebView 拖进来。
#include "plugin/MiciriProcessor.h"

namespace miciri
{
juce::AudioProcessorEditor* MiciriProcessor::createEditor()
{
    return nullptr;
}
} // namespace miciri
