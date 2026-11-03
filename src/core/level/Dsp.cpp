// SPDX-License-Identifier: GPL-3.0-or-later
#include "level/Dsp.h"

#include <algorithm>
#include <cmath>
#include <complex>

// 分析代码从不起线程；必须在包含 pocketfft 之前定义
#define POCKETFFT_NO_MULTITHREADING
#include <pocketfft_hdronly.h>

namespace miciri::core::dsp
{
double powerToDb(double power)
{
    return 10.0 * std::log10(std::max(power, kPowerFloor));
}

double amplitudeToDb(double amplitude)
{
    return powerToDb(amplitude * amplitude);
}

double dbToPower(double db)
{
    return std::pow(10.0, db / 10.0);
}

double percentile(std::vector<double> values, double pct)
{
    if (values.empty())
        return 0.0;
    std::sort(values.begin(), values.end());
    const double position = std::clamp(pct, 0.0, 100.0) / 100.0 * static_cast<double>(values.size() - 1);
    const auto lower = static_cast<std::size_t>(std::floor(position));
    const std::size_t upper = std::min(lower + 1, values.size() - 1);
    return values[lower] + (values[upper] - values[lower]) * (position - static_cast<double>(lower));
}

double median(std::vector<double> values)
{
    return percentile(std::move(values), 50.0);
}

BiquadCascade::BiquadCascade(std::vector<Biquad> stages) : stages_(std::move(stages)), state_(stages_.size())
{
    reset();
}

void BiquadCascade::reset()
{
    for (auto& s : state_)
        s = {0.0, 0.0};
}

double BiquadCascade::process(double x)
{
    for (std::size_t i = 0; i < stages_.size(); ++i)
    {
        const Biquad& c = stages_[i];
        auto& z = state_[i];
        const double y = c.b0 * x + z[0];
        z[0] = c.b1 * x - c.a1 * y + z[1];
        z[1] = c.b2 * x - c.a2 * y;
        x = y;
    }
    return x;
}

double BiquadCascade::magnitudeDb(double hz, double sampleRate) const
{
    const double w = 2.0 * kPi * hz / sampleRate;
    const std::complex<double> z1 = std::polar(1.0, -w);
    const std::complex<double> z2 = std::polar(1.0, -2.0 * w);
    double magnitude = 1.0;
    for (const Biquad& c : stages_)
        magnitude *= std::abs(c.b0 + c.b1 * z1 + c.b2 * z2) / std::abs(1.0 + c.a1 * z1 + c.a2 * z2);
    return 20.0 * std::log10(magnitude);
}

void BiquadCascade::scale(double gain)
{
    if (stages_.empty())
        return;
    stages_[0].b0 *= gain;
    stages_[0].b1 *= gain;
    stages_[0].b2 *= gain;
}

namespace
{
// 一阶高通 s/(s+ω)，预畸变的双线性变换，转折点落在 hz
Biquad highpass1(double hz, double fs)
{
    const double k = std::tan(kPi * hz / fs);
    return {1.0 / (1.0 + k), -1.0 / (1.0 + k), 0.0, (k - 1.0) / (1.0 + k), 0.0};
}

// 二阶高通 s²/(s+ω)²（双重实极点）
Biquad highpass2(double hz, double fs)
{
    const double k = std::tan(kPi * hz / fs);
    const double a0 = (1.0 + k) * (1.0 + k);
    return {1.0 / a0, -2.0 / a0, 1.0 / a0, 2.0 * (k * k - 1.0) / a0, (1.0 - k) * (1.0 - k) / a0};
}

// 二阶低通 1/(1+s/ω)²（Q = 1/2）的匹配幅度实现：极点用 exp(-ω/fs)，一阶分子使直流增益为 1，且
// 在转折频率处与模拟原型相等；所以接近 Nyquist 时也不会像双线性那样被压低。
Biquad matchedLowpass2(double hz, double fs)
{
    constexpr double q = 0.5;
    const double w0 = 2.0 * kPi * hz / fs;
    const double pole = std::exp(-w0);
    const double a1 = -2.0 * pole;
    const double a2 = pole * pole;
    const double sinHalf2 = std::sin(w0 / 2.0) * std::sin(w0 / 2.0);
    const double phi0 = 1.0 - sinHalf2;
    const double phi1 = sinHalf2;
    const double phi2 = 4.0 * phi0 * phi1;
    const double aa0 = (1.0 + a1 + a2) * (1.0 + a1 + a2);
    const double aa1 = (1.0 - a1 + a2) * (1.0 - a1 + a2);
    const double aa2 = -4.0 * a2;
    const double r1 = (aa0 * phi0 + aa1 * phi1 + aa2 * phi2) * q * q;
    const double bb1 = (r1 - aa0 * phi0) / phi1;
    const double b0 = 0.5 * (std::sqrt(aa0) + std::sqrt(bb1));
    return {b0, std::sqrt(aa0) - b0, 0.0, a1, a2};
}

double besselI0(double x)
{
    double sum = 1.0;
    double term = 1.0;
    for (int k = 1; k < 64; ++k)
    {
        const double half = x / (2.0 * k);
        term *= half * half;
        sum += term;
        if (term < 1e-17 * sum)
            break;
    }
    return sum;
}

// Kaiser 窗，x 取 -1 到 1
double kaiser(double x, double beta)
{
    const double inside = 1.0 - x * x;
    return inside <= 0.0 ? 0.0 : besselI0(beta * std::sqrt(inside)) / besselI0(beta);
}

double sinc(double x)
{
    return std::fabs(x) < 1e-12 ? 1.0 : std::sin(kPi * x) / (kPi * x);
}

// 插值核与抽取核的单边长度（以较低那个采样率的间隔计）与 Kaiser 的 β，和常见的重采样器取值一致
constexpr int kKernelHalfLength = 10;
constexpr double kKaiserBeta = 5.0;
} // namespace

BiquadCascade makeAWeighting(double sampleRate)
{
    // IEC 61672 的 A 加权转折频率（Hz）与归一用的参考频率
    constexpr double f1 = 20.598997;
    constexpr double f2 = 107.65265;
    constexpr double f3 = 737.86223;
    constexpr double f4 = 12194.217;
    constexpr double referenceHz = 1000.0;
    BiquadCascade cascade({highpass2(f1, sampleRate), highpass1(f2, sampleRate), highpass1(f3, sampleRate),
                           matchedLowpass2(f4, sampleRate)});
    cascade.scale(std::pow(10.0, -cascade.magnitudeDb(referenceHz, sampleRate) / 20.0));
    return cascade;
}

int decimationFactor(double sampleRate, double maxRate)
{
    int factor = 1;
    while (sampleRate / factor > maxRate * (1.0 + 1e-9))
        ++factor;
    return factor;
}

Decimator::Decimator(int factor) : factor_(std::max(1, factor)), base_(0)
{
    if (factor_ == 1)
        return;
    const int half = kKernelHalfLength * factor_;
    const double cutoff = 1.0 / factor_;
    kernel_.resize(static_cast<std::size_t>(2 * half + 1));
    double sum = 0.0;
    for (int i = -half; i <= half; ++i)
    {
        const double h = cutoff * sinc(i * cutoff) * kaiser(static_cast<double>(i) / half, kKaiserBeta);
        kernel_[static_cast<std::size_t>(i + half)] = h;
        sum += h;
    }
    for (double& h : kernel_)
        h /= sum; // 直流增益恰为 1
    base_ = -static_cast<long long>(kernel_.size() - 1);
    buffer_.assign(kernel_.size() - 1, 0.0);
}

void Decimator::push(const double* samples, std::size_t count, std::vector<double>& out)
{
    if (factor_ == 1)
    {
        out.insert(out.end(), samples, samples + count);
        return;
    }
    buffer_.insert(buffer_.end(), samples, samples + count);
    total_ += static_cast<long long>(count);
    const auto taps = static_cast<long long>(kernel_.size());
    while (nextOutput_ < total_)
    {
        double y = 0.0;
        for (long long j = 0; j < taps; ++j)
            y += kernel_[static_cast<std::size_t>(j)] * buffer_[static_cast<std::size_t>(nextOutput_ - j - base_)];
        out.push_back(y);
        nextOutput_ += factor_;
    }
    const long long drop = (nextOutput_ - (taps - 1)) - base_;
    if (drop > 0)
    {
        buffer_.erase(buffer_.begin(), buffer_.begin() + drop);
        base_ += drop;
    }
}

double truePeak(const double* samples, std::size_t count, int factor)
{
    double peak = 0.0;
    for (std::size_t i = 0; i < count; ++i)
        peak = std::max(peak, std::fabs(samples[i]));
    const auto half = static_cast<std::size_t>(kKernelHalfLength);
    if (factor < 2 || count < 2 * half + 1)
        return peak;
    // coef[p - 1][t]：n + p/factor 处的值 = Σ_t coef[t] * x[n + half - t]（t = d + half，d 取 -half 到 half-1，x[n -
    // d]）
    std::vector<std::vector<double>> coef(static_cast<std::size_t>(factor - 1), std::vector<double>(2 * half));
    for (int p = 1; p < factor; ++p)
    {
        auto& row = coef[static_cast<std::size_t>(p - 1)];
        double sum = 0.0;
        for (int d = -kKernelHalfLength; d < kKernelHalfLength; ++d)
        {
            const double tau = static_cast<double>(p) / factor + d;
            const double h = sinc(tau) * kaiser(tau / kKernelHalfLength, kKaiserBeta);
            row[static_cast<std::size_t>(d + kKernelHalfLength)] = h;
            sum += h;
        }
        for (double& h : row)
            h /= sum;
    }
    for (std::size_t n = half - 1; n + half < count; ++n)
    {
        for (const auto& row : coef)
        {
            double y = 0.0;
            for (std::size_t t = 0; t < 2 * half; ++t)
                y += row[t] * samples[n + half - t];
            peak = std::max(peak, std::fabs(y));
        }
    }
    return peak;
}

void powerSpectrum(const double* samples, std::size_t count, std::vector<double>& out)
{
    std::vector<std::complex<double>> spectrum(count / 2 + 1);
    pocketfft::r2c<double>(pocketfft::shape_t{count}, pocketfft::stride_t{static_cast<std::ptrdiff_t>(sizeof(double))},
                           pocketfft::stride_t{static_cast<std::ptrdiff_t>(sizeof(std::complex<double>))}, 0, true,
                           samples, spectrum.data(), 1.0);
    out.resize(spectrum.size());
    for (std::size_t k = 0; k < spectrum.size(); ++k)
        out[k] = std::norm(spectrum[k]);
}
} // namespace miciri::core::dsp
