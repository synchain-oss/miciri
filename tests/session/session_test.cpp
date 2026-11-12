// SPDX-License-Identifier: GPL-3.0-or-later
//
// 起线程的部分：采集环、工作线程、控制函数与保护。一个线程扮演宿主的音频线程往 tap 里推块，
// 主线程扮演消息线程读快照、取消、重新准备。sanitizer 构建（含 TSan）跑的就是这些交接。
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>
#include <vector>

#include "config/Thresholds.h"
#include "core/level/Synth.h"
#include "session/Session.h"

using namespace miciri;
using namespace miciri::testing;
using core::RetestReason;
using core::StepId;
using core::StepStatus;
using Clock = std::chrono::steady_clock;

namespace
{
std::atomic<int> gAnalyses{0};
void countAnalysis() noexcept
{
    gAnalyses.fetch_add(1);
}

// 在另一个线程上按块推样本。speed 是相对实时的倍数，0 表示尽快推完
void pump(session::AudioTap& tap, const std::vector<double>& x, int channels, double fs, double speed,
          bool nonRealtime = false, int block = 512)
{
    std::thread audio([&] {
        const auto start = Clock::now();
        std::vector<const double*> ptrs(static_cast<std::size_t>(channels));
        for (std::size_t i = 0; i < x.size(); i += static_cast<std::size_t>(block))
        {
            const int n = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(block), x.size() - i));
            for (auto& p : ptrs)
                p = x.data() + i;
            tap.process(ptrs.data(), channels, n, nonRealtime);
            if (speed > 0.0)
                std::this_thread::sleep_until(
                    start +
                    std::chrono::duration<double>(static_cast<double>(i + static_cast<std::size_t>(n)) / (fs * speed)));
        }
    });
    audio.join();
}

bool waitFor(const std::function<bool()>& ok, double seconds = 15.0)
{
    const auto until = Clock::now() + std::chrono::duration<double>(seconds);
    while (Clock::now() < until)
    {
        if (ok())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return ok();
}

session::StepState stepOf(const session::Session& s, StepId id)
{
    return s.view().steps[static_cast<std::size_t>(id)];
}
} // namespace

TEST_CASE("the capture ring keeps frame order across wrap-around and drops whole blocks when full")
{
    session::AudioTap tap;
    const double fs = 1000.0;
    tap.arm(fs, 2, 0.1); // 100 帧 × 2 路，环取到 256 个 float
    std::vector<double> l(30), r(30);
    std::vector<float> out(64 * 2);
    double next = 0.0;
    double expect = 0.0;
    for (int round = 0; round < 20; ++round)
    {
        for (std::size_t i = 0; i < l.size(); ++i)
        {
            l[i] = next;
            r[i] = -next;
            next += 1.0;
        }
        const double* ptrs[2] = {l.data(), r.data()};
        tap.process(ptrs, 2, 30, false);
        std::size_t got = 0;
        while ((got = tap.pop(out.data(), 64)) > 0)
            for (std::size_t i = 0; i < got; ++i)
            {
                REQUIRE(out[2 * i] == static_cast<float>(expect));
                REQUIRE(out[2 * i + 1] == static_cast<float>(-expect));
                expect += 1.0;
            }
    }
    CHECK(expect == next);
    CHECK_FALSE(tap.overflowed());
    CHECK(tap.framesArrived() == 600);

    // 装不下的一块整块丢掉，不写半块
    std::vector<double> big(200, 0.5);
    const double* bigPtrs[2] = {big.data(), big.data()};
    tap.process(bigPtrs, 2, 200, false);
    CHECK(tap.overflowed());
    CHECK(tap.pop(out.data(), 64) == 0);

    // 声道数不符、宿主报离线：只置标志
    tap.arm(fs, 2, 0.1);
    tap.process(bigPtrs, 1, 10, false);
    CHECK(tap.layoutMismatch());
    tap.process(bigPtrs, 2, 10, true);
    CHECK(tap.offline());
    CHECK(tap.pop(out.data(), 64) == 0);

    // 停录之后音频线程什么都不做
    tap.disarm();
    tap.process(bigPtrs, 2, 10, false);
    CHECK(tap.framesArrived() == 0);
}

TEST_CASE("a step paced like real time completes, runs the analyses, and later expires by the rules")
{
    gAnalyses = 0;
    session::Session s(countAnalysis);
    const double fs = 16000.0;
    s.prepare(fs, 1);
    REQUIRE(s.startStep(StepId::Connect) == session::Session::Start::Ok);
    CHECK(s.startStep(StepId::Gain) == session::Session::Start::Busy);
    const auto x = voice(fs, thr::no_signal::low_flat_window_s + 0.5, 0.1, 5);
    pump(s.tap(), x, 1, fs, 1.3);
    REQUIRE(waitFor([&] { return !s.running(); }));
    CHECK(stepOf(s, StepId::Connect).status == StepStatus::Done);
    REQUIRE(s.view().results.noSignal.has_value());
    CHECK(s.view().results.noSignal->verdict.state == core::SignalState::Ok);
    CHECK(gAnalyses > 0);
    CHECK(s.view().live.notice == core::FlowNotice::Success);
    CHECK_FALSE(s.tap().armed());

    // 采样率变了：已有结果全部过期
    s.prepare(fs * 2, 1);
    CHECK(s.view().stale[static_cast<std::size_t>(session::Section::NoSignal)]);
    CHECK_FALSE(s.view().stale[static_cast<std::size_t>(session::Section::Gain)]); // 没做过的不算
}

TEST_CASE("audio arriving much faster than real time voids the step without analysing it")
{
    gAnalyses = 0;
    session::Session s(countAnalysis);
    const double fs = 48000.0;
    s.prepare(fs, 1);
    REQUIRE(s.startStep(StepId::Gain) == session::Session::Start::Ok);
    // 宿主在离线渲染却报实时：块一个接一个地来，不等墙钟
    pump(s.tap(), sine(fs, 440.0, 0.3, thr::session::gain_s + 1.0), 1, fs, 0.0);
    REQUIRE(waitFor([&] { return !s.running(); }));
    const auto st = stepOf(s, StepId::Gain);
    CHECK(st.status == StepStatus::Invalid);
    CHECK(st.invalidReason == RetestReason::OfflineRender);
    CHECK(s.view().offlineRender);
    CHECK_FALSE(s.view().results.gain.has_value());
    CHECK(gAnalyses == 0);
}

TEST_CASE("the host's own offline flag voids the step and nothing is analysed")
{
    gAnalyses = 0;
    session::Session s(countAnalysis);
    s.prepare(48000.0, 2);
    REQUIRE(s.startStep(StepId::Quiet) == session::Session::Start::Ok);
    pump(s.tap(), std::vector<double>(4800, 0.01), 2, 48000.0, 0.0, true);
    REQUIRE(waitFor([&] { return !s.running(); }));
    CHECK(stepOf(s, StepId::Quiet).invalidReason == RetestReason::OfflineRender);
    CHECK(gAnalyses == 0);
}

TEST_CASE("tests do not start in a validation host, during an offline render, or before prepare")
{
    session::Session s;
    CHECK(s.startStep(StepId::Gain) == session::Session::Start::Disabled);
    s.prepare(48000.0, 1);
    CHECK(s.startStep(StepId::Gain, true) == session::Session::Start::Disabled);
    s.setHostTraits({true, false});
    CHECK(s.startStep(StepId::Gain) == session::Session::Start::Disabled);
    CHECK_FALSE(s.tap().armed());
    CHECK(stepOf(s, StepId::Gain).status == StepStatus::Idle);
}

TEST_CASE("a channel count that changes mid-step voids it")
{
    session::Session s;
    s.prepare(48000.0, 2);
    REQUIRE(s.startStep(StepId::Gain) == session::Session::Start::Ok);
    pump(s.tap(), std::vector<double>(4800, 0.01), 1, 48000.0, 0.0);
    REQUIRE(waitFor([&] { return !s.running(); }));
    CHECK(stepOf(s, StepId::Gain).invalidReason == RetestReason::LayoutChanged);
}

TEST_CASE("no audio for too long voids the step as a callback gap")
{
    session::Session s;
    s.prepare(48000.0, 1);
    REQUIRE(s.startStep(StepId::Quiet) == session::Session::Start::Ok);
    REQUIRE(waitFor([&] { return !s.running(); }, thr::no_signal::no_callback_s + 5.0));
    CHECK(stepOf(s, StepId::Quiet).invalidReason == RetestReason::CallbackGap);
}

TEST_CASE("a sample-rate change mid-step voids that step")
{
    session::Session s;
    s.prepare(48000.0, 1);
    REQUIRE(s.startStep(StepId::Gain) == session::Session::Start::Ok);
    std::thread audio([&] { pump(s.tap(), std::vector<double>(9600, 0.01), 1, 48000.0, 1.0); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    s.prepare(44100.0, 1);
    audio.join();
    CHECK_FALSE(s.running());
    CHECK(stepOf(s, StepId::Gain).status == StepStatus::Invalid);
    CHECK(stepOf(s, StepId::Gain).invalidReason == RetestReason::RateChanged);
    CHECK_FALSE(s.tap().armed());
    // 同样的采样率再 prepare 一次不算变化
    REQUIRE(s.startStep(StepId::Gain) == session::Session::Start::Ok);
    s.prepare(44100.0, 1);
    CHECK(s.running());
    s.cancelStep();
}

TEST_CASE("cancel restores the step's earlier status and stops capturing")
{
    session::Session s;
    s.prepare(48000.0, 1);
    REQUIRE(s.startStep(StepId::Room) == session::Session::Start::Ok);
    std::thread audio([&] { pump(s.tap(), std::vector<double>(4800, 0.01), 1, 48000.0, 1.0); });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    for (int i = 0; i < 20; ++i)
        (void)s.view(); // 消息线程同时在读快照
    s.cancelStep();
    audio.join();
    CHECK_FALSE(s.running());
    CHECK_FALSE(s.tap().armed());
    CHECK(stepOf(s, StepId::Room).status == StepStatus::Idle);
    CHECK(s.startStep(StepId::Room) == session::Session::Start::Ok);
    // 析构时正在跑：先停工作线程再退出，不挂住
}
