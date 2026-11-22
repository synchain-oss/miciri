// SPDX-License-Identifier: GPL-3.0-or-later
//
// 由各项结果合成的判断：房间建议、距离标尺、方向测试的提示方式。纯函数，不读样本。
// 建议的枚举（RoomAdvice、DistanceAdvice、DirectionPrompt）进了桥名表，定义在 result/Enums.h 末尾。
#pragma once

#include <optional>

#include "direction/Direction.h"
#include "result/Results.h"

namespace miciri::core
{
// ---- 房间建议：只看 T20 ----
// D/R 不参与：拍手 D/R 相对扫频真值的偏移在房间之间差约 4 dB，T20 × D/R 的决策表在实录上判不准
struct RoomDecision
{
    RoomAdvice advice = RoomAdvice::Unknown;
    bool priority = false; // 进“最该改”的候选：偏混与很混
};
RoomDecision decideRoom(const std::optional<ReverbResult>& reverb);

// ---- 距离标尺：不显示厘米，也不给绝对判断 ----
// 一项读数在一次位置测试里的值。available 为假：这一次没有可比的值（没测、要再测、没有事件）
struct DistanceReading
{
    bool available = false;
    double value = 0.0; // dB，越大越近：D/R 升高、喷麦比值升高、齿音电平升高
    bool lowerBound = false; // 只知道下限（D/R 尾部测不到）：真值只会更高
};
struct DistanceEvidence
{
    // 重测位置之后的“上次 → 现在”；第一次测试时 before 都不可用
    DistanceReading drBefore, drNow;
    DistanceReading plosiveBefore, plosiveNow;
    DistanceReading sibilanceBefore, sibilanceNow;
    bool gainChanged = false; // 两次之间调过增益或声道布局变了：齿音的绝对电平不能比
    bool roomChanged = false; // 两次之间换了房间：三项都不比
    // 第一次测试的弱提示：当前、可信的 SNR 差（A 加权，按噪声档位）
    bool snrBad = false;
    double tMidSec = 0.0; // 当前、可信的 T20 主指标；没有时为 0
};
// 前后对比说出了方向就用它；一项都比不了时，SNR 差且有 T20 才说“可能离远了”，否则 unknown。
// 置信度恒为可信：不确定的程度写在 kind 里（maybe-too-far、conflicting），不靠再测表达
DistanceResult decideDistance(const DistanceEvidence& evidence);
// 按 T20 估的经验距离上限（米）；tMidSec 不为正时为 0
double empiricalDistanceLimitM(double tMidSec);
// 距离的建议只由 kind 决定：可能离远了 → 靠近一点；其余不给
DistanceAdvice distanceAdviceOf(const DistanceResult& distance);

// ---- 方向 ----
// 可能放反，且两次测量里较小的优势也不低于 priority_db，才进“最该改”
bool directionIsPriority(const DirectionAnalysis& direction);

struct DirectionPromptInput
{
    MicType mic = MicType::Unknown;
    bool directionTested = false;
    bool directionStale = false; // 测过之后挪过位置
    RoomAdvice room = RoomAdvice::Unknown; // good 就是 T20 干或正常
    DistanceCase distance = DistanceCase::Unknown;
    bool gainLowOrNoiseBad = false;
};
// 没测过方向不算问题：这里只决定提示放在哪，不影响“可以开始录音了”
DirectionPrompt decideDirectionPrompt(const DirectionPromptInput& in);
} // namespace miciri::core
