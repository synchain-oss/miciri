// SPDX-License-Identifier: GPL-3.0-or-later
//
// 人声三项：喷麦、齿音（含有效带宽）、口水音。都分析一整段已经录完的测试（字表或自然朗读），
// 不是逐块流式：起音、LPC 残差与局部尺度都要看前后文。采样率高于各自的分析上限时先整数倍抽取。
// 阈值全部来自 thresholds.json 的 plosive、sibilance、click 三组。
//
// 喷麦：按宽带起音切出音节，量起音段低频峰值比同音节元音稳态的低频电平高多少（同音节瞬态比）。
//       一遍字表的严重度取所有音节里最大的那个，不取中位数：不喷的字（t、k 开头）会把中位数稀释掉。
//       低基频时灵敏度下降的标记（lowSensitivity）要等低基频的界有实录依据，现在不设。
// 齿音：按谱重心、平坦度与高频能量找擦音段，量 3-6 kHz 与 6-12 kHz 减去底噪后的绝对电平（dBFS）。
//       只供同一增益下两次测试之间的 A/B 比较，不分档：对元音的比值受近讲效应影响，挪远时反而变大。
// 有效带宽：从 bandwidth_search_from_hz 往上，说话的谱连续高出底噪 bandwidth_over_noise_db 的最高频率。
//       不按能量占比算：人声能量大多在低频，按占比会误关高频项。
// 口水音：LPC 残差里超过局部尺度很多倍的尖峰；之后高频持续的（擦音）、之后低频和元音一起上来的（爆破）
//       和高频背景本来就响的（齿音起头、送气）都不算。只作“可能有口水音”的提示，不作距离证据。
//
// 齿音和口水音不分档：它们的 severity 停在最低档只表示“未分档”，不表示“良好”，上层不要拿它直接着色。
#pragma once

#include <optional>
#include <vector>

#include "result/Results.h"

namespace miciri::core
{
// ---- 喷麦 ----
struct PlosiveEvent
{
    double startSec = 0.0;
    double endSec = 0.0;
    double ratioDb = 0.0; // 起音段低频峰值相对同音节元音稳态的低频电平
};
std::vector<PlosiveEvent> plosiveEvents(const std::vector<double>& samples, double sampleRate);
PlosiveResult analysePlosive(const std::vector<double>& samples, double sampleRate);

// ---- 齿音与有效带宽 ----
struct SibilantEvent
{
    double startSec = 0.0;
    double endSec = 0.0;
    double midHighDb = 0.0; // 3-6 kHz，减底噪后的均方电平，dBFS
    double highDb = 0.0; // 6-12 kHz
};
std::vector<SibilantEvent> sibilantEvents(const std::vector<double>& samples, double sampleRate);
// 从搜索起点就没有高出底噪时为 0（没有可报的带宽）
double effectiveBandwidthHz(const std::vector<double>& samples, double sampleRate);
SibilanceResult analyseSibilance(const std::vector<double>& samples, double sampleRate);

// ---- 口水音 ----
// 每个残差尖峰都留下，规则在 summariseClicks 里套：这样能对同一次扫描比较不同的残差倍数。
struct ClickCandidate
{
    double timeSec = 0.0;
    double residualMultiple = 0.0; // 尖峰处残差绝对值 ÷ 局部残差尺度
    double peakDbfs = 0.0; // 尖峰附近高通后的峰值
    double relSpeechDb = 0.0; // peakDbfs 相对本人语音
    double contextRelDb = 0.0; // 尖峰前后的高频背景，相对本人语音
    bool fricative = false; // 之后高频保持：擦音
    bool plosive = false; // 之后低频与元音同时上来：爆破音
};
struct ClickScan
{
    std::vector<ClickCandidate> candidates;
    double seconds = 0.0;
    // 本人语音（200-4000 Hz 活跃帧）的均方电平。只有底噪（响的帧比安静的帧高不出 speech_over_floor_db）
    // 或数字静音时为空：没有语音可作参照，结果要求再测
    std::optional<double> speechDb;
};
ClickScan scanClicks(const std::vector<double>& samples, double sampleRate);
ClickResult summariseClicks(const ClickScan& scan, double residualMultiple);
ClickResult analyseClicks(const std::vector<double>& samples, double sampleRate);
} // namespace miciri::core
