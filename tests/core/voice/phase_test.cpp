// SPDX-License-Identifier: GPL-3.0-or-later
//
// 相位不变性：同一段输入，分析起点挪几个样本（用户点“开始”的时刻是随机的），读数不该跟着变。
// 每项把合成输入的开头去掉 0 到一个完整周期的样本数（逐 0.5 ms，另加几个零碎的样本数），看读数的最大减最小。
// 周期取改动前的块长：块不重叠时读数按块长循环，这些测试在那样的实现上会变红。
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "core/voice/VoiceSynth.h"
#include "voice/Voice.h"

using namespace miciri;
using namespace miciri::testing;

namespace
{
constexpr double kFs = 48000.0;

// 去掉开头的 0、24、48、… 个样本直到 period，再加几个不在 0.5 ms 网格上的样本数
std::vector<std::size_t> shifts(std::size_t period)
{
    std::vector<std::size_t> out;
    for (std::size_t d = 0; d < period; d += 24)
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

// 基频在 seconds 内从 f0 线性滑到 f1 的元音（谐波到 3.9 kHz 以下，幅度按 1/k）：预测误差随时间变化，像真实的语音
void addGlide(std::vector<double>& x, double fs, double startSec, double seconds, double f0, double f1,
              double amplitude)
{
    const auto start = static_cast<std::size_t>(startSec * fs);
    const auto length = static_cast<std::size_t>(seconds * fs);
    double phase = 0.0;
    for (std::size_t i = 0; i < length && start + i < x.size(); ++i)
    {
        const double f = f0 + (f1 - f0) * static_cast<double>(i) / static_cast<double>(length);
        phase += 2.0 * kPi * f / fs;
        double s = 0.0;
        for (int k = 1; k * f < 3900.0; ++k)
            s += std::sin(k * phase + 0.3 * k) / k;
        x[start + i] += amplitude * 0.5 * ramp(i, length, fs, 0.03) * s;
    }
}

// 自然朗读式的一段：六个滑音元音，之间是擦音。每个元音里七下尖峰，相隔 110 ms（不并成一簇），幅度逐个增加 1%，
// 所以总有几下的残差倍数贴着计数门限：残差或尺度随起点抖几个百分点，计数就会变
std::vector<double> clickTake()
{
    std::vector<double> x = whiteNoise(static_cast<std::size_t>(7.6 * kFs), 1e-4, 61);
    double a = 0.0038;
    for (int i = 0; i < 6; ++i)
    {
        const double t = 0.2 + 1.2 * static_cast<double>(i);
        addGlide(x, kFs, t, 0.9, 110.0 + 20.0 * i, 170.0 - 10.0 * i, 0.12);
        addHiss(x, kFs, t + 0.95, 0.12, 0.02, 70 + static_cast<std::uint64_t>(i));
        for (int j = 0; j < 7; ++j)
        {
            addTick(x, kFs, t + 0.1 + 0.11 * j + 0.0017 * i, a);
            a *= 1.01;
        }
    }
    return x;
}

// 音节串：每个音节 90 ms、基频在音节之间跳变（预测系数跟着跳），每个音节里放一下尖峰。ticks 收到每一下的时间
std::vector<double> syllableTake(std::vector<double>& ticks)
{
    std::vector<double> x = whiteNoise(static_cast<std::size_t>(8.0 * kFs), 1e-4, 61);
    for (int p = 0; p < 6; ++p)
        for (int s = 0; s < 8; ++s)
        {
            const double t = 0.2 + 1.25 * p + 0.09 * s;
            const double f = 110.0 + 37.0 * ((s * 5 + p * 3) % 7);
            addGlide(x, kFs, t, 0.09, f, f * 1.05, 0.12);
            ticks.push_back(t + 0.02 + 0.006 * (s % 5));
            addTick(x, kFs, ticks.back(), 0.006);
        }
    return x;
}
} // namespace

TEST_CASE("相位：口水音的次数与最响一下不随分析起点变化", "[voice][clicks][phase]")
{
    const auto x = clickTake();
    Range count, perMinute, loudest;
    for (std::size_t d : shifts(480))
    {
        const auto r = core::analyseClicks(from(x, d), kFs);
        count.add(static_cast<double>(r.measures.count));
        perMinute.add(r.measures.perMinute);
        loudest.add(r.measures.loudestRelDb);
    }
    INFO("count " << count.lo << ".." << count.hi << ", loudest " << loudest.lo << ".." << loudest.hi << " dB");
    REQUIRE(count.lo >= 20.0); // 门限两边都有尖峰，测试才有意义
    CHECK(count.width() <= 1.0);
    CHECK(perMinute.width() <= 0.05 * perMinute.hi);
    CHECK(loudest.width() <= 1.0);
}

// 计数只在尖峰贴着门限时才会变，这里直接看每一下的残差倍数：预测系数只在块中心算、不在块间插值，
// 或局部尺度的块不重叠、取纯中位数时，变化在 2% 以上；现在的实现不到 1%
TEST_CASE("相位：每一下尖峰的残差倍数不随分析起点变化", "[voice][clicks][phase]")
{
    std::vector<double> ticks;
    const auto x = syllableTake(ticks);
    const auto offsets = shifts(480);
    std::vector<Range> multiple(ticks.size());
    std::vector<std::size_t> hits(ticks.size(), 0);
    Range speech;
    for (std::size_t d : offsets)
    {
        const auto scan = core::scanClicks(from(x, d), kFs);
        REQUIRE(scan.speechDb.has_value());
        speech.add(*scan.speechDb);
        for (std::size_t k = 0; k < ticks.size(); ++k)
        {
            // 去掉开头 d 个样本以后，这一下在新时间轴上的位置
            const double t = (std::floor(ticks[k] * kFs) - static_cast<double>(d)) / kFs;
            double best = 0.0;
            for (const auto& c : scan.candidates)
                if (std::fabs(c.timeSec - t) < 0.001)
                    best = std::max(best, c.residualMultiple);
            if (best > 0.0)
            {
                ++hits[k];
                multiple[k].add(best);
            }
        }
    }
    // 只比较每个起点都成了候选的那几下；有几下不到候选门限，不算
    std::size_t compared = 0;
    double worst = 0.0;
    for (std::size_t k = 0; k < ticks.size(); ++k)
        if (hits[k] == offsets.size())
        {
            ++compared;
            worst = std::max(worst, multiple[k].hi / multiple[k].lo - 1.0);
        }
    REQUIRE(compared >= 24);
    INFO("单个尖峰残差倍数的最大相对变化 " << worst << "，比较了 " << compared << " 下");
    CHECK(worst <= 0.015);
    // 本人语音电平（最响一下的参照、太轻的尖峰按它排除）：音节短、起落快，语音帧不重叠时它随起点变
    INFO("语音电平 " << speech.lo << ".." << speech.hi << " dB");
    CHECK(speech.width() <= 0.05);
}
