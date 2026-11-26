// SPDX-License-Identifier: GPL-3.0-or-later
//
// 流式真峰值：与整段的 truePeak 数值一致，与分块方式无关；采样点之间的峰能读到；起点不因零延拓虚报；
// NaN 与 Inf 不让状态失效。
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "core/level/Synth.h"
#include "level/Dsp.h"

using namespace miciri;
using namespace miciri::testing;
using miciri::core::dsp::StreamingTruePeak;

namespace
{
// 把整段信号按给定块长喂进去，返回各块返回值的最大值（再与样本峰值取大，和实时电平表的用法一致）。
template<class Sample>
double streamPeak(const std::vector<Sample>& x, int factor, int block)
{
    StreamingTruePeak tp;
    tp.prepare(factor);
    double peak = 0.0;
    for (std::size_t i = 0; i < x.size(); i += static_cast<std::size_t>(block))
    {
        const int n = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(block), x.size() - i));
        peak = std::max(peak, tp.push(x.data() + i, n));
    }
    for (Sample v : x)
        if (std::isfinite(static_cast<double>(v)))
            peak = std::max(peak, std::fabs(static_cast<double>(v)));
    return peak;
}

std::vector<double> noisyTone()
{
    Rng rng(7);
    std::vector<double> x = sine(48000.0, 9000.0, 0.6, 0.5, 0.3);
    for (double& v : x)
        v += 0.1 * rng.gauss();
    return x;
}
} // namespace

TEST_CASE("streaming true peak agrees with the whole-signal one, whatever the block size")
{
    const std::vector<double> x = noisyTone();
    for (int factor : {2, 4})
    {
        const double whole = core::dsp::truePeak(x.data(), x.size(), factor);
        for (int block : {1, 7, 64, 480, 4096})
            CHECK(streamPeak(x, factor, block) == within(whole, 1e-12));
    }
}

TEST_CASE("streaming true peak reads single-precision input the same way")
{
    const std::vector<double> x = noisyTone();
    std::vector<float> f;
    std::vector<double> back; // 单精度取整之后的值
    for (double v : x)
    {
        f.push_back(static_cast<float>(v));
        back.push_back(static_cast<double>(f.back()));
    }
    CHECK(streamPeak(f, 4, 128) == within(core::dsp::truePeak(back.data(), back.size(), 4), 1e-12));
}

TEST_CASE("the peak between samples is seen: a quarter-rate sine at 45 degrees")
{
    // 样本全是 ±0.707 A，连续波形的峰是 A；样本峰值会少报 3 dB
    const std::vector<double> x = sine(48000.0, 12000.0, 0.9, 0.2, kPi / 4.0);
    const double samplePeak = *std::max_element(x.begin(), x.end());
    CHECK(samplePeak == within(0.9 * std::sqrt(0.5), 1e-9));
    CHECK(streamPeak(x, 4, 128) == within(0.9, 0.01));
    CHECK(streamPeak(x, 2, 128) == within(0.9, 0.01));
}

TEST_CASE("no false overshoot while the history fills: a signal that starts at full scale")
{
    // 起点之前的零延拓会把一个直流台阶插值成过冲；攒满一个窗口之前不输出，台阶之后是平的就不该超过 1
    const std::vector<double> x(512, 1.0);
    StreamingTruePeak tp;
    tp.prepare(4);
    CHECK(tp.push(x.data(), 19) == 0.0);
    CHECK(tp.push(x.data() + 19, 493) == within(1.0, 1e-12));
}

TEST_CASE("factor below two leaves only the sample peak")
{
    const std::vector<double> x = sine(48000.0, 12000.0, 0.9, 0.05, kPi / 4.0);
    StreamingTruePeak tp;
    tp.prepare(1);
    CHECK(tp.push(x.data(), static_cast<int>(x.size())) == 0.0);
    tp.prepare(8); // 超过上限的倍数按上限算
    CHECK(tp.push(x.data(), static_cast<int>(x.size())) == within(0.9, 0.01));
}

TEST_CASE("NaN and Inf count as zero and do not poison later blocks")
{
    const std::vector<double> x = sine(48000.0, 1000.0, 0.5, 0.1);
    std::vector<double> bad(64, std::numeric_limits<double>::quiet_NaN());
    bad[3] = std::numeric_limits<double>::infinity();
    bad[4] = -std::numeric_limits<double>::infinity();
    const std::vector<double> zeros(64, 0.0);

    StreamingTruePeak withBad;
    withBad.prepare(4);
    CHECK(std::isfinite(withBad.push(bad.data(), 64)));
    const double afterBad = withBad.push(x.data(), static_cast<int>(x.size()));
    StreamingTruePeak withZeros;
    withZeros.prepare(4);
    withZeros.push(zeros.data(), 64);
    // 坏块当 0 进历史：之后的读数与先喂一块静音完全相同，且是有限值
    CHECK(std::isfinite(afterBad));
    CHECK(afterBad == withZeros.push(x.data(), static_cast<int>(x.size())));
}
