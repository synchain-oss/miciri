// SPDX-License-Identifier: GPL-3.0-or-later
//
// 测试用的合成信号：全部由固定种子的整数随机数生成，所以每个平台逐样本一致。
#pragma once

#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace miciri::testing
{
constexpr double kPi = 3.14159265358979323846264338327950288;
inline const std::vector<double> kRates{44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0};

inline Catch::Approx within(double value, double tolerance)
{
    return Catch::Approx(value).margin(tolerance).epsilon(0.0);
}

inline double fromDb(double db)
{
    return std::pow(10.0, db / 20.0);
}

inline double rmsOf(const std::vector<double>& x)
{
    double sum = 0.0;
    for (double v : x)
        sum += v * v;
    return x.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(x.size()));
}

// 抽取倍数（与分析代码里 48 kHz 上限的口径一致）
inline double decimation(double fs)
{
    return fs > 96000.0 ? 4.0 : (fs > 48000.0 ? 2.0 : 1.0);
}

class Rng
{
public:
    explicit Rng(std::uint64_t seed) : state_(seed * 0x9E3779B97F4A7C15ULL + 0x1234567ULL) {}
    std::uint64_t next()
    {
        state_ ^= state_ >> 12;
        state_ ^= state_ << 25;
        state_ ^= state_ >> 27;
        return state_ * 0x2545F4914F6CDD1DULL;
    }
    double uniform() { return static_cast<double>(next() >> 11) / 9007199254740992.0; }
    // 十二个均匀数之和减 6：方差为 1 的近似高斯
    double gauss()
    {
        double s = -6.0;
        for (int i = 0; i < 12; ++i)
            s += uniform();
        return s;
    }

private:
    std::uint64_t state_;
};

inline std::vector<double> sine(double fs, double hz, double amplitude, double seconds, double phase = 0.0)
{
    std::vector<double> x(static_cast<std::size_t>(seconds * fs));
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = amplitude * std::sin(2.0 * kPi * hz * static_cast<double>(i) / fs + phase);
    return x;
}

inline std::vector<double> whiteNoise(std::size_t n, double rms, std::uint64_t seed)
{
    Rng rng(seed);
    std::vector<double> x(n);
    for (double& v : x)
        v = rms * rng.gauss();
    return x;
}

// 一阶低通后的噪声，整体归一到给定 RMS
inline std::vector<double> lowpassNoise(double fs, std::size_t n, double rms, double cutoffHz, std::uint64_t seed)
{
    std::vector<double> x = whiteNoise(n, 1.0, seed);
    const double a = 1.0 - std::exp(-2.0 * kPi * cutoffHz / fs);
    double y = 0.0;
    for (double& v : x)
    {
        y += a * (v - y);
        v = y;
    }
    const double scale = rms / rmsOf(x);
    for (double& v : x)
        v *= scale;
    return x;
}

inline void mix(std::vector<double>& target, const std::vector<double>& other, double gain = 1.0)
{
    for (std::size_t i = 0; i < std::min(target.size(), other.size()); ++i)
        target[i] += gain * other[i];
}

// 像说话的信号：几十到几百毫秒的元音音节（100 到 200 Hz 的基频加 10 次谐波），音节之间有停顿，开头可留静音。
// 非零部分的 RMS 归一为 activeRms。
inline std::vector<double> voice(double fs, double seconds, double activeRms, std::uint64_t seed,
                                 double leadInSeconds = 0.0)
{
    Rng rng(seed);
    std::vector<double> x(static_cast<std::size_t>(seconds * fs), 0.0);
    double t = leadInSeconds;
    while (t < seconds - 0.3)
    {
        const double duration = 0.15 + 0.15 * rng.uniform();
        const double f0 = 100.0 + 100.0 * rng.uniform();
        double phase[10];
        for (double& p : phase)
            p = 2.0 * kPi * rng.uniform();
        const auto start = static_cast<std::size_t>(t * fs);
        const auto length = static_cast<std::size_t>(duration * fs);
        for (std::size_t i = 0; i < length && start + i < x.size(); ++i)
        {
            const double envelope =
                0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(length));
            double s = 0.0;
            for (int k = 1; k <= 10; ++k)
                s += std::sin(2.0 * kPi * k * f0 * static_cast<double>(i) / fs + phase[k - 1]) / k;
            x[start + i] = envelope * s;
        }
        t += duration + 0.1 + 0.15 * rng.uniform();
    }
    double sum = 0.0;
    std::size_t count = 0;
    for (double v : x)
        if (v != 0.0)
        {
            sum += v * v;
            ++count;
        }
    const double scale = count ? activeRms / std::sqrt(sum / static_cast<double>(count)) : 0.0;
    for (double& v : x)
        v *= scale;
    return x;
}

// 电流声：基频 f0，各次谐波按 profileDb（dB，相对）给幅度，总功率归一为 totalPower
inline std::vector<double> humSignal(double fs, double seconds, double f0, const std::vector<double>& profileDb,
                                     double totalPower)
{
    std::vector<double> x(static_cast<std::size_t>(seconds * fs), 0.0);
    double norm = 0.0;
    for (double p : profileDb)
        norm += std::pow(10.0, p / 10.0) / 2.0;
    const double scale = std::sqrt(totalPower / norm);
    for (std::size_t k = 1; k <= profileDb.size(); ++k)
    {
        const double amplitude = scale * fromDb(profileDb[k - 1]);
        const double hz = static_cast<double>(k) * f0;
        for (std::size_t i = 0; i < x.size(); ++i)
            x[i] += amplitude * std::sin(2.0 * kPi * hz * static_cast<double>(i) / fs + 0.7 * static_cast<double>(k));
    }
    return x;
}

inline const std::vector<double> kHumProfileDb{-6, 0, -8, -6, -12, -10, -14, -12};

// 不规则分块地喂给任意带 push(const double*, size_t) 的分析器
template<class Analyser>
void feedInRandomBlocks(Analyser& analyser, const std::vector<double>& x, std::uint64_t seed)
{
    Rng rng(seed);
    std::size_t position = 0;
    while (position < x.size())
    {
        const std::size_t length = std::min(x.size() - position, 1 + static_cast<std::size_t>(rng.uniform() * 3000.0));
        analyser.push(x.data() + position, length);
        position += length;
    }
}
} // namespace miciri::testing
