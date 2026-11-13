// SPDX-License-Identifier: GPL-3.0-or-later
//
// 最小的 wav 读取：RIFF/WAVE，整数 PCM（8、16、24、32 bit）与浮点（32、64 bit），含 WAVE_FORMAT_EXTENSIBLE。
// 样本换算成 -1 到 1 的 double，按声道拆开。只读内存里的字节，不碰文件系统。
#pragma once

#include <string>
#include <vector>

namespace miciri::tools
{
struct WavData
{
    double sampleRate = 0.0;
    std::vector<std::vector<double>> channels; // channels[c][frame]
};

// 成功返回 true；失败返回 false 并在 error 里写原因
bool parseWav(const std::vector<unsigned char>& bytes, WavData& out, std::string& error);

// 所有样本乘以 10^(db/20)，不限幅。用来补回导出时的衰减（例如单声道轨按 -3 dB 声像律导成双声道），
// 让分析看到的是录音时的电平。
void applyGainDb(WavData& wav, double db);
} // namespace miciri::tools
