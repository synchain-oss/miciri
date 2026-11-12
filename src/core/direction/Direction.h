// SPDX-License-Identifier: GPL-3.0-or-later
//
// 方向：只回答“当前方向是不是最响”，不猜话筒型号。
// 每个方向唱一段：K 加权后 frame_ms 帧，活跃帧是落在第 active_top_percentile 百分位帧电平 active_within_top_db
// 以内的帧，电平取活跃帧电平的中位数。亮度（3–10 kHz 对 300–1000 Hz，只在活跃帧上算）只显示，不参与判断：
// 从背面进来的主要是房间声，亮度反而可能比正面高。
//
// 判断：四个方向最响与最轻差不到 small_difference_db 说“差别不大”；当前方向就是最响的说“最响”；
// 别的方向比当前响 repeat_trigger_db 以上时要重测那一对，两次都成立才说“可能放反”；其余一律“不确定”。
#pragma once

#include <array>
#include <optional>
#include <vector>

#include "result/Results.h"

namespace miciri::core
{
struct DirectionTake
{
    double levelDb = 0.0; // 活跃帧 K 加权电平的中位数（dBFS）
    double brightnessDb = 0.0;
    bool active = false; // 有活跃帧；没有时两个数都是 0
};
DirectionTake measureDirectionTake(const std::vector<double>& samples, double sampleRate);

// 确认重测：把当前方向与那个更响的方向各再唱一次
struct DirectionRepeat
{
    double currentLevelDb = 0.0;
    double otherLevelDb = 0.0;
};

struct DirectionAnalysis
{
    DirectionResult result;
    bool needsRepeat = false; // 还要重测当前方向与 repeatIndex 这一对
    int repeatIndex = -1; // 最响的别的方向
    double confirmedAdvantageDb = 0.0; // 两次里较小的那个优势；没确认过时为 0
};
// takes 按方向编号排列（0 到 3），currentIndex 是用户当前的方向。repeat 在 needsRepeat 之后由重测给出。
DirectionAnalysis decideDirection(const std::array<DirectionTake, 4>& takes, int currentIndex,
                                  const std::optional<DirectionRepeat>& repeat = std::nullopt);
} // namespace miciri::core
