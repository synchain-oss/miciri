// SPDX-License-Identifier: GPL-3.0-or-later
//
// 自动操作脚本的解析与调度：不依赖宿主与处理器，默认（OFF）的构建里照常编、照常跑。
// 动作的执行由用例自己给（记下调用、按需回 busy），所以这里只验“什么时候执行、按什么顺序、记下了什么”。
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "HostProbeTestSupport.h"
#include "plugin/HostProbeScript.h"

namespace
{
namespace probe = miciri::probe;
using namespace miciri::probe_test;
using probe::Op;

// 记下每次执行的动作；busyFor 次之内对 startStep 回 busy
struct FakeBridge
{
    int busyFor = 0;
    std::vector<std::string> calls;

    probe::Reply operator()(const probe::Action& a)
    {
        calls.push_back(std::string(probe::opName(a.op)) + (a.arg.empty() ? "" : ":" + a.arg));
        if (a.op == Op::StartStep && busyFor > 0)
        {
            --busyFor;
            return {false, "busy"};
        }
        if (a.op == Op::StartStep && a.arg == "nope")
            return {false, "bad-step"};
        return {};
    }
};

probe::Script parse(const std::string& text)
{
    return probe::parseScript(text);
}
} // namespace

TEST_CASE("a well-formed script parses into actions in order", "[plugin][probe][script]")
{
    const auto s = parse(R"({"schema":1,"actions":[
        {"at":0.5,"do":"setMicType","arg":"condenser"},
        {"at":1,"do":"startStep","arg":"connect"},
        {"at":1,"do":"cancelStep"},
        {"at":200,"do":"reportChange","arg":"position"},
        {"at":201.25,"do":"dismissChannelPrompt"}]})");
    REQUIRE(s.ok());
    REQUIRE(s.actions.size() == 5);
    CHECK(s.actions[0].op == Op::SetMicType);
    CHECK(s.actions[0].arg == "condenser");
    CHECK(s.actions[0].atSec == 0.5);
    CHECK(s.actions[1].op == Op::StartStep);
    CHECK(s.actions[1].atSec == 1.0); // 整数秒也认
    CHECK(s.actions[2].op == Op::CancelStep);
    CHECK(s.actions[2].arg.empty());
    CHECK(s.actions[3].op == Op::ReportChange);
    CHECK(s.actions[4].op == Op::DismissChannelPrompt);
    CHECK(s.actions[4].atSec == 201.25);

    const auto empty = parse(R"({"schema":1,"actions":[]})");
    CHECK(empty.ok());
    CHECK(empty.actions.empty());
}

TEST_CASE("a malformed script is rejected as a whole, with the reason", "[plugin][probe][script]")
{
    struct Bad
    {
        const char* text;
        const char* reasonPart;
    };
    const Bad cases[] = {
        {"not json", "not valid JSON"},
        {R"([1,2])", "JSON object"},
        {R"({"actions":[]})", "schema"},
        {R"({"schema":2,"actions":[]})", "schema"},
        {R"({"schema":"1","actions":[]})", "schema"},
        {R"({"schema":1})", "actions must be an array"},
        {R"({"schema":1,"actions":{}})", "actions must be an array"},
        {R"({"schema":1,"actions":[1]})", "actions[0] must be an object"},
        {R"({"schema":1,"actions":[[0.5,"cancelStep"]]})", "actions[0] must be an object"},
        {R"({"schema":1,"actions":[{"do":"cancelStep"}]})", "actions[0].at"},
        {R"({"schema":1,"actions":[{"at":"1","do":"cancelStep"}]})", "actions[0].at"},
        {R"({"schema":1,"actions":[{"at":-1,"do":"cancelStep"}]})", "not negative"},
        {R"({"schema":1,"actions":[{"at":2,"do":"cancelStep"},{"at":1,"do":"cancelStep"}]})",
         "actions[1].at is earlier"},
        {R"({"schema":1,"actions":[{"at":1}]})", "actions[0].do"},
        {R"({"schema":1,"actions":[{"at":1,"do":"openExternal","arg":"https://example.com"}]})", "unknown action"},
        {R"({"schema":1,"actions":[{"at":1,"do":"startstep","arg":"gain"}]})", "unknown action"},
        {R"({"schema":1,"actions":[{"at":1,"do":"startStep"}]})", "needs a string arg"},
        {R"({"schema":1,"actions":[{"at":1,"do":"startStep","arg":3}]})", "needs a string arg"},
        {R"({"schema":1,"actions":[{"at":1,"do":"cancelStep","arg":"gain"}]})", "takes no arg"},
    };
    for (const auto& c : cases)
    {
        INFO(c.text);
        const auto s = parse(c.text);
        INFO(s.error);
        CHECK_FALSE(s.ok());
        CHECK(s.actions.empty()); // 一处不合规就一个动作也不留
        CHECK(s.error.find(c.reasonPart) != std::string::npos);
    }

    // 后面的动作不合规，前面合规的也不执行
    const auto partly =
        parse(R"({"schema":1,"actions":[{"at":0,"do":"setMicType","arg":"usb"},{"at":1,"do":"jump"}]})");
    CHECK_FALSE(partly.ok());
    CHECK(partly.actions.empty());
}

TEST_CASE("loading a script reports a missing or oversized file instead of throwing", "[plugin][probe][script]")
{
    TempDir dir;
    const auto missing = probe::loadScript(dir.path() / "missing.json");
    CHECK_FALSE(missing.ok());
    CHECK(missing.actions.empty());

    const auto good = dir.path() / "good.json";
    {
        std::ofstream out(good, std::ios::binary);
        out << R"({"schema":1,"actions":[{"at":0,"do":"cancelStep"}]})";
    }
    const auto loaded = probe::loadScript(good);
    CHECK(loaded.ok());
    CHECK(loaded.actions.size() == 1);

    const auto big = dir.path() / "big.json";
    {
        std::ofstream out(big, std::ios::binary);
        out << std::string(static_cast<std::size_t>(probe::kScriptMaxBytes) + 1, ' ');
    }
    const auto tooBig = probe::loadScript(big);
    CHECK_FALSE(tooBig.ok());
    CHECK(tooBig.error.find("larger") != std::string::npos);
}

TEST_CASE("the clock is processed samples over the sample rate, and starts with the first samples",
          "[plugin][probe][script]")
{
    probe::SampleClock clock;
    CHECK_FALSE(clock.seconds(480)); // 还没 prepare，不知道采样率
    clock.prepare(0, 48000.0);
    CHECK_FALSE(clock.seconds(0)); // 还没收到有样本的块
    REQUIRE(clock.seconds(24000));
    CHECK(*clock.seconds(24000) == 0.5);

    // 中途用另一个采样率重新 prepare：之前的样本按旧采样率算，时钟不跳
    clock.prepare(48000, 44100.0);
    CHECK(*clock.seconds(48000) == 1.0);
    CHECK(*clock.seconds(48000 + 22050) == 1.5);
    clock.prepare(48000 + 22050, 44100.0); // 同一采样率再 prepare 一次也不变
    CHECK(*clock.seconds(48000 + 22050 + 44100) == 2.5);
}

TEST_CASE("actions run in order once the processed time reaches them", "[plugin][probe][script]")
{
    const auto s = parse(R"({"schema":1,"actions":[
        {"at":0,"do":"setMicType","arg":"dynamic"},
        {"at":1,"do":"startStep","arg":"connect"},
        {"at":1,"do":"startStep","arg":"nope"},
        {"at":3,"do":"cancelStep"}]})");
    REQUIRE(s.ok());
    probe::Scheduler sched(s.actions);
    FakeBridge bridge;
    auto run = [&](std::optional<double> t) { return sched.advance(t, std::ref(bridge)); };

    CHECK(run(std::nullopt) == 0); // 时钟没开始：连 at=0 的也不执行
    CHECK(bridge.calls.empty());
    CHECK(run(0.02) == 1);
    CHECK(run(0.99) == 0);
    CHECK(run(1.0) == 2); // 同一时刻到期的两个按顺序一起执行
    CHECK(run(2.5) == 0);
    CHECK(run(3.1) == 1);
    CHECK(sched.finished());
    CHECK(run(10.0) == 0);

    CHECK(bridge.calls ==
          std::vector<std::string>{"setMicType:dynamic", "startStep:connect", "startStep:nope", "cancelStep"});
    const auto& done = sched.done();
    REQUIRE(done.size() == 4);
    CHECK(done[0].doneSec == 0.02);
    CHECK(done[1].index == 1);
    CHECK(done[1].firstTriedSec == 1.0);
    CHECK(done[1].reply.ok);
    CHECK_FALSE(done[2].reply.ok); // 回执原样记下，接着往下走
    CHECK(done[2].reply.reason == "bad-step");
    CHECK(done[3].doneSec == 3.1);
    CHECK(done[3].busyReplies == 0);

    // 输出里的样子
    const auto v = probe::toVar(done[2]);
    CHECK(v.getProperty("do", {}).toString() == "startStep");
    CHECK(v.getProperty("arg", {}).toString() == "nope");
    CHECK_FALSE(static_cast<bool>(v.getProperty("reply", {}).getProperty("ok", true)));
    CHECK(v.getProperty("reply", {}).getProperty("reason", {}).toString() == "bad-step");
    CHECK_FALSE(probe::toVar(done[3]).getDynamicObject()->hasProperty("arg"));
    CHECK(probe::actionsVar(sched).getArray()->size() == 4);
}

TEST_CASE("a busy startStep waits for the previous step, and everything after it waits too", "[plugin][probe][script]")
{
    const auto s = parse(R"({"schema":1,"actions":[
        {"at":1,"do":"startStep","arg":"gain"},
        {"at":1.5,"do":"reportChange","arg":"gain"}]})");
    REQUIRE(s.ok());
    probe::Scheduler sched(s.actions);
    FakeBridge bridge;
    bridge.busyFor = 2;
    auto run = [&](double t) { return sched.advance(t, std::ref(bridge)); };

    CHECK(run(1.05) == 0);
    REQUIRE(sched.waiting());
    CHECK(sched.waiting()->busyReplies == 1);
    CHECK(run(2.0) == 0); // 第二个动作早就到期，也要等第一个
    CHECK(sched.waiting()->busyReplies == 2);
    CHECK(sched.remaining() == 2);
    CHECK(run(2.4) == 2); // 上一步结束：先执行等着的那个，再执行后面到期的
    CHECK_FALSE(sched.waiting());
    REQUIRE(sched.done().size() == 2);
    const auto& first = sched.done()[0];
    CHECK(first.firstTriedSec == 1.05);
    CHECK(first.doneSec == 2.4);
    CHECK(first.busyReplies == 2);
    CHECK(first.reply.ok);
    CHECK(sched.done()[1].doneSec == 2.4);
    CHECK(bridge.calls ==
          std::vector<std::string>{"startStep:gain", "startStep:gain", "startStep:gain", "reportChange:gain"});
}
