// SPDX-License-Identifier: GPL-3.0-or-later
#include "voice/VoiceDsp.h"

#include <algorithm>
#include <cmath>

#include "room/Butterworth.h"

namespace miciri::core::voice
{
dsp::BiquadCascade highpass(int order, double hz, double sampleRate)
{
    const double w0 = 2.0 * dsp::kPi * hz / sampleRate;
    const double c = std::cos(w0);
    const double s = std::sin(w0);
    std::vector<dsp::Biquad> stages;
    // 模拟原型的极点在 s 平面左半的圆周上等角分布，第 k 对共轭极点的品质因数是 1 / (2 cos θk)
    for (int k = 0; k < order / 2; ++k)
    {
        const double theta = dsp::kPi * (2.0 * k + 1.0) / (2.0 * order);
        const double alpha = s * std::cos(theta);
        const double a0 = 1.0 + alpha;
        const double edge = (1.0 + c) / 2.0;
        dsp::Biquad q;
        q.b0 = edge / a0;
        q.b1 = -2.0 * edge / a0;
        q.b2 = edge / a0;
        q.a1 = -2.0 * c / a0;
        q.a2 = (1.0 - alpha) / a0;
        stages.push_back(q);
    }
    return dsp::BiquadCascade(std::move(stages));
}

dsp::BiquadCascade bandpass(int order, double loHz, double hiHz, double sampleRate)
{
    return dsp::makeButterworthBandpass(order, loHz, hiHz, sampleRate);
}

std::vector<double> filtered(dsp::BiquadCascade cascade, const std::vector<double>& x)
{
    cascade.reset();
    std::vector<double> y(x.size());
    for (std::size_t i = 0; i < x.size(); ++i)
        y[i] = cascade.process(x[i]);
    return y;
}

std::vector<double> prepare(const std::vector<double>& samples, double sampleRate, double maxRate, double& rate)
{
    dsp::AnalysisInput input(sampleRate, maxRate);
    rate = input.rate();
    std::vector<double> out;
    input.push(samples.data(), samples.size(), out);
    return out;
}

long long offsetSamples(double ms, double rate)
{
    return std::llround(ms * rate / 1000.0);
}

std::vector<double> framePowerDb(const std::vector<double>& x, std::size_t n, std::size_t hop)
{
    std::vector<double> out;
    if (n == 0 || hop == 0 || x.size() < n)
        return out;
    const std::size_t frames = (x.size() - n) / hop + 1;
    out.reserve(frames);
    for (std::size_t f = 0; f < frames; ++f)
    {
        double sum = 0.0;
        for (std::size_t i = 0; i < n; ++i)
            sum += x[f * hop + i] * x[f * hop + i];
        out.push_back(dsp::powerToDb(sum / static_cast<double>(n)));
    }
    return out;
}

Spectrogram spectrogram(const std::vector<double>& x, double rate, double frameMs, double hopMs)
{
    Spectrogram s;
    const auto n = static_cast<std::size_t>(std::max<long long>(2, offsetSamples(frameMs, rate)));
    const auto hop = static_cast<std::size_t>(std::max<long long>(1, offsetSamples(hopMs, rate)));
    s.frameLength = n;
    s.binHz = rate / static_cast<double>(n);
    s.hopSeconds = static_cast<double>(hop) / rate;
    s.frameSeconds = static_cast<double>(n) / rate;
    std::vector<double> window(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        window[i] = 0.5 - 0.5 * std::cos(2.0 * dsp::kPi * static_cast<double>(i) / static_cast<double>(n - 1));
        s.windowPower += window[i] * window[i];
    }
    if (x.size() < n)
        return s;
    std::vector<double> frame(n);
    for (std::size_t start = 0; start + n <= x.size(); start += hop)
    {
        for (std::size_t i = 0; i < n; ++i)
            frame[i] = x[start + i] * window[i];
        s.power.emplace_back();
        dsp::powerSpectrum(frame.data(), n, s.power.back());
    }
    return s;
}

double bandSum(const std::vector<double>& power, double binHz, double loHz, double hiHz)
{
    double sum = 0.0;
    for (std::size_t k = 0; k < power.size(); ++k)
    {
        const double f = static_cast<double>(k) * binHz;
        if (f >= loHz && f < hiHz)
            sum += power[k];
    }
    return sum;
}

double bandLevelDb(double powerSum, const Spectrogram& s)
{
    // 单边谱：均方 = 2 × Σ|X|² / (N × Σw²)（帕塞瓦尔，频带不含直流与奈奎斯特 bin）
    const double denom = static_cast<double>(s.frameLength) * s.windowPower;
    return dsp::powerToDb(denom > 0.0 ? 2.0 * powerSum / denom : 0.0);
}

void keepStrongest(std::vector<EvidenceMark>& marks, std::size_t maxMarks)
{
    if (marks.size() > maxMarks)
    {
        std::stable_sort(marks.begin(), marks.end(),
                         [](const EvidenceMark& a, const EvidenceMark& b) { return a.value > b.value; });
        marks.resize(maxMarks);
    }
    std::stable_sort(marks.begin(), marks.end(),
                     [](const EvidenceMark& a, const EvidenceMark& b) { return a.startSec < b.startSec; });
}
} // namespace miciri::core::voice
