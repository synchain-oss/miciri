// SPDX-License-Identifier: GPL-3.0-or-later
//
// 分析共用的基本件：dB 换算、百分位、二阶节级联（A 加权与 K 加权共用）、整数倍抽取、真峰值插值、FFT 功率谱。
// 不依赖 JUCE，double 精度，没有全局状态。
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace miciri::core::dsp
{
inline constexpr double kPi = 3.14159265358979323846264338327950288;
// 取对数前的功率下限：静音落在 -300 dB，而不是 -inf（-inf 会污染 JSON 与 golden）
inline constexpr double kPowerFloor = 1e-30;

// NaN 与 Inf 不能让分析状态失效：进入滤波器和累加器之前一律当作 0
inline double finiteOrZero(double x)
{
    return std::isfinite(x) ? x : 0.0;
}

double powerToDb(double power); // 10 log10，下限 -300
double amplitudeToDb(double amplitude); // 20 log10，下限 -300
double dbToPower(double db);

// 百分位（0 到 100），相邻两个次序统计量之间线性插值，和 numpy 的默认一致。空输入返回 0。
double percentile(std::vector<double> values, double pct);
double median(std::vector<double> values);

// 定长帧的均方值：逐样本累加，帧满了就记一项。累加顺序只取决于样本顺序，所以与分块方式无关。
class FrameMeanSquare
{
public:
    explicit FrameMeanSquare(std::size_t frameLength) : length_(frameLength == 0 ? 1 : frameLength) {}
    void add(double sample)
    {
        sum_ += sample * sample;
        if (++count_ == length_)
        {
            frames_.push_back(sum_ / static_cast<double>(length_));
            sum_ = 0.0;
            count_ = 0;
        }
    }
    const std::vector<double>& frames() const { return frames_; }

private:
    std::size_t length_;
    std::size_t count_ = 0;
    double sum_ = 0.0;
    std::vector<double> frames_;
};

// 二阶节，直接 II 转置型，a0 已归一化为 1。
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};

class BiquadCascade
{
public:
    BiquadCascade() = default;
    explicit BiquadCascade(std::vector<Biquad> stages);
    double process(double x);
    void reset();
    // 解析的幅频响应（dB）：给测试对照标准表用，不走样本
    double magnitudeDb(double hz, double sampleRate) const;
    // 整体乘一个线性增益（并入第一节的分子）
    void scale(double gain);
    const std::vector<Biquad>& stages() const { return stages_; }

private:
    std::vector<Biquad> stages_;
    std::vector<std::array<double, 2>> state_;
};

// A 加权（IEC 61672 的零极点），1 kHz 处归一为 0 dB。低端三个转折点用预畸变的双线性变换，
// 高端的 12.19 kHz 双极点用匹配幅度的二阶节：纯双线性在 10 kHz 差 1.2 到 1.5 dB，这里在所有采样率上不超过 0.05 dB。
BiquadCascade makeAWeighting(double sampleRate);

// 抽取倍数：使 sampleRate / 倍数 不超过 maxRate 的最小整数。
int decimationFactor(double sampleRate, double maxRate);

// 整数倍抽取：窗函数 sinc 低通加每 factor 个取一个。带状态，所以任意分块得到同一个输出序列。
// 输出相对输入有固定的群延迟，开头有一小段滤波器起振。
class Decimator
{
public:
    explicit Decimator(int factor);
    int factor() const { return factor_; }
    void push(const double* samples, std::size_t count, std::vector<double>& out);

private:
    int factor_;
    std::vector<double> kernel_;
    std::vector<double> buffer_;
    long long base_; // buffer_[0] 对应的输入序号
    long long nextOutput_ = 0;
    long long total_ = 0;
};

// 分析入口：样本先清理（NaN、Inf 当 0），再抽取到不超过 maxRate 的分析采样率（≤ maxRate 的采样率原样通过）。
class AnalysisInput
{
public:
    AnalysisInput(double sampleRate, double maxRate)
        : decimator_(decimationFactor(sampleRate, maxRate)), rate_(sampleRate / decimator_.factor())
    {
    }
    double rate() const { return rate_; }
    void push(const double* samples, std::size_t count, std::vector<double>& out)
    {
        clean_.resize(count);
        for (std::size_t i = 0; i < count; ++i)
            clean_[i] = finiteOrZero(samples[i]);
        decimator_.push(clean_.data(), count, out);
    }

private:
    Decimator decimator_;
    double rate_;
    std::vector<double> clean_;
};

// 真峰值（线性幅度）：窗函数 sinc 插值 factor 倍后取最大绝对值；factor 小于 2 时就是样本峰值。
// 两端各留出插值核的一半长度不插值（零延拓会在边缘制造假的过冲），那一段只看样本值。
double truePeak(const double* samples, std::size_t count, int factor);

// 实数序列的功率谱 |X[k]|²，k = 0 到 n/2，不加窗不缩放。
void powerSpectrum(const double* samples, std::size_t count, std::vector<double>& out);
} // namespace miciri::core::dsp
