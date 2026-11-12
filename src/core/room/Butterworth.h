// SPDX-License-Identifier: GPL-3.0-or-later
//
// Butterworth 带通：模拟低通原型经低通到带通变换，再用双线性变换到数字域（两个边缘频率先预畸变），拆成二阶节。
// order 是原型的阶数：4 阶原型得到 8 阶带通、4 个二阶节。几何中心频率处增益归一为 0 dB。
// 上沿超过奈奎斯特频率的 45% 时压到那里，避免预畸变在奈奎斯特附近发散。
#pragma once

#include "level/Dsp.h"

namespace miciri::core::dsp
{
BiquadCascade makeButterworthBandpass(int order, double lowHz, double highHz, double sampleRate);
} // namespace miciri::core::dsp
