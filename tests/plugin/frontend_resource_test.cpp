// SPDX-License-Identifier: GPL-3.0-or-later
//
// 真实宿主里页面要能取到 JUCE 的前端桥脚本：页面按相对页面根的 js/juce/index.js 加载它。
// 这个文件不在本仓里，构建时从 JUCE 检出复制并编进二进制；这里确认 ResourceProvider 按该路径取到的是非空的 JavaScript，
// 并且它 import 的同目录文件也都编进去了：少一个，整个模块加载失败，页面的桥调用一个都发不出去。
#include <catch2/catch_test_macros.hpp>

#include <regex>
#include <string>
#include <vector>

#include <MiciriJuceFrontendTestData.h>

#include "plugin/ResourceProvider.h"

namespace
{
miciri::webview::ResourceProvider frontendProvider()
{
    return miciri::webview::ResourceProvider(
        {MiciriJuceFrontendTestData::namedResourceListSize, MiciriJuceFrontendTestData::originalFilenames,
         MiciriJuceFrontendTestData::namedResourceList, &MiciriJuceFrontendTestData::getNamedResource});
}

// 脚本里的相对 import（静态 import、import ... from、export ... from），返回引号里的路径。
std::vector<std::string> relativeImports(const std::string& source)
{
    static const std::regex re(
        R"re((?:^|[;\s])(?:import|export)\s*(?:[\w*{}\s,]+\s*from\s*)?["'](\.{1,2}/[^"']+)["'])re");
    std::vector<std::string> out;
    for (auto it = std::sregex_iterator(source.begin(), source.end(), re); it != std::sregex_iterator(); ++it)
        out.push_back((*it)[1].str());
    return out;
}

// 相对 import 按引用方所在目录解析成页面路径。只处理 "./" 与 "../" 开头的写法。
juce::String resolveFrom(const juce::String& importer, const juce::String& spec)
{
    auto dir = importer.upToLastOccurrenceOf("/", false, false);
    auto rest = spec;
    while (true)
    {
        if (rest.startsWith("./"))
            rest = rest.substring(2);
        else if (rest.startsWith("../"))
        {
            rest = rest.substring(3);
            dir = dir.upToLastOccurrenceOf("/", false, false);
        }
        else
            break;
    }
    return dir.isEmpty() ? rest : dir + "/" + rest;
}
} // namespace

TEST_CASE("the JUCE frontend helper is served at js/juce/index.js", "[plugin][resources]")
{
    const auto provider = frontendProvider();

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

TEST_CASE("every relative import of the JUCE frontend helper is served too", "[plugin][resources]")
{
    const auto provider = frontendProvider();
    const auto hit = provider.lookup("js/juce/index.js");
    REQUIRE(hit.has_value());
    const auto imports = relativeImports(std::string(hit->data, static_cast<std::size_t>(hit->size)));
    // JUCE 8 的 index.js 至少 import 一个同目录文件；一个都没扫到说明扫描本身坏了，检查就是空转。
    REQUIRE_FALSE(imports.empty());
    for (const auto& spec : imports)
    {
        const auto path = resolveFrom("js/juce/index.js", juce::String(spec));
        INFO("js/juce/index.js imports " << spec << " -> " << path);
        CHECK(provider.lookup(path).has_value());
    }
}

TEST_CASE("the import scanner finds the forms the helper uses", "[plugin][resources]")
{
    CHECK(relativeImports("import \"./a.js\";\nimport { b } from './b.js';\nexport * from \"../c.js\";") ==
          std::vector<std::string>{"./a.js", "./b.js", "../c.js"});
    CHECK(relativeImports("const s = \"import './no.js'\"; import x from \"https://x/y.js\";").empty());
    CHECK(resolveFrom("js/juce/index.js", "./check_native_interop.js") == "js/juce/check_native_interop.js");
    CHECK(resolveFrom("js/juce/index.js", "../x.js") == "js/x.js");
}
