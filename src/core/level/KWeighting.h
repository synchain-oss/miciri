// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from synchain-oss/scvb; see upstream/dsp.json
//
// K 加权（响度测量用的两级滤波）：高频搁架加低频高通。系数由模拟原型的参数在任意采样率下现算，
// 48 kHz 时与 ITU-R BS.1770 公布的系数一致到 1e-10。
#pragma once

#include "level/Dsp.h"

namespace miciri::core::dsp
{
Biquad kWeightingShelf(double sampleRate); // 约 1.68 kHz 的高频搁架，抬 4 dB
Biquad kWeightingHighpass(double sampleRate); // 约 38 Hz 的高通
BiquadCascade makeKWeighting(double sampleRate);
} // namespace miciri::core::dsp
