// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from synchain-oss/scvb; see upstream/dsp.json
#include "level/KWeighting.h"

#include <cmath>

namespace miciri::core::dsp
{
Biquad kWeightingShelf(double sampleRate)
{
    // 模拟原型的参数：转折频率、搁架增益与 Q，由 48 kHz 的公布系数反推
    constexpr double f0 = 1681.9744509555319;
    constexpr double gainDb = 3.999843853973347;
    constexpr double q = 0.7071752369554196;
    constexpr double bandExponent = 0.4996667741545416;
    const double k = std::tan(kPi * f0 / sampleRate);
    const double vh = std::pow(10.0, gainDb / 20.0);
    const double vb = std::pow(vh, bandExponent);
    const double d = 1.0 + k / q + k * k;
    return {(vh + vb * k / q + k * k) / d, 2.0 * (k * k - vh) / d, (vh - vb * k / q + k * k) / d,
            2.0 * (k * k - 1.0) / d, (1.0 - k / q + k * k) / d};
}

Biquad kWeightingHighpass(double sampleRate)
{
    constexpr double f0 = 38.13547087602444;
    constexpr double q = 0.5003270373238773;
    const double k = std::tan(kPi * f0 / sampleRate);
    const double d = 1.0 + k / q + k * k;
    return {1.0, -2.0, 1.0, 2.0 * (k * k - 1.0) / d, (1.0 - k / q + k * k) / d};
}

BiquadCascade makeKWeighting(double sampleRate)
{
    return BiquadCascade({kWeightingShelf(sampleRate), kWeightingHighpass(sampleRate)});
}
} // namespace miciri::core::dsp
