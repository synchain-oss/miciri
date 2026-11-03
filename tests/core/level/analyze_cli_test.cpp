// SPDX-License-Identifier: GPL-3.0-or-later
//
// 命令行工具的库部分：wav 解析（各种位深、浮点、截断、坏文件）和 JSON 报告的形状。
// 字节都在内存里造，不读写文件。
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <string>

#include "Report.h"
#include "Wav.h"
#include "core/level/Synth.h"

using namespace miciri;
using namespace miciri::testing;

namespace
{
void put32(std::vector<unsigned char>& b, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        b.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xFFU));
}

void put16(std::vector<unsigned char>& b, std::uint16_t v)
{
    b.push_back(static_cast<unsigned char>(v & 0xFFU));
    b.push_back(static_cast<unsigned char>((v >> 8) & 0xFFU));
}

void putTag(std::vector<unsigned char>& b, const char* tag)
{
    b.insert(b.end(), tag, tag + 4);
}

// bits 为 16、24 或 32 时是整数 PCM，bits 为 -32 时是 32 bit 浮点
std::vector<unsigned char> makeWav(const std::vector<std::vector<double>>& channels, std::uint32_t rate, int bits)
{
    const bool floating = bits < 0;
    const auto width = static_cast<std::uint16_t>(floating ? 32 : bits);
    const auto count = static_cast<std::uint16_t>(channels.size());
    const std::size_t frames = channels[0].size();
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(frames * count * (width / 8U));
    std::vector<unsigned char> b;
    putTag(b, "RIFF");
    put32(b, 36 + dataBytes);
    putTag(b, "WAVE");
    putTag(b, "fmt ");
    put32(b, 16);
    put16(b, floating ? 3 : 1);
    put16(b, count);
    put32(b, rate);
    put32(b, rate * count * (width / 8U));
    put16(b, static_cast<std::uint16_t>(count * (width / 8U)));
    put16(b, width);
    putTag(b, "data");
    put32(b, dataBytes);
    for (std::size_t f = 0; f < frames; ++f)
    {
        for (const auto& channel : channels)
        {
            const double v = channel[f];
            if (floating)
            {
                const float x = static_cast<float>(v);
                std::uint32_t raw;
                std::memcpy(&raw, &x, sizeof raw);
                put32(b, raw);
            }
            else if (width == 16)
            {
                put16(b, static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(v * 32767.0))));
            }
            else if (width == 24)
            {
                const auto raw = static_cast<std::uint32_t>(static_cast<std::int32_t>(std::lround(v * 8388607.0)));
                b.push_back(static_cast<unsigned char>(raw & 0xFFU));
                b.push_back(static_cast<unsigned char>((raw >> 8) & 0xFFU));
                b.push_back(static_cast<unsigned char>((raw >> 16) & 0xFFU));
            }
            else
            {
                put32(b, static_cast<std::uint32_t>(static_cast<std::int32_t>(std::lround(v * 2147483647.0))));
            }
        }
    }
    return b;
}

bool contains(const std::string& text, const char* needle)
{
    return text.find(needle) != std::string::npos;
}

// 括号配平、引号成对：不是完整的 JSON 校验，但能抓住拼接出错
bool looksBalanced(const std::string& text)
{
    int curly = 0;
    int square = 0;
    bool inString = false;
    for (char c : text)
    {
        if (c == '"')
            inString = !inString;
        if (inString)
            continue;
        curly += (c == '{') - (c == '}');
        square += (c == '[') - (c == ']');
        if (curly < 0 || square < 0)
            return false;
    }
    return curly == 0 && square == 0 && !inString;
}
} // namespace

TEST_CASE("wav: integer and float formats are read back to within their resolution")
{
    const std::vector<double> left = sine(48000.0, 440.0, 0.5, 0.1);
    const std::vector<double> right = sine(48000.0, 660.0, 0.25, 0.1);
    struct Case
    {
        int bits;
        double tolerance;
    };
    for (const Case c : {Case{16, 1.0 / 32768.0}, Case{24, 1.0 / 8388608.0}, Case{32, 1e-9}, Case{-32, 1e-7}})
    {
        INFO("bits " << c.bits);
        tools::WavData wav;
        std::string error;
        REQUIRE(tools::parseWav(makeWav({left, right}, 48000, c.bits), wav, error));
        CHECK(wav.sampleRate == 48000.0);
        REQUIRE(wav.channels.size() == 2);
        REQUIRE(wav.channels[0].size() == left.size());
        for (std::size_t i = 0; i < left.size(); i += 17)
        {
            CHECK(wav.channels[0][i] == within(left[i], 2.0 * c.tolerance));
            CHECK(wav.channels[1][i] == within(right[i], 2.0 * c.tolerance));
        }
    }
}

TEST_CASE("wav: bad input is refused with a reason, a truncated data chunk is read as far as it goes")
{
    tools::WavData wav;
    std::string error;
    CHECK_FALSE(tools::parseWav({}, wav, error));
    CHECK_FALSE(error.empty());

    std::vector<unsigned char> notWav(64, 0x41);
    CHECK_FALSE(tools::parseWav(notWav, wav, error));

    // 没有 data 块
    std::vector<unsigned char> noData = makeWav({sine(48000.0, 440.0, 0.5, 0.01)}, 48000, 16);
    noData.resize(36);
    error.clear();
    CHECK_FALSE(tools::parseWav(noData, wav, error));
    CHECK_FALSE(error.empty());

    // 不支持的格式标签（ADPCM）
    std::vector<unsigned char> adpcm = makeWav({sine(48000.0, 440.0, 0.5, 0.01)}, 48000, 16);
    adpcm[20] = 2;
    error.clear();
    CHECK_FALSE(tools::parseWav(adpcm, wav, error));
    CHECK(error.find("unsupported") != std::string::npos);

    // 截断：声明的长度比实际多
    std::vector<unsigned char> cut = makeWav({sine(48000.0, 440.0, 0.5, 0.1)}, 48000, 16);
    cut.resize(cut.size() - 1001);
    REQUIRE(tools::parseWav(cut, wav, error));
    CHECK(wav.channels[0].size() == 4800 - 501);
}

TEST_CASE("report: a stereo recording gets every section, in the wire names")
{
    const double fs = 48000.0;
    std::vector<double> left = voice(fs, 7.0, fromDb(-22.0), 1, 1.0);
    mix(left, whiteNoise(left.size(), fromDb(-70.0), 2));
    std::vector<double> right = voice(fs, 7.0, fromDb(-22.0), 3, 1.0);
    mix(right, whiteNoise(right.size(), fromDb(-70.0), 4));
    tools::WavData wav;
    std::string error;
    REQUIRE(tools::parseWav(makeWav({left, right}, 48000, -32), wav, error));

    const std::string json = tools::analyseToJson(wav);
    CHECK(looksBalanced(json));
    for (const char* key : {"\"sampleRate\"", "\"channelCheck\"", "\"noSignal\"", "\"gain\"", "\"noise\"", "\"hum\"",
                            "\"deviceProcessing\"", "\"measures\"", "\"verdict\"", "\"confidence\"", "\"evidence\"",
                            "\"truePeakDbfsL\"", "\"snrDb\"", "\"fundamentalHz\"", "\"longestZeroMs\""})
        CHECK(contains(json, key));
    CHECK(contains(json, "\"kind\":\"true-stereo\""));
    CHECK(contains(json, "\"state\":\"ok\""));
    CHECK_FALSE(contains(json, "nan"));
    CHECK_FALSE(contains(json, "inf"));
    CHECK(json.back() == '\n');
}

TEST_CASE("report: a mono recording has no channel check and says what it found")
{
    const double fs = 44100.0;
    std::vector<double> x = humSignal(fs, 8.0, 50.0, kHumProfileDb, fromDb(-50.0) * fromDb(-50.0));
    mix(x, whiteNoise(x.size(), fromDb(-60.0), 5));
    tools::WavData wav;
    std::string error;
    REQUIRE(tools::parseWav(makeWav({x}, 44100, 24), wav, error));

    const std::string json = tools::analyseToJson(wav);
    CHECK(looksBalanced(json));
    CHECK_FALSE(contains(json, "\"channelCheck\""));
    CHECK(contains(json, "\"channels\":1"));
    CHECK(contains(json, "\"kind\":\"hum\""));
}

TEST_CASE("report: strictly silent audio is reported, not rejected")
{
    const std::vector<double> zeros(static_cast<std::size_t>(2.0 * 48000.0), 0.0);
    tools::WavData wav;
    std::string error;
    REQUIRE(tools::parseWav(makeWav({zeros}, 48000, 16), wav, error));
    const std::string json = tools::analyseToJson(wav);
    CHECK(looksBalanced(json));
    CHECK(contains(json, "\"state\":\"all-zero-from-start\""));
    CHECK_FALSE(contains(json, "nan"));
}
