// SPDX-License-Identifier: GPL-3.0-or-later
//
// 宿主探针的核心逻辑：全 0 判断、离线与否的分类、JSON 序列化、原子写文件、环境变量开关。
// 这些类不依赖宿主，也不依赖 MICIRI_HOST_PROBE，所以默认（OFF）的构建里同样编译、同样运行，探针不会悄悄烂掉。
// JSON 用 JUCE 的解析器核对（只在测试里用，探针本身不依赖 JUCE）。
#include <catch2/catch_approx.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "HostProbeTestSupport.h"
#include "plugin/HostProbe.h"

namespace
{
namespace fs = std::filesystem;
namespace probe = miciri::probe;
using namespace miciri::probe_test;

const char* const kFieldNames[] = {
    "schema",       "pid",          "sampleRate",        "maxBlockSize",     "numInputChannels", "numOutputChannels",
    "prepareCount", "blocksTotal",  "blocksNonRealtime", "blocksRealtime",   "blocksAllZero",    "blocksNonZero",
    "editorOpens",  "editorCloses", "analysisCount",     "lastIsNonRealtime"};
} // namespace

TEMPLATE_TEST_CASE("isAllZero: plus and minus zero are silence, anything else is content", "[plugin][probe]", float,
                   double)
{
    using Sample = TestType;
    constexpr int kSamples = 37;
    std::vector<Sample> left(kSamples, Sample(0));
    std::vector<Sample> right(kSamples, Sample(0));
    auto check = [&] {
        const Sample* chans[] = {left.data(), right.data()};
        return probe::isAllZero(chans, 2, kSamples);
    };

    SECTION("all +0")
    {
        CHECK(check());
    }
    SECTION("-0 is still silence")
    {
        left[0] = -Sample(0);
        right[kSamples - 1] = -Sample(0);
        CHECK(check());
    }
    SECTION("the smallest denormal counts as content, even in the last sample of the last channel")
    {
        right[kSamples - 1] = std::numeric_limits<Sample>::denorm_min();
        CHECK_FALSE(check());
    }
    SECTION("a negative denormal counts as content")
    {
        left[5] = -std::numeric_limits<Sample>::denorm_min();
        CHECK_FALSE(check());
    }
    SECTION("the smallest normal value counts as content")
    {
        left[3] = std::numeric_limits<Sample>::min();
        CHECK_FALSE(check());
    }
    SECTION("NaN and Inf count as content")
    {
        left[0] = std::numeric_limits<Sample>::quiet_NaN();
        CHECK_FALSE(check());
        left[0] = Sample(0);
        right[1] = -std::numeric_limits<Sample>::infinity();
        CHECK_FALSE(check());
    }
    SECTION("content in the first channel only is enough")
    {
        left[10] = Sample(0.25);
        CHECK_FALSE(check());
    }
    SECTION("no channels, no samples and null channels are vacuously silent")
    {
        const Sample* chans[] = {left.data(), right.data()};
        CHECK(probe::isAllZero(chans, 0, kSamples));
        CHECK(probe::isAllZero(chans, 2, 0));
        const Sample* withNull[] = {nullptr, left.data()};
        CHECK(probe::isAllZero(withNull, 2, kSamples));
    }
}

TEST_CASE("Counters start at zero", "[plugin][probe]")
{
    probe::Counters c;
    const auto s = c.snapshot();
    CHECK(s.sampleRate == 0.0);
    CHECK(s.maxBlockSize == 0);
    CHECK(s.numInputChannels == 0);
    CHECK(s.numOutputChannels == 0);
    CHECK(s.prepareCount == 0);
    CHECK(s.blocksTotal == 0);
    CHECK(s.blocksNonRealtime == 0);
    CHECK(s.blocksRealtime == 0);
    CHECK(s.blocksAllZero == 0);
    CHECK(s.blocksNonZero == 0);
    CHECK(s.editorOpens == 0);
    CHECK(s.editorCloses == 0);
    CHECK(s.analysisCount == 0);
    CHECK_FALSE(s.lastIsNonRealtime);
}

TEST_CASE("noteBlock classifies every block by realtime-ness and by silence", "[plugin][probe]")
{
    probe::Counters c;
    for (int i = 0; i < 3; ++i)
        c.noteBlock(false, true); // 实时、全 0
    for (int i = 0; i < 2; ++i)
        c.noteBlock(false, false); // 实时、有内容
    for (int i = 0; i < 4; ++i)
        c.noteBlock(true, true); // 离线、全 0
    for (int i = 0; i < 5; ++i)
        c.noteBlock(true, false); // 离线、有内容

    auto s = c.snapshot();
    CHECK(s.blocksRealtime == 5);
    CHECK(s.blocksNonRealtime == 9);
    CHECK(s.blocksAllZero == 7);
    CHECK(s.blocksNonZero == 7);
    CHECK(s.blocksTotal == 14);
    CHECK(s.lastIsNonRealtime); // 最后一块是离线

    c.noteBlock(false, false);
    s = c.snapshot();
    CHECK_FALSE(s.lastIsNonRealtime);
    CHECK(s.blocksTotal == 15);
}

TEST_CASE("every snapshot keeps the totals consistent", "[plugin][probe]")
{
    probe::Counters c;
    std::uint32_t seed = 12345u;
    for (int i = 0; i < 1000; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        c.noteBlock((seed >> 16) % 2 == 0, (seed >> 20) % 3 == 0);
        const auto s = c.snapshot();
        REQUIRE(s.blocksTotal == s.blocksNonRealtime + s.blocksRealtime);
        REQUIRE(s.blocksTotal == s.blocksAllZero + s.blocksNonZero);
        REQUIRE(s.blocksTotal == static_cast<std::uint64_t>(i + 1));
    }
}

TEST_CASE("prepare, editor and analysis counters", "[plugin][probe]")
{
    probe::Counters c;
    c.notePrepare(44100.0, 256, 1, 1);
    c.notePrepare(96000.0, 1024, 2, 2);
    c.noteEditorOpened();
    c.noteEditorOpened();
    c.noteEditorOpened();
    c.noteEditorClosed();
    c.noteAnalysis();
    c.noteAnalysis();

    const auto s = c.snapshot();
    CHECK(s.prepareCount == 2);
    CHECK(s.sampleRate == 96000.0); // 最近一次 prepare 的值
    CHECK(s.maxBlockSize == 1024);
    CHECK(s.numInputChannels == 2);
    CHECK(s.numOutputChannels == 2);
    CHECK(s.editorOpens == 3);
    CHECK(s.editorCloses == 1);
    CHECK(s.analysisCount == 2);
}

TEST_CASE("the JSON is a flat object with exactly the documented fields", "[plugin][probe]")
{
    probe::Counters c;
    c.notePrepare(48000.0, 512, 2, 2);
    c.noteBlock(true, false);
    c.noteBlock(true, true);
    c.noteBlock(false, true);
    c.noteEditorOpened();
    c.noteAnalysis();

    const auto json = probe::toJson(c.snapshot(), 4242);
    const juce::var v = parseJson(json);
    REQUIRE(v.isObject());
    const auto* obj = v.getDynamicObject();
    REQUIRE(obj != nullptr);
    CHECK(obj->getProperties().size() == static_cast<int>(sizeof(kFieldNames) / sizeof(kFieldNames[0])));
    for (const char* name : kFieldNames)
        CHECK(obj->hasProperty(name));

    CHECK(intField(v, "schema") == probe::kSchemaVersion);
    CHECK(intField(v, "schema") == 1);
    CHECK(intField(v, "pid") == 4242);
    CHECK(static_cast<double>(v.getProperty("sampleRate", 0)) == 48000.0);
    CHECK(intField(v, "maxBlockSize") == 512);
    CHECK(intField(v, "numInputChannels") == 2);
    CHECK(intField(v, "numOutputChannels") == 2);
    CHECK(intField(v, "prepareCount") == 1);
    CHECK(intField(v, "blocksTotal") == 3);
    CHECK(intField(v, "blocksNonRealtime") == 2);
    CHECK(intField(v, "blocksRealtime") == 1);
    CHECK(intField(v, "blocksAllZero") == 2);
    CHECK(intField(v, "blocksNonZero") == 1);
    CHECK(intField(v, "editorOpens") == 1);
    CHECK(intField(v, "editorCloses") == 0);
    CHECK(intField(v, "analysisCount") == 1);
    CHECK(v.getProperty("lastIsNonRealtime", juce::var(1)).isBool());
    CHECK_FALSE(static_cast<bool>(v.getProperty("lastIsNonRealtime", juce::var(true))));
}

TEST_CASE("the JSON stays valid for odd sample rates", "[plugin][probe]")
{
    probe::Snapshot s;
    s.sampleRate = 47952.047952;
    CHECK(static_cast<double>(parseJson(probe::toJson(s, 1)).getProperty("sampleRate", 0)) ==
          Catch::Approx(47952.047952).margin(1e-6));

    s.sampleRate = std::numeric_limits<double>::quiet_NaN();
    const juce::var nan = parseJson(probe::toJson(s, 1));
    REQUIRE(nan.isObject());
    CHECK(static_cast<double>(nan.getProperty("sampleRate", 1)) == 0.0);

    s.sampleRate = std::numeric_limits<double>::infinity();
    const juce::var inf = parseJson(probe::toJson(s, 1));
    REQUIRE(inf.isObject());
    CHECK(static_cast<double>(inf.getProperty("sampleRate", 1)) == 0.0);
}

TEST_CASE("flush writes a parseable file and leaves no temporary file behind", "[plugin][probe]")
{
    TempDir dir;
    const fs::path target = dir.path() / "probe.json";
    probe::Counters c;
    c.noteBlock(true, false);
    probe::Reporter r(target, c);

    REQUIRE(r.flush());
    CHECK(r.writesDone() == 1);
    CHECK(dir.entryCount() == 1);
    const juce::var v = parseJson(readFile(target));
    REQUIRE(v.isObject());
    CHECK(intField(v, "blocksNonRealtime") == 1);
    CHECK(intField(v, "pid") == probe::currentPid());
}

TEST_CASE("flush replaces an existing file whole", "[plugin][probe]")
{
    TempDir dir;
    const fs::path target = dir.path() / "probe.json";
    {
        std::ofstream stale(target, std::ios::binary);
        stale << "not json at all";
    }
    probe::Counters c;
    probe::Reporter r(target, c);
    REQUIRE(r.flush());
    c.noteBlock(false, true);
    REQUIRE(r.flush());
    const juce::var v = parseJson(readFile(target));
    REQUIRE(v.isObject());
    CHECK(intField(v, "blocksTotal") == 1);
    CHECK(dir.entryCount() == 1);
}

TEST_CASE("tick writes only when the content changed", "[plugin][probe]")
{
    TempDir dir;
    const fs::path target = dir.path() / "probe.json";
    probe::Counters c;
    probe::Reporter r(target, c);

    CHECK(r.tick()); // 第一次：还没写过
    CHECK_FALSE(r.tick());
    CHECK_FALSE(r.tick());
    CHECK(r.writesDone() == 1);

    c.noteBlock(true, true);
    CHECK(r.tick());
    CHECK_FALSE(r.tick());
    CHECK(r.writesDone() == 2);

    CHECK(r.flush()); // flush 不看有没有变化
    CHECK(r.writesDone() == 3);
}

TEST_CASE("a failed write is silent, leaves nothing behind and is retried by the next tick", "[plugin][probe]")
{
    TempDir dir;
    const fs::path missingDir = dir.path() / "not-there";
    const fs::path target = missingDir / "probe.json";
    probe::Counters c;
    probe::Reporter r(target, c);

    CHECK_FALSE(r.flush());
    CHECK_FALSE(r.tick());
    CHECK(r.writesDone() == 0);
    CHECK_FALSE(fs::exists(missingDir)); // 不替调用者建目录
    CHECK(dir.entryCount() == 0);

    std::error_code ec;
    REQUIRE(fs::create_directory(missingDir, ec));
    CHECK(r.tick()); // 失败时没有记成“已写”，目录出现后内容虽没变也会补写
    CHECK(r.writesDone() == 1);
    CHECK(parseJson(readFile(target)).isObject());
}

TEST_CASE("a target that is a directory fails quietly", "[plugin][probe]")
{
    TempDir dir;
    const fs::path target = dir.path() / "occupied";
    std::error_code ec;
    REQUIRE(fs::create_directory(target, ec));
    probe::Counters c;
    probe::Reporter r(target, c);
    CHECK_FALSE(r.flush());
    CHECK(r.writesDone() == 0);
    CHECK(dir.entryCount() == 1); // 只剩那个目录，没有临时文件
}

TEST_CASE("a reader never sees a half-written file while the writer keeps replacing it", "[plugin][probe]")
{
    TempDir dir;
    const fs::path target = dir.path() / "probe.json";
    probe::Counters c;
    probe::Reporter r(target, c);
    REQUIRE(r.flush());

    constexpr int kWrites = 400;
    std::atomic<bool> done{false};
    std::thread writer([&] {
        for (int i = 0; i < kWrites; ++i)
        {
            c.noteBlock(i % 2 == 0, i % 3 == 0);
            r.flush();
        }
        done.store(true);
    });

    int valid = 0;
    int empty = 0;
    int invalid = 0;
    while (!done.load())
    {
        std::ifstream in(target, std::ios::binary);
        if (!in)
            continue; // 改名的瞬间被占着打不开是允许的，读不到不等于读到半截
        std::ostringstream ss;
        ss << in.rdbuf();
        const std::string text = ss.str();
        if (text.empty())
        {
            ++empty;
            continue;
        }
        const juce::var v = parseJson(text);
        if (v.isObject() && intField(v, "schema") == 1 && v.getDynamicObject()->getProperties().size() == 16)
            ++valid;
        else
            ++invalid;
    }
    writer.join();

    CHECK(empty == 0);
    CHECK(invalid == 0);
    CHECK(valid > 0);
    CHECK(r.writesDone() >= 1);
    CHECK(dir.entryCount() == 1);
}

TEST_CASE("the environment variable switches the probe on, and unset or empty means nothing is written",
          "[plugin][probe]")
{
    EnvCleanup cleanup;
    TempDir dir;
    probe::Counters c;

    SECTION("unset: no path, no reporter, nothing created")
    {
        unsetEnv();
        CHECK_FALSE(probe::Reporter::pathFromEnvironment().has_value());
        CHECK(probe::Reporter::fromEnvironment(c) == nullptr);
        CHECK(dir.entryCount() == 0);
    }
    SECTION("empty string counts as unset")
    {
        setEnv(fs::path());
        CHECK_FALSE(probe::Reporter::pathFromEnvironment().has_value());
        CHECK(probe::Reporter::fromEnvironment(c) == nullptr);
        CHECK(dir.entryCount() == 0);
    }
    SECTION("set: the reporter targets that path and writes it")
    {
        const fs::path target = dir.path() / "from-env.json";
        setEnv(target);
        const auto path = probe::Reporter::pathFromEnvironment();
        REQUIRE(path.has_value());
        CHECK(*path == target);
        auto reporter = probe::Reporter::fromEnvironment(c);
        REQUIRE(reporter != nullptr);
        CHECK(reporter->target() == target);
        REQUIRE(reporter->flush());
        CHECK(parseJson(readFile(target)).isObject());
    }
}

TEST_CASE("the analysis hook is an empty function unless the probe is compiled in", "[plugin][probe]")
{
    // 这个翻译单元不定义 MICIRI_HOST_PROBE，钩子必须是空函数：不引用计数器，也不动它。
    const auto before = probe::counters().snapshot().analysisCount;
    probe::noteAnalysis();
    probe::noteAnalysis();
#ifdef MICIRI_HOST_PROBE
    CHECK(probe::counters().snapshot().analysisCount == before + 2);
#else
    CHECK(probe::counters().snapshot().analysisCount == before);
#endif
}
