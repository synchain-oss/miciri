// SPDX-License-Identifier: GPL-3.0-or-later
//
// 窗口的设计盒与缩放档位（C++ 一侧）。窗口尺寸 = 设计盒 × 缩放档位，页面在盒内按同一比例排版。
// 1120×700 是原型阶段的暂定值，页面一侧的设计盒常量落地后要和这里对拍；改尺寸或档位表时两边一起改。
#pragma once

#include <array>

namespace miciri::design
{
inline constexpr int kDesignW = 1120;
inline constexpr int kDesignH = 700;

inline constexpr std::array<double, 7> kPresets = {0.5, 0.65, 0.8, 1.0, 1.25, 1.5, 2.0};

// 宿主给插件窗加的框（标题栏加工具条）的估计值，用来判断某个档位在当前屏幕上放不放得下。
inline constexpr int kHostFrameW = 16;
inline constexpr int kHostFrameH = 64;
} // namespace miciri::design
