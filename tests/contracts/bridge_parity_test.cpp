// SPDX-License-Identifier: GPL-3.0-or-later
//
// 桥契约对拍：web/shared/bridge-api.js 与 src/plugin/BridgeApi.h 必须逐项相同（表名、每项内容、顺序、版本号），
// 文档 docs/contracts/BRIDGE.md 必须提到每个函数、事件与失败原因。JS 文件按它自己声明的字面量文法读，不执行。
// 后半段是“改坏必须变红”：对两边各做若干种破坏，对拍必须报出差异；破坏没被发现就是对拍本身坏了。
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <utility>
#include "plugin/BridgeApi.h"

using Tables = std::map<std::string, std::vector<std::string>>;

struct JsContract
{
    Tables tables;
    int exports = 0;
    int malformed = 0;
};

static std::string readFile(const char* path)
{
    std::ifstream in(path, std::ios::binary);
    CHECK(in.good());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

static std::string stripLineComments(const std::string& text)
{
    std::istringstream in(text);
    std::string line;
    std::string out;
    while (std::getline(in, line))
    {
        const auto c = line.find("//");
        if (c != std::string::npos)
            line.erase(c);
        out += line;
        out += '\n';
    }
    return out;
}

static bool wireChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-';
}

static JsContract parseJs(const std::string& raw)
{
    const std::string t = stripLineComments(raw);
    JsContract js;
    const std::string kw = "export const ";
    for (std::size_t at = t.find("export "); at != std::string::npos; at = t.find("export ", at + 1))
        ++js.exports;
    int parsed = 0;
    std::size_t pos = 0;
    while ((pos = t.find(kw, pos)) != std::string::npos)
    {
        ++parsed;
        pos += kw.size();
        const auto eq = t.find(" = ", pos);
        if (eq == std::string::npos)
        {
            ++js.malformed;
            break;
        }
        const std::string name = t.substr(pos, eq - pos);
        const std::size_t v = eq + 3;
        std::vector<std::string> items;
        if (v < t.size() && t[v] == '[')
        {
            const auto end = t.find("];", v);
            if (end == std::string::npos)
            {
                ++js.malformed;
                break;
            }
            for (std::size_t i = v + 1; i < end;)
            {
                if (t[i] == '"')
                {
                    const auto close = t.find('"', i + 1);
                    if (close == std::string::npos || close > end)
                    {
                        ++js.malformed;
                        break;
                    }
                    items.push_back(t.substr(i + 1, close - i - 1));
                    if (items.back().empty() || !std::all_of(items.back().begin(), items.back().end(), wireChar))
                        ++js.malformed;
                    i = close + 1;
                }
                else
                {
                    if (!std::isspace(static_cast<unsigned char>(t[i])) && t[i] != ',')
                        ++js.malformed;
                    ++i;
                }
            }
            pos = end + 2;
        }
        else
        {
            const auto end = t.find(';', v);
            if (end == std::string::npos)
            {
                ++js.malformed;
                break;
            }
            items.push_back(t.substr(v, end - v));
            if (items.back().empty() || !std::all_of(items.back().begin(), items.back().end(), [](char c) {
                    return std::isdigit(static_cast<unsigned char>(c)) != 0;
                }))
                ++js.malformed;
            pos = end + 1;
        }
        if (!js.tables.emplace(name, items).second)
            ++js.malformed; // 重名
    }
    if (parsed != js.exports) // 有 export 不是 `export const`，或写法不在文法里
        ++js.malformed;
    return js;
}

static Tables cppTables()
{
    Tables t;
    const auto tables = miciri::bridge::contractTables();
    for (const auto& tb : tables)
        t[std::string(tb.name)] = std::vector<std::string>(tb.data, tb.data + tb.size);
    CHECK(t.size() == tables.size()); // 登记表里没有重名
    t["BRIDGE_API_VERSION"] = {std::to_string(miciri::bridge::kApiVersion)};
    return t;
}

static std::vector<std::string> diff(const Tables& js, const Tables& cpp)
{
    std::vector<std::string> out;
    for (const auto& kv : js)
    {
        const auto it = cpp.find(kv.first);
        if (it == cpp.end())
            out.push_back("only in JS: " + kv.first);
        else if (it->second != kv.second)
            out.push_back("different: " + kv.first);
    }
    for (const auto& kv : cpp)
        if (!js.count(kv.first))
            out.push_back("only in C++: " + kv.first);
    return out;
}

static std::string replaceOnce(std::string text, const std::string& from, const std::string& to)
{
    const auto at = text.find(from);
    CHECK(at != std::string::npos); // 找不到说明这条破坏是空转，测试本身失效
    if (at != std::string::npos)
        text.replace(at, from.size(), to);
    return text;
}

static void mustBeRed(const char* what, const Tables& js, const Tables& cpp)
{
    if (diff(js, cpp).empty())
        std::fprintf(stderr, "mutation not detected: %s\n", what);
    CHECK(!diff(js, cpp).empty());
}

TEST_CASE("bridge contract: JS and C++ agree, and breaking either side turns it red")
{
    const std::string jsText = readFile("web/shared/bridge-api.js");
    const JsContract js = parseJs(jsText);
    const Tables cpp = cppTables();

    // 一致性：两边逐项相同，JS 文件全部落在文法里
    CHECK(js.malformed == 0);
    CHECK(js.exports == static_cast<int>(js.tables.size()));
    for (const auto& d : diff(js.tables, cpp))
        std::fprintf(stderr, "bridge contract mismatch: %s\n", d.c_str());
    CHECK(diff(js.tables, cpp).empty());

    // 文档里提到每个函数、事件与失败原因
    const std::string doc = readFile("docs/contracts/BRIDGE.md");
    for (const char* table : {"BRIDGE_FUNCTIONS", "BRIDGE_EVENTS", "BRIDGE_REASONS"})
        for (const auto& name : cpp.at(table))
            if (doc.find(name) == std::string::npos)
            {
                std::fprintf(stderr, "docs/contracts/BRIDGE.md does not mention %s\n", name.c_str());
                CHECK(false);
            }

    // 改坏必须变红：改 JS 文本再解析
    mustBeRed("rename an event", parseJs(replaceOnce(jsText, "\"miciri.meters\"", "\"miciri.metres\"")).tables, cpp);
    mustBeRed("rename an enum value",
              parseJs(replaceOnce(jsText, "\"all-zero-from-start\"", "\"all-zero-at-start\"")).tables, cpp);
    mustBeRed("bump the version",
              parseJs(replaceOnce(jsText, "BRIDGE_API_VERSION = 1;", "BRIDGE_API_VERSION = 2;")).tables, cpp);
    mustBeRed("append a function", parseJs(replaceOnce(jsText, "\"showLicense\"", "\"showLicense\", \"extra\"")).tables,
              cpp);
    CHECK(parseJs(replaceOnce(jsText, "export const BRIDGE_EVENTS = [", "export const BRIDGE_EVENTS = {")).malformed >
          0);
    CHECK(parseJs(jsText + "\nexport function f() {}\n").malformed > 0);

    // 改坏必须变红：直接改两边解析出来的表
    Tables m = js.tables;
    m["RESULT_GAIN"].erase(m["RESULT_GAIN"].begin());
    mustBeRed("JS drops a snapshot field", m, cpp);
    m = js.tables;
    std::swap(m["ENUM_TONE"][1], m["ENUM_TONE"][2]);
    mustBeRed("JS reorders an enum", m, cpp);
    m = js.tables;
    m.erase("SNAPSHOT_STEP");
    mustBeRed("JS loses a table", m, cpp);
    m = js.tables;
    m["EXTRA_TABLE"] = {"x"};
    mustBeRed("JS gains a table", m, cpp);
    m = cpp;
    m["BRIDGE_FUNCTIONS"][0] = "requestState";
    mustBeRed("C++ renames a function", js.tables, m);
    m = cpp;
    m["ENUM_RETEST_REASON"].pop_back();
    mustBeRed("C++ drops an enum value", js.tables, m);
    m = cpp;
    std::swap(m["METER_FIELDS"][0], m["METER_FIELDS"][1]);
    mustBeRed("C++ reorders meter fields", js.tables, m);
}
