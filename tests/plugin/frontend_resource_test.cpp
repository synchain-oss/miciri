// SPDX-License-Identifier: GPL-3.0-or-later
//
// 真实宿主里页面要能取到 JUCE 的前端桥脚本：页面按相对页面根的 js/juce/index.js 加载它。
// 这个文件不在本仓里，构建时从 JUCE 检出复制并编进二进制；这里确认 ResourceProvider 按该路径取到的是非空的 JavaScript。
#include <catch2/catch_test_macros.hpp>

#include <string>

#include <MiciriJuceFrontendTestData.h>

#include "plugin/ResourceProvider.h"

TEST_CASE("the JUCE frontend helper is served at js/juce/index.js", "[plugin][resources]")
{
    const miciri::webview::ResourceProvider provider(
        {MiciriJuceFrontendTestData::namedResourceListSize, MiciriJuceFrontendTestData::originalFilenames,
         MiciriJuceFrontendTestData::namedResourceList, &MiciriJuceFrontendTestData::getNamedResource});

    const auto hit = provider.lookup("js/juce/index.js");
    REQUIRE(hit.has_value());
    REQUIRE(hit->data != nullptr);
    CHECK(hit->size > 0);
    CHECK(juce::String(hit->mime).contains("javascript"));

    // 前端桥脚本导出的入口函数；内容对了才说明取到的是 JUCE 的那份，而不是空壳。
    const std::string text(hit->data, static_cast<std::size_t>(hit->size));
    CHECK(text.find("getNativeFunction") != std::string::npos);

    // 带资源根前缀的地址与不存在的路径。
    CHECK(provider.lookup("/js/juce/index.js").has_value());
    CHECK_FALSE(provider.lookup("js/juce/missing.js").has_value());
}
