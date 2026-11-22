// SPDX-License-Identifier: GPL-3.0-or-later
//
// 带探针的处理器跑自动操作脚本（本目标定义 MICIRI_HOST_PROBE）：没有编辑器，假宿主在另一个线程上按块喂音频，
// 测试线程跑消息循环（探针的定时器在那里执行动作、写文件），再读输出目录里的文件。
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>

#include "HostProbeTestSupport.h"
#include "plugin/HostProbe.h"
#include "plugin/MiciriProcessor.h"

namespace
{
namespace fs = std::filesystem;
namespace probe = miciri::probe;
using miciri::MiciriProcessor;
using namespace miciri::probe_test;
using Clock = std::chrono::steady_clock;

constexpr double kRate = 48000.0;
constexpr int kBlock = 480; // 10 ms：秒数整除，样本秒好算

void configure(MiciriProcessor& p)
{
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add(juce::AudioChannelSet::stereo());
    layout.outputBuses.add(juce::AudioChannelSet::stereo());
    REQUIRE(p.setBusesLayout(layout));
    p.prepareToPlay(kRate, kBlock);
}

void writeText(const fs::path& file, const std::string& text)
{
    std::ofstream out(file, std::ios::binary);
    out << text;
}

// 喂 blocks 块正弦（两路不同的相位，像一个说话的声音那样有内容）；speed 是相对实时的倍数，0 表示尽快
void feed(MiciriProcessor& p, int blocks, double speed, std::atomic<bool>* stop = nullptr)
{
    juce::AudioBuffer<float> b(2, kBlock);
    juce::MidiBuffer midi;
    const auto start = Clock::now();
    static std::int64_t phase = 0;
    for (int n = 0; n < blocks && (stop == nullptr || !stop->load()); ++n)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kBlock; ++i)
                b.getWritePointer(ch)[i] = static_cast<float>(
                    0.1 * std::sin(2.0 * 3.141592653589793 * 220.0 * static_cast<double>(phase + i) / kRate + ch));
        phase += kBlock;
        p.processBlock(b, midi);
        if (speed > 0.0)
            std::this_thread::sleep_until(start + std::chrono::duration<double>((n + 1) * kBlock / (kRate * speed)));
    }
}

// 在测试线程上跑消息循环，直到条件成立或超时
bool dispatchUntil(const std::function<bool()>& ok, int timeoutMs)
{
    const auto until = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (Clock::now() < until)
    {
        if (ok())
            return true;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    }
    return ok();
}

void dispatchFor(int ms)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil(ms);
}

std::string pidFile(const std::string& stem)
{
    return stem + "-pid" + std::to_string(probe::currentPid()) + ".json";
}

juce::var at(const juce::var& v, std::initializer_list<const char*> path)
{
    juce::var cur = v;
    for (const char* key : path)
        cur = cur.getProperty(key, {});
    return cur;
}

juce::String str(const juce::var& v, std::initializer_list<const char*> path)
{
    return at(v, path).toString();
}
} // namespace

TEST_CASE("without an editor, the script runs the steps by processed time and each finished step is written",
          "[plugin][probe][autorun]")
{
    EnvCleanup cleanup;
    TempDir scriptDir;
    TempDir out;
    const auto script = scriptDir.path() / "script.json";
    // gain 在 connect 进行中就到期：回 busy，等 connect 结束再开始；随后立即取消
    writeText(script, R"({"schema":1,"actions":[
        {"at":0,"do":"setMicType","arg":"condenser"},
        {"at":0.1,"do":"startStep","arg":"connect"},
        {"at":0.2,"do":"startStep","arg":"gain"},
        {"at":0.3,"do":"cancelStep"}]})");
    setEnvVar(kScriptEnv, script);
    setEnvVar(kOutEnv, out.path());
    juce::ScopedJuceInitialiser_GUI gui;

    const auto connectFile = out.path() / pidFile("step-connect-001");
    const auto gainFile = out.path() / pidFile("step-gain-002");
    const auto finalFile = out.path() / pidFile("final");
    {
        MiciriProcessor p;
        configure(p);
        CHECK(fs::is_empty(out.path())); // 构造与 prepare 都不写 step/final 文件

        // 接通步要 6 秒音频；按 1.25 倍实时喂（快于实时 1.5 倍以上会被判成离线渲染而作废）
        std::atomic<bool> stop{false};
        std::thread audio([&] { feed(p, 800, 1.25, &stop); });
        const bool both = dispatchUntil([&] { return fs::exists(gainFile); }, 30000);
        stop = true;
        audio.join();
        REQUIRE(both);

        const auto connect = parseFile(connectFile);
        REQUIRE(connect.isObject());
        CHECK(intField(connect, "schema") == 1);
        CHECK(str(connect, {"kind"}) == "step");
        CHECK(str(connect, {"step"}) == "connect");
        CHECK(intField(connect, "seq") == 1);
        CHECK(intField(connect, "pid") == probe::currentPid());
        CHECK(str(connect, {"outcome"}) == "done");
        CHECK(static_cast<double>(connect.getProperty("processedSec", 0)) >= 6.1);
        CHECK(str(connect, {"script", "state"}) == "ok");
        // 快照：与桥同一份 Session JSON，这一步做完了；没有编辑器才有的界面字段
        CHECK(str(connect, {"snapshot", "steps", "connect", "status"}) == "done");
        CHECK(static_cast<int>(at(connect, {"snapshot", "steps", "connect", "runs"})) == 1);
        CHECK(str(connect, {"snapshot", "micType"}) == "condenser");
        CHECK(static_cast<double>(at(connect, {"snapshot", "device", "sampleRate"})) == kRate);
        CHECK(at(connect, {"snapshot", "results", "noSignal"}).isObject());
        CHECK_FALSE(at(connect, {"snapshot"}).getDynamicObject()->hasProperty("lang"));
        // 证据：与 getEvidence 的回执相同
        CHECK(static_cast<bool>(at(connect, {"evidence", "ok"})));
        CHECK(str(connect, {"evidence", "evidence", "stepId"}) == "connect");
        // 动作日志：前两个执行完；gain 在等，已经回过 busy
        const auto* actions = at(connect, {"actions"}).getArray();
        REQUIRE(actions != nullptr);
        REQUIRE(actions->size() == 2);
        CHECK(str((*actions)[0], {"do"}) == "setMicType");
        CHECK(static_cast<bool>(at((*actions)[0], {"reply", "ok"})));
        CHECK(static_cast<double>(at((*actions)[1], {"doneSec"})) >= 0.1);
        CHECK(str(connect, {"waiting", "arg"}) == "gain");
        CHECK(static_cast<int>(at(connect, {"waiting", "busyReplies"})) >= 1);

        const auto gain = parseFile(gainFile);
        REQUIRE(gain.isObject());
        CHECK(str(gain, {"outcome"}) == "cancelled");
        CHECK(str(gain, {"snapshot", "steps", "gain", "status"}) == "idle");
        CHECK(str(gain, {"snapshot", "steps", "connect", "status"}) == "done");
        CHECK_FALSE(static_cast<bool>(at(gain, {"evidence", "ok"})));
        CHECK(str(gain, {"evidence", "reason"}) == "no-evidence");
        const auto* all = at(gain, {"actions"}).getArray();
        REQUIRE(all != nullptr);
        REQUIRE(all->size() == 4);
        CHECK(static_cast<int>(at((*all)[2], {"busyReplies"})) >= 1);
        CHECK(static_cast<double>(at((*all)[2], {"doneSec"})) >= 6.1); // 等到 connect 收尾才开始
        CHECK(static_cast<bool>(at((*all)[2], {"reply", "ok"})));
        CHECK(at(gain, {"waiting"}).isVoid());

        p.releaseResources();
        const auto fin = parseFile(finalFile);
        REQUIRE(fin.isObject());
        CHECK(str(fin, {"kind"}) == "final");
        CHECK(str(fin, {"reason"}) == "release");
        CHECK(intField(fin, "stepFiles") == 2);
        CHECK(static_cast<int>(at(fin, {"script", "remaining"})) == 0);
        CHECK(at(fin, {"actions"}).getArray()->size() == 4);
    }
    CHECK(str(parseFile(finalFile), {"reason"}) == "destroy"); // 析构时再写一次
}

TEST_CASE("an unusable script runs nothing and the final file says why", "[plugin][probe][autorun]")
{
    EnvCleanup cleanup;
    TempDir scriptDir;
    TempDir out;
    const auto script = scriptDir.path() / "script.json";
    writeText(script, R"({"schema":1,"actions":[{"at":0,"do":"setMicType","arg":"usb"},{"at":0.1,"do":"fly"}]})");
    setEnvVar(kScriptEnv, script);
    setEnvVar(kOutEnv, out.path());
    juce::ScopedJuceInitialiser_GUI gui;

    MiciriProcessor p;
    configure(p);
    feed(p, 50, 0.0);
    dispatchFor(300);
    p.releaseResources();
    CHECK(p.micType() == 0); // 合规的第一个动作也没执行
    CHECK_FALSE(p.session().running());

    const auto fin = parseFile(out.path() / pidFile("final"));
    REQUIRE(fin.isObject());
    CHECK(str(fin, {"script", "state"}) == "error");
    CHECK(str(fin, {"script", "error"}).contains("unknown action"));
    CHECK(at(fin, {"actions"}).getArray()->isEmpty());
    CHECK(intField(fin, "stepFiles") == 0);
    CHECK(str(fin, {"snapshot", "micType"}) == "unknown");
}

TEST_CASE("actions wait for processed samples, not for the wall clock", "[plugin][probe][autorun]")
{
    EnvCleanup cleanup;
    TempDir scriptDir;
    const auto script = scriptDir.path() / "script.json";
    writeText(script, R"({"schema":1,"actions":[{"at":0.5,"do":"setMicType","arg":"dynamic"}]})");
    setEnvVar(kScriptEnv, script); // 只给脚本、不给输出目录：照样执行
    juce::ScopedJuceInitialiser_GUI gui;

    MiciriProcessor p;
    configure(p);
    dispatchFor(700); // 墙钟过了 0.5 秒，但一个样本都没处理
    CHECK(p.micType() == 0);
    feed(p, 40, 0.0); // 0.4 秒
    dispatchFor(300);
    CHECK(p.micType() == 0);
    feed(p, 10, 0.0); // 一共 0.5 秒
    CHECK(dispatchUntil([&] { return p.micType() != 0; }, 2000));
    CHECK(p.micType() == 1); // dynamic
}
