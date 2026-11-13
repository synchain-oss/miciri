// SPDX-License-Identifier: GPL-3.0-or-later
//
// 把一个 wav 当成一次完整的录音，跑全部电平类分析，输出 JSON：每项有 measures（连续量）、verdict（档位，
// 用线上名字）、confidence、evidence。立体声文件先做声道检查，其余分析取较响的一路（相同或相反时取左路）。
// 整个文件同时充当无信号窗口、噪声步和增益步，所以对一段“停顿加说话”的录音最有意义。
#pragma once

#include <optional>
#include <string>

#include "Wav.h"

namespace miciri::tools
{
// floorDbfs：接通步会读到的底噪（RMS，dBFS），给增益的模拟平顶放宽容差；不给就只用相对容差。
std::string analyseToJson(const WavData& wav, std::optional<double> floorDbfs = std::nullopt);
} // namespace miciri::tools
