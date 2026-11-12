// SPDX-License-Identifier: GPL-3.0-or-later
#include "room/Butterworth.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace miciri::core::dsp
{
namespace
{
using Complex = std::complex<double>;

// 上沿最多到采样率的这个比例（奈奎斯特的 90%）：只是数值保护，不是可调的分析参数
constexpr double kMaxEdgeOfRate = 0.45;

Complex bilinear(Complex s, double sampleRate)
{
    return (2.0 * sampleRate + s) / (2.0 * sampleRate - s);
}

// 一对极点加分子 1 - z^-2：零点一个在 z = 1（模拟域 s = 0），一个在 z = -1（模拟域无穷远）
Biquad stageFromPoles(Complex a, Complex b)
{
    Biquad q;
    q.b0 = 1.0;
    q.b1 = 0.0;
    q.b2 = -1.0;
    q.a1 = -(a + b).real();
    q.a2 = (a * b).real();
    return q;
}
} // namespace

BiquadCascade makeButterworthBandpass(int order, double lowHz, double highHz, double sampleRate)
{
    const int n = std::max(1, order);
    const double hi = std::min(highHz, kMaxEdgeOfRate * sampleRate);
    const double lo = std::clamp(lowHz, 1e-6 * sampleRate, hi);
    const double w1 = 2.0 * sampleRate * std::tan(kPi * lo / sampleRate);
    const double w2 = 2.0 * sampleRate * std::tan(kPi * hi / sampleRate);
    const double w0 = std::sqrt(w1 * w2);
    const double bw = w2 - w1;

    // 每个原型极点 p 变成 s = p*bw/2 ± sqrt((p*bw/2)^2 - w0^2) 两个极点；共轭对只取上半平面的那个
    std::vector<Biquad> stages;
    std::vector<Complex> realPoles;
    for (int k = 0; k < n; ++k)
    {
        const Complex p = std::polar(1.0, kPi * (2.0 * k + n + 1.0) / (2.0 * n));
        const Complex half = p * (0.5 * bw);
        const Complex root = std::sqrt(half * half - w0 * w0);
        for (const Complex s : {half + root, half - root})
        {
            const Complex z = bilinear(s, sampleRate);
            if (std::abs(z.imag()) <= 1e-12 * std::abs(z))
                realPoles.push_back({z.real(), 0.0});
            else if (z.imag() > 0.0)
                stages.push_back(stageFromPoles(z, std::conj(z)));
        }
    }
    for (std::size_t i = 0; i + 1 < realPoles.size(); i += 2)
        stages.push_back(stageFromPoles(realPoles[i], realPoles[i + 1]));

    BiquadCascade cascade(std::move(stages));
    const double centreHz = sampleRate / kPi * std::atan(w0 / (2.0 * sampleRate));
    cascade.scale(std::pow(10.0, -cascade.magnitudeDb(centreHz, sampleRate) / 20.0));
    return cascade;
}
} // namespace miciri::core::dsp
