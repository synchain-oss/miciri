// SPDX-License-Identifier: GPL-3.0-or-later
//
// 相位不变性（喷麦、齿音、方向）：同一段输入，分析起点挪几个样本（用户点“开始”的时刻是随机的），读数不该跟着变。
// 每项把合成输入的开头去掉 0 到一个完整周期的样本数，看读数的最大减最小。周期取改动前的帧距（方向取改动前的帧长）：
// 帧距粗、或帧不重叠时读数按它循环，这些测试在那样的实现上会变红。合成信号比实录平稳，喷麦与齿音在改动前的帧距下
// 也只变零点几 dB，所以这两项的容差取 0.1 dB，比实录上要求的 0.5 dB 紧。
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "core/voice/VoiceSynth.h"
#include "direction/Direction.h"
#include "voice/Voice.h"

using namespace miciri;
using namespace miciri::testing;

namespace
{
constexpr double kFs = 48000.0;

// 去掉开头的 0、step、2·step、… 个样本直到 period，再加几个不在这个网格上的样本数
std::vector<std::size_t> shifts(std::size_t period, std::size_t step)
{
    std::vector<std::size_t> out;
    for (std::size_t d = 0; d < period; d += step)
        out.push_back(d);
    for (std::size_t d : std::array<std::size_t, 7>{1, 5, 11, 17, 29, 37, 101})
        if (d < period)
            out.push_back(d);
    return out;
}

std::vector<double> from(const std::vector<double>& x, std::size_t d)
{
    return std::vector<double>(x.begin() + static_cast<std::ptrdiff_t>(d), x.end());
}

struct Range
{
    double lo = 1e300, hi = -1e300;
    void add(double v)
    {
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    double width() const { return hi - lo; }
};

// 一遍字表：十二个字，基频从 85 Hz 起逐字升高（低基频时 10 ms 帧的功率随基频起伏最大），每三个字喷一个、一个比一个响
std::vector<double> poppedList()
{
    std::vector<double> x = whiteNoise(static_cast<std::size_t>(14.0 * kFs), 1e-4, 11);
    for (int i = 0; i < 12; ++i)
    {
        const double t = 0.5 + 1.1 * i;
        if (i % 3 == 1)
            addPop(x, kFs, t, 0.04, 60.0, 0.3 * std::pow(10.0, (2.0 + i) / 20.0));
        addVowel(x, kFs, t + 0.02, 0.3, 85.0 + 9.0 * i, 0.3);
    }
    return x;
}

// 六个 150 ms 的白噪声擦音，后面各跟一个元音（基频从 85 Hz 起各不相同）
std::vector<double> sibilantTake()
{
    std::vector<double> x = whiteNoise(static_cast<std::size_t>(8.0 * kFs), 3e-5, 21);
    for (int i = 0; i < 6; ++i)
    {
        const double t = 0.5 + 1.2 * i;
        addNoiseBurst(x, kFs, t, 0.15, 0.05, 100 + static_cast<std::uint64_t>(i));
        addVowel(x, kFs, t + 0.2, 0.3, 85.0 + 11.0 * i, 0.3);
    }
    return x;
}

// 唱一段：元音一个接一个，每个 0.25 s、基频与幅度各不相同，之间有短停顿
std::vector<double> sungTake()
{
    std::vector<double> x = whiteNoise(static_cast<std::size_t>(4.0 * kFs), 1e-4, 33);
    for (int i = 0; i < 12; ++i)
        addVowel(x, kFs, 0.1 + 0.32 * i, 0.25, 120.0 + 23.0 * (i % 5), 0.1 + 0.04 * (i % 3));
    return x;
}
} // namespace

TEST_CASE("相位：喷麦的最大值与事件数不随分析起点变化", "[voice][plosive][phase]")
{
    const auto x = poppedList();
    Range worst, events;
    for (std::size_t d : shifts(120, 12)) // 改动前每 2.5 ms 一帧
    {
        const auto r = core::analysePlosive(from(x, d), kFs);
        worst.add(r.measures.worstRatioDb);
        events.add(static_cast<double>(r.measures.events));
    }
    INFO("worst " << worst.lo << ".." << worst.hi << " dB, events " << events.lo << ".." << events.hi);
    REQUIRE(events.lo >= 10.0);
    CHECK(worst.width() <= 0.1);
    CHECK(events.width() <= 1.0);
}

TEST_CASE("相位：齿音各项不随分析起点变化", "[voice][sibilance][phase]")
{
    const auto x = sibilantTake();
    Range mid, high, overVowel, events;
    for (std::size_t d : shifts(240, 24)) // 改动前每 5 ms 一帧
    {
        const auto r = core::analyseSibilance(from(x, d), kFs);
        REQUIRE(r.measures.vowelEvents > 0);
        mid.add(r.measures.midHighDb);
        high.add(r.measures.highDb);
        overVowel.add(r.measures.highOverVowelDb);
        events.add(static_cast<double>(r.measures.events));
    }
    INFO("midHigh " << mid.width() << " dB, high " << high.width() << " dB, overVowel " << overVowel.lo << ".."
                    << overVowel.hi << " dB");
    CHECK(mid.width() <= 0.1);
    CHECK(high.width() <= 0.1);
    CHECK(overVowel.width() <= 0.1);
    CHECK(events.width() <= 1.0);
}

TEST_CASE("相位：方向的电平与亮度不随分析起点变化", "[direction][phase]")
{
    const auto x = sungTake();
    Range level, brightness;
    for (std::size_t d : shifts(4800, 240)) // 改动前 100 ms 一帧、不重叠
    {
        const auto t = core::measureDirectionTake(from(x, d), kFs);
        REQUIRE(t.active);
        level.add(t.levelDb);
        brightness.add(t.brightnessDb);
    }
    INFO("level " << level.lo << ".." << level.hi << " dB, brightness " << brightness.lo << ".." << brightness.hi
                  << " dB");
    CHECK(level.width() <= 0.5);
    CHECK(brightness.width() <= 0.5);
}
