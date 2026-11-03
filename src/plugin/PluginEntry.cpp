// SPDX-License-Identifier: GPL-3.0-or-later
//
// 插件入口：工厂函数，以及把编进二进制的页面资源交给编辑器。
// createEditor 单独放在这里，处理器的测试不必链 WebView。
#include <utility>

#include "MiciriEditor.h"
#include "MiciriProcessor.h"

#if MICIRI_HAVE_WEB_ASSETS
#include <MiciriWebData.h>
#endif

namespace miciri
{
juce::AudioProcessorEditor* MiciriProcessor::createEditor()
{
    webview::WebViewHost::Config config;
#if MICIRI_HAVE_WEB_ASSETS
    config.resourceSource = {MiciriWebData::namedResourceListSize, MiciriWebData::originalFilenames,
                             MiciriWebData::namedResourceList, &MiciriWebData::getNamedResource};
#endif
    return new MiciriEditor(*this, std::move(config));
}
} // namespace miciri

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new miciri::MiciriProcessor();
}
