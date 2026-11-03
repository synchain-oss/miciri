// SPDX-License-Identifier: GPL-3.0-or-later
//
// 声道检查（只在立体声轨上做，说话时判断；不拦测试，不进“最该改”）。
// 20 ms 帧；每路底噪取帧功率的 P5，需要足够长的停顿才可信。“差”（L-R）与“和”（L+R）都和两路底噪之和比；
// 读不到底噪（没有停顿）时回退到相对信号的阈值，此时“一路明显更响”只认差达到 one_sided_delta_db
// 的情形。按这个顺序判：一路全零、一路明显更响、两路相位相反、两路相同、真立体声。 分析哪一路由 analysedChannel
// 给出，不把两路相加。
#pragma once

#include <vector>

#include "result/Results.h"

namespace miciri::core
{
ChannelsResult analyseChannels(const std::vector<double>& left, const std::vector<double>& right, double sampleRate);
} // namespace miciri::core
