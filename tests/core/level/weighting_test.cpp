// SPDX-License-Identifier: GPL-3.0-or-later
//
// 加权滤波器与重采样原件：A 加权对 IEC 61672 的标准表，K 加权各采样率对 48 kHz 公布系数的频响，
// 抽取器的直流增益与分块一致性，百分位的定义。
#include <catch2/catch_test_macros.hpp>

#include "core/level/Synth.h"
#include "level/Dsp.h"
#include "level/KWeighting.h"

using namespace miciri;
using namespace miciri::testing;

namespace
{
struct IecPoint
{
    double hz;
    double db;
};
// IEC 61672-1 的 A 加权标称值
const std::vector<IecPoint> kIecA{
    {10, -70.4}, {12.5, -63.4}, {16, -56.7},  {20, -50.5},  {25, -44.7},  {31.5, -39.4}, {40, -34.6},  {50, -30.2},
    {63, -26.2}, {80, -22.5},   {100, -19.1}, {125, -16.1}, {160, -13.4}, {200, -10.9},  {250, -8.6},  {315, -6.6},
    {400, -4.8}, {500, -3.2},   {630, -1.9},  {800, -0.8},  {1000, 0.0},  {1250, 0.6},   {1600, 1.0},  {2000, 1.2},
    {2500, 1.3}, {3150, 1.2},   {4000, 1.0},  {5000, 0.5},  {6300, -0.1}, {8000, -1.1},  {10000, -2.5}};
} // namespace

TEST_CASE("A weighting matches the IEC table at every sample rate")
{
    for (double fs : kRates)
    {
        const core::dsp::BiquadCascade a = core::dsp::makeAWeighting(fs);
        for (const IecPoint& p : kIecA)
        {
            // 标准表只给到 0.1 dB，低端 10 到 16 Hz 的表值本身与公式差 0.2 到 0.3 dB；10 kHz 的要求是 0.5 dB
            const double tolerance = p.hz == 10000.0 ? 0.5 : 0.3;
            INFO("fs " << fs << " f " << p.hz);
            CHECK(a.magnitudeDb(p.hz, fs) == within(p.db, tolerance));
        }
        // 10 kHz 实际误差远小于要求：双线性在这里差 1.2 到 1.5 dB，匹配幅度的高端节不该退回去
        CHECK(a.magnitudeDb(10000.0, fs) == within(-2.5, 0.1));
        CHECK(a.magnitudeDb(1000.0, fs) == within(0.0, 1e-9));
    }
}

TEST_CASE("A weighting filter run on samples agrees with its analytic response")
{
    const double fs = 48000.0;
    for (double hz : {100.0, 1000.0, 10000.0})
    {
        core::dsp::BiquadCascade a = core::dsp::makeAWeighting(fs);
        const std::vector<double> x = sine(fs, hz, 1.0, 1.0);
        std::vector<double> y;
        for (double v : x)
            y.push_back(a.process(v));
        const std::vector<double> settled(y.begin() + 24000, y.end());
        const double measured = 20.0 * std::log10(rmsOf(settled) / (1.0 / std::sqrt(2.0)));
        CHECK(measured == within(a.magnitudeDb(hz, fs), 0.02));
    }
}

TEST_CASE("K weighting reproduces the published 48 kHz coefficients")
{
    const core::dsp::Biquad shelf = core::dsp::kWeightingShelf(48000.0);
    CHECK(shelf.b0 == within(1.53512485958697, 1e-9));
    CHECK(shelf.b1 == within(-2.69169618940638, 1e-9));
    CHECK(shelf.b2 == within(1.19839281085285, 1e-9));
    CHECK(shelf.a1 == within(-1.69065929318241, 1e-9));
    CHECK(shelf.a2 == within(0.73248077421585, 1e-9));
    const core::dsp::Biquad highpass = core::dsp::kWeightingHighpass(48000.0);
    CHECK(highpass.a1 == within(-1.99004745483398, 1e-9));
    CHECK(highpass.a2 == within(0.99007225036621, 1e-9));
}

TEST_CASE("K weighting frequency response at every rate follows the 48 kHz published response")
{
    const core::dsp::BiquadCascade published(
        {core::dsp::Biquad{1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585},
         core::dsp::Biquad{1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621}});
    for (double fs : kRates)
    {
        const core::dsp::BiquadCascade k = core::dsp::makeKWeighting(fs);
        for (double hz :
             {20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 3000.0, 4000.0, 6000.0, 8000.0, 10000.0, 15000.0})
        {
            INFO("fs " << fs << " f " << hz);
            CHECK(k.magnitudeDb(hz, fs) == within(published.magnitudeDb(hz, 48000.0), 0.1));
        }
    }
}

TEST_CASE("percentile interpolates like the usual definition")
{
    const std::vector<double> v{5, 1, 4, 2, 3};
    CHECK(core::dsp::percentile(v, 0) == within(1.0, 1e-12));
    CHECK(core::dsp::percentile(v, 10) == within(1.4, 1e-12));
    CHECK(core::dsp::percentile(v, 50) == within(3.0, 1e-12));
    CHECK(core::dsp::percentile(v, 100) == within(5.0, 1e-12));
    CHECK(core::dsp::percentile({}, 50) == 0.0);
}

TEST_CASE("decimator keeps level, has unit DC gain and does not depend on block size")
{
    for (int factor : {2, 4})
    {
        const double fs = 48000.0 * factor;
        const std::vector<double> x = sine(fs, 1000.0, 0.5, 1.0);
        core::dsp::Decimator whole(factor);
        std::vector<double> a;
        whole.push(x.data(), x.size(), a);
        core::dsp::Decimator chunked(factor);
        std::vector<double> b;
        Rng rng(7);
        for (std::size_t pos = 0; pos < x.size();)
        {
            const std::size_t length = std::min(x.size() - pos, 1 + static_cast<std::size_t>(rng.uniform() * 500.0));
            chunked.push(x.data() + pos, length, b);
            pos += length;
        }
        REQUIRE(a.size() == b.size());
        CHECK(a == b);
        const std::vector<double> settled(a.begin() + 2000, a.end());
        CHECK(rmsOf(settled) == within(0.5 / std::sqrt(2.0), 1e-3));

        core::dsp::Decimator dc(factor);
        const std::vector<double> ones(20000, 1.0);
        std::vector<double> out;
        dc.push(ones.data(), ones.size(), out);
        CHECK(out.back() == within(1.0, 1e-9));
    }
}
