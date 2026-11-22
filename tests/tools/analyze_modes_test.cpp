// SPDX-License-Identifier: GPL-3.0-or-later
//
// miciri-analyze 的各个 --mode：在合成录音上跑整条命令行（runCli），检查 JSON 里的关键字段和数值是否落在
// 合成真值附近，以及参数不对时退出码为 2。合成信号与 core 的测试同源，真值与容差也沿用那里的。
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "Cli.h"
#include "Modes.h"
#include "config/Thresholds.h"
#include "core/level/Synth.h"
#include "core/room/ClapSynth.h"
#include "core/voice/VoiceSynth.h"

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

// 32 bit 浮点 wav（FORMAT 3），声道交错
std::vector<unsigned char> makeWav(const std::vector<std::vector<double>>& channels, double rate)
{
    const auto count = static_cast<std::uint16_t>(channels.size());
    const std::size_t frames = channels[0].size();
    const auto dataBytes = static_cast<std::uint32_t>(frames * count * 4U);
    std::vector<unsigned char> b;
    const auto tag = [&b](const char* t) { b.insert(b.end(), t, t + 4); };
    tag("RIFF");
    put32(b, 36 + dataBytes);
    tag("WAVE");
    tag("fmt ");
    put32(b, 16);
    put16(b, 3);
    put16(b, count);
    put32(b, static_cast<std::uint32_t>(rate));
    put32(b, static_cast<std::uint32_t>(rate) * count * 4U);
    put16(b, static_cast<std::uint16_t>(count * 4U));
    put16(b, 32);
    tag("data");
    put32(b, dataBytes);
    for (std::size_t f = 0; f < frames; ++f)
        for (const auto& channel : channels)
        {
            const auto x = static_cast<float>(channel[f]);
            std::uint32_t raw = 0;
            std::memcpy(&raw, &x, sizeof raw);
            put32(b, raw);
        }
    return b;
}

// 以 bytes 当作任何路径下的文件内容，跑一遍命令行；args 不含文件名，文件名固定为 take.wav
tools::CliOutput run(std::vector<std::string> args, const std::vector<unsigned char>& bytes)
{
    args.emplace_back("take.wav");
    return tools::runCli(args, [&bytes](const std::string&, std::vector<unsigned char>& out) {
        out = bytes;
        return true;
    });
}

tools::CliOutput runMono(const std::vector<std::string>& args, const std::vector<double>& x, double rate)
{
    return run(args, makeWav({x}, rate));
}

// 从 from 起第一个 "key": 后面的数
double numberAt(const std::string& json, const std::string& key, std::size_t from = 0)
{
    const std::size_t at = json.find("\"" + key + "\":", from);
    REQUIRE(at != std::string::npos);
    return std::strtod(json.c_str() + at + key.size() + 3, nullptr);
}

// 从 from 起第一个 "key": 后面的数组，只认数（null 读成 NaN）
std::vector<double> arrayAt(const std::string& json, const std::string& key, std::size_t from = 0)
{
    const std::size_t at = json.find("\"" + key + "\":[", from);
    REQUIRE(at != std::string::npos);
    std::vector<double> values;
    const char* p = json.c_str() + at + key.size() + 4;
    while (*p != ']')
    {
        if (std::strncmp(p, "null", 4) == 0)
            values.push_back(std::nan(""));
        else
            values.push_back(std::strtod(p, nullptr));
        while (*p != ',' && *p != ']')
            ++p;
        if (*p == ',')
            ++p;
    }
    return values;
}

std::size_t sectionAt(const std::string& json, const std::string& key)
{
    const std::size_t at = json.find("\"" + key + "\":{");
    REQUIRE(at != std::string::npos);
    return at;
}

bool contains(const std::string& text, const std::string& needle)
{
    return text.find(needle) != std::string::npos;
}

std::size_t occurrences(const std::string& text, const std::string& needle)
{
    std::size_t n = 0;
    for (std::size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + needle.size()))
        ++n;
    return n;
}

// 括号配平、引号成对，且没有写成 nan 或 inf 的数值（非有限的数输出为 null）：不是完整的 JSON 校验，
// 但能抓住拼接出错和非有限数。只认冒号后面的写法，免得被 one-dominant 这样的枚举名误伤
bool looksValid(const std::string& text)
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
    return curly == 0 && square == 0 && !inString && !contains(text, "nan") && !contains(text, "inf") &&
           text.back() == '\n';
}

// ---- 合成录音，与 core 的测试同一批 ----

constexpr double kFs = 48000.0;

std::vector<double> sibilantTake(double fs, double vowelAmplitude)
{
    std::vector<double> x = whiteNoise(static_cast<std::size_t>(8.0 * fs), 3e-5, 21);
    for (int i = 0; i < 6; ++i)
    {
        const double t = 0.5 + 1.2 * i;
        addNoiseBurst(x, fs, t, 0.15, 0.05, 100 + static_cast<std::uint64_t>(i));
        addVowel(x, fs, t + 0.2, 0.3, 140.0, vowelAmplitude);
    }
    return x;
}

double bandDb(double rms, double loHz, double hiHz, double fs)
{
    return 10.0 * std::log10(rms * rms * (hiHz - loHz) / (fs / 2.0));
}

// 八个元音，之间是 -80 dBFS 的底噪；之后在停顿里加口水音式的尖峰
std::vector<double> speechTake(double fs)
{
    std::vector<double> x = whiteNoise(static_cast<std::size_t>(8.5 * fs), 1e-4, 51);
    for (int i = 0; i < 8; ++i)
        addVowel(x, fs, 0.5 + 1.0 * i, 0.3, 130.0 + 10.0 * i, 0.1, 0.03);
    return x;
}
} // namespace

TEST_CASE("level is the default mode and --mode level prints the same thing")
{
    const std::vector<double> x = voice(kFs, 6.0, fromDb(-22.0), 1, 1.0);
    const tools::CliOutput plain = runMono({}, x, kFs);
    REQUIRE(plain.exitCode == 0);
    CHECK(plain.err.empty());
    CHECK(looksValid(plain.out));
    CHECK(contains(plain.out, "\"noSignal\""));
    CHECK_FALSE(contains(plain.out, "\"mode\""));
    CHECK(runMono({"--mode", "level"}, x, kFs).out == plain.out);
    // --floor-dbfs 只属于 level，别的模式不接受
    CHECK(runMono({"--mode", "level", "--floor-dbfs", "-70"}, x, kFs).exitCode == 0);
}

TEST_CASE("room prints T20 per band, Tmid, the valid claps per band and every clap")
{
    ClapTrain c; // 八下拍手，T60 为 0.4 s
    const tools::CliOutput r = runMono({"--mode", "room"}, makeClaps(c), kFs);
    REQUIRE(r.exitCode == 0);
    CHECK(looksValid(r.out));
    CHECK(contains(r.out, "\"mode\":\"room\""));
    const std::size_t at = sectionAt(r.out, "reverb");
    const std::vector<double> t20 = arrayAt(r.out, "t20Sec", at);
    REQUIRE(t20.size() == 4);
    for (std::size_t b = 0; b < 4; ++b)
    {
        INFO("band " << b);
        CHECK(t20[b] == within(0.4, 0.08)); // 单次录音的随机起伏，core 的测试里取四段平均对 5%
    }
    CHECK(numberAt(r.out, "tMidSec", at) == within(0.4, 0.06));
    CHECK(numberAt(r.out, "clapCount", at) == 8.0);
    CHECK(arrayAt(r.out, "bandsHz", at) == std::vector<double>{500.0, 1000.0, 2000.0, 4000.0});
    for (double kept : arrayAt(r.out, "validClaps", at))
        CHECK(kept >= static_cast<double>(thr::reverb::repeat_min_kept));
    CHECK(contains(r.out, "\"gated\":false"));
    CHECK(contains(r.out, "\"status\":\"valid\""));
    CHECK(occurrences(r.out, "\"timeSec\"") == 8);
}

TEST_CASE("room with no claps asks for a retest and every number is finite")
{
    const tools::CliOutput r =
        runMono({"--mode", "room"}, whiteNoise(static_cast<std::size_t>(4.0 * kFs), 1e-4, 5), kFs);
    REQUIRE(r.exitCode == 0);
    CHECK(looksValid(r.out));
    CHECK(numberAt(r.out, "clapCount") == 0.0);
    CHECK(numberAt(r.out, "tMidSec") == 0.0);
    CHECK(contains(r.out, "\"level\":\"retest\""));
    CHECK(contains(r.out, "\"reason\":\"few-claps\""));
}

TEST_CASE("dr prints the direct-to-reverberant ratio, the lower-bound flag and every clap")
{
    ClapTrain c;
    c.claps = 5;
    c.tailDelayMs = 15.0; // 尾巴整个落在直达窗之后
    c.tailGain = 0.002;
    const double target = 8.0;
    c.direct = std::sqrt(tailEnergyFrom(c, 0.4, 0.0) * std::pow(10.0, target / 10.0));
    const tools::CliOutput r = runMono({"--mode", "dr"}, makeClaps(c), kFs);
    REQUIRE(r.exitCode == 0);
    CHECK(looksValid(r.out));
    CHECK(contains(r.out, "\"mode\":\"dr\""));
    const std::size_t at = sectionAt(r.out, "directRatio");
    CHECK(numberAt(r.out, "drDb", at) == within(target, 1.0));
    CHECK(contains(r.out, "\"lowerBoundOnly\":false"));
    CHECK(numberAt(r.out, "clapCount", at) == 5.0);
    CHECK(occurrences(r.out, "\"tailSnrDb\"") == 5);
    CHECK(contains(r.out, "\"usable\":true"));
}

TEST_CASE("plosive prints the worst transient ratio of the pass and one ratio per syllable")
{
    std::vector<double> pops(12, -200.0); // 这个字不喷
    const tools::CliOutput clean = runMono({"--mode", "plosive"}, wordList(kFs, pops), kFs);
    REQUIRE(clean.exitCode == 0);
    CHECK(looksValid(clean.out));
    CHECK(numberAt(clean.out, "events") == 12.0);
    CHECK(std::fabs(numberAt(clean.out, "worstRatioDb")) < 2.0);

    pops[7] = 10.0; // 第八个字上有一个比元音低频高 10 dB 的爆发
    const tools::CliOutput popped = runMono({"--mode", "plosive"}, wordList(kFs, pops), kFs);
    REQUIRE(popped.exitCode == 0);
    const std::size_t list = popped.out.find("\"eventList\":");
    REQUIRE(list != std::string::npos);
    CHECK(occurrences(popped.out.substr(list), "\"ratioDb\"") == 12);
    const double worst = numberAt(popped.out, "worstRatioDb");
    CHECK(worst > 6.0);
    CHECK(worst > numberAt(clean.out, "worstRatioDb") + 4.0);
    // 一遍的最大值就是各音节里最大的那个
    double biggest = -1e9;
    for (std::size_t at = list; (at = popped.out.find("\"ratioDb\":", at)) != std::string::npos; at += 10)
        biggest = std::max(biggest, numberAt(popped.out, "ratioDb", at));
    CHECK(worst == within(biggest, 1e-6));
    CHECK(contains(popped.out, "\"severity\":\"light\"")); // 不分档
}

TEST_CASE("sibilance prints the two band levels, the vowel reference and the bandwidth")
{
    const std::vector<double> x = sibilantTake(kFs, 0.3);
    const tools::CliOutput r = runMono({"--mode", "sibilance"}, x, kFs);
    REQUIRE(r.exitCode == 0);
    CHECK(looksValid(r.out));
    const std::size_t at = sectionAt(r.out, "sibilance");
    CHECK(numberAt(r.out, "events", at) == 6.0);
    CHECK(numberAt(r.out, "midHighDb", at) == within(bandDb(0.05, 3000.0, 6000.0, kFs), 1.0));
    CHECK(numberAt(r.out, "highDb", at) == within(bandDb(0.05, 6000.0, 12000.0, kFs), 1.0));
    CHECK(numberAt(r.out, "vowelEvents", at) > 0.0);
    CHECK(numberAt(r.out, "effectiveBandwidthHz", at) > 10000.0);
    CHECK(contains(r.out, "\"highBandDisabled\":false"));
    CHECK(occurrences(r.out, "\"hasVowel\"") == 6);

    // --gain-db 对所有模式生效：先加 6 dB，两段绝对电平都高 6 dB
    const tools::CliOutput louder = runMono({"--mode", "sibilance", "--gain-db", "6"}, x, kFs);
    REQUIRE(louder.exitCode == 0);
    CHECK(numberAt(louder.out, "midHighDb", sectionAt(louder.out, "sibilance")) ==
          within(numberAt(r.out, "midHighDb", at) + 6.0, 0.05));
    CHECK(numberAt(louder.out, "highDb", sectionAt(louder.out, "sibilance")) ==
          within(numberAt(r.out, "highDb", at) + 6.0, 0.05));
}

TEST_CASE("clicks prints the rate per minute, the loudest relative to speech and the count")
{
    const double tick = 0.05;
    std::vector<double> x = speechTake(kFs);
    const tools::CliOutput none = runMono({"--mode", "clicks"}, x, kFs);
    REQUIRE(none.exitCode == 0);
    CHECK(numberAt(none.out, "count") == 0.0);
    CHECK(numberAt(none.out, "perMinute") == 0.0);

    for (int i = 0; i < 5; ++i)
        addTick(x, kFs, 1.05 + 1.0 * i, tick);
    addTick(x, kFs, 6.05, 2.0 * tick);
    const tools::CliOutput r = runMono({"--mode", "clicks"}, x, kFs);
    REQUIRE(r.exitCode == 0);
    CHECK(looksValid(r.out));
    const std::size_t at = sectionAt(r.out, "click");
    CHECK(numberAt(r.out, "count", at) == 6.0);
    CHECK(numberAt(r.out, "analysedSeconds", at) == within(8.5, 1e-3));
    CHECK(numberAt(r.out, "perMinute", at) == within(6.0 / 8.5 * 60.0, 1e-3));
    // 最响的是加倍的那一下：高通后的峰值比 2 × tick 略低
    const double loudest = numberAt(r.out, "loudestDbfs", at);
    CHECK(loudest < 20.0 * std::log10(2.0 * tick));
    CHECK(loudest > 20.0 * std::log10(2.0 * tick) - 4.0);
    CHECK(numberAt(r.out, "loudestRelDb", at) > 0.0); // 比本人语音响
    CHECK(occurrences(r.out, "\"kind\":\"click\"") == 6);
}

TEST_CASE("direction prints the level and the brightness of one take, and --gain-db shifts the level")
{
    const std::vector<double> loud = voice(kFs, 4.0, 0.1, 3);
    std::vector<double> quiet = loud;
    for (double& v : quiet)
        v *= fromDb(-6.0);
    const tools::CliOutput a = runMono({"--mode", "direction"}, loud, kFs);
    const tools::CliOutput b = runMono({"--mode", "direction"}, quiet, kFs);
    REQUIRE(a.exitCode == 0);
    CHECK(looksValid(a.out));
    CHECK(contains(a.out, "\"active\":true"));
    CHECK(numberAt(a.out, "levelDb") - numberAt(b.out, "levelDb") == within(6.0, 0.01));
    CHECK(numberAt(a.out, "brightnessDb") == within(numberAt(b.out, "brightnessDb"), 0.01));
    const tools::CliOutput restored = runMono({"--mode", "direction", "--gain-db", "6"}, quiet, kFs);
    CHECK(numberAt(restored.out, "levelDb") == within(numberAt(a.out, "levelDb"), 0.01));

    // 数字静音：没有可比的电平，active 为 false，两个数是 0
    const tools::CliOutput silent = runMono({"--mode", "direction"}, std::vector<double>(48000, 0.0), kFs);
    CHECK(contains(silent.out, "\"active\":false"));
    CHECK(numberAt(silent.out, "levelDb") == 0.0);
}

TEST_CASE("every mode on digital silence exits 0 and prints only finite numbers")
{
    const std::vector<double> silence(static_cast<std::size_t>(3.0 * kFs), 0.0);
    for (const auto& entry : tools::kModes)
    {
        INFO(std::string(entry.first));
        const tools::CliOutput r = runMono({"--mode", std::string(entry.first)}, silence, kFs);
        CHECK(r.exitCode == 0);
        CHECK(r.err.empty());
        CHECK(looksValid(r.out));
    }
}

TEST_CASE("a stereo file is analysed on the channel the channel check picks, in every mode")
{
    const std::vector<double> take = voice(kFs, 4.0, 0.1, 3);
    const std::vector<double> floorOnly = whiteNoise(take.size(), 1e-5, 9);
    const tools::CliOutput onRight = run({"--mode", "direction"}, makeWav({floorOnly, take}, kFs));
    const tools::CliOutput mono = runMono({"--mode", "direction"}, take, kFs);
    REQUIRE(onRight.exitCode == 0);
    CHECK(contains(onRight.out, "\"channels\":2"));
    CHECK(contains(onRight.out, "\"channelCheck\""));
    CHECK(occurrences(onRight.out, "\"analysedChannel\":\"right\"") == 2); // 声道检查的判定，和根上选出的一路
    CHECK(numberAt(onRight.out, "levelDb") == within(numberAt(mono.out, "levelDb"), 1e-6));
    CHECK_FALSE(contains(mono.out, "channelCheck"));
    CHECK(contains(mono.out, "\"analysedChannel\":\"left\""));
}

TEST_CASE("bad arguments exit with 2 and print nothing on stdout")
{
    const std::vector<double> x = voice(kFs, 2.0, 0.1, 3);
    const std::vector<unsigned char> wav = makeWav({x}, kFs);
    const auto reader = [&wav](const std::string&, std::vector<unsigned char>& out) {
        out = wav;
        return true;
    };
    const std::vector<std::vector<std::string>> bad = {
        {"--mode", "tremolo", "take.wav"}, // 未知模式
        {"--mode", "take.wav"}, // 模式缺参数：把文件名当成了模式名
        {"take.wav", "--mode"}, // 模式缺参数：后面什么都没有
        {"--mode"},
        {"--mode", "room"}, // 没有文件
        {},
        {"--mode", "room", "a.wav", "b.wav"}, // 两个文件
        {"--mode", "room", "--floor-dbfs", "-60", "take.wav"}, // --floor-dbfs 只属于 level
        {"--gain-db", "loud", "take.wav"},
        {"--gain-db", "take.wav"},
        {"--bogus", "take.wav"},
    };
    for (const auto& args : bad)
    {
        INFO(args.size() << " args, first: " << (args.empty() ? "" : args[0]));
        const tools::CliOutput r = tools::runCli(args, reader);
        CHECK(r.exitCode == 2);
        CHECK(r.out.empty());
        CHECK_FALSE(r.err.empty());
    }
    // 文件打不开、不是 wav：也是 2
    CHECK(tools::runCli({"--mode", "room", "gone.wav"}, [](const std::string&, std::vector<unsigned char>&) {
              return false;
          }).exitCode == 2);
    CHECK(run({"--mode", "dr"}, std::vector<unsigned char>(64, 0x41)).exitCode == 2);
}

TEST_CASE("--help exits 0, lists every mode and says to share the numbers, never the audio")
{
    const tools::CliOutput r =
        tools::runCli({"--help"}, [](const std::string&, std::vector<unsigned char>&) { return false; });
    CHECK(r.exitCode == 0);
    CHECK(r.out.empty());
    for (const auto& entry : tools::kModes)
        CHECK(contains(r.err, "  " + std::string(entry.first) + " "));
    CHECK(contains(r.err, "never the audio"));
    CHECK(contains(r.err, "--gain-db"));
}
