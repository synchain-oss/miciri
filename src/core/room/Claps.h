// SPDX-License-Identifier: GPL-3.0-or-later
//
// 拍手录音的公共部分：逐块收样本，抽取到分析采样率后留在内存里；数字削波段和严格数字零段在原采样率上
// 边收边记（只记起止时间）。找拍手：每次取剩下的最响处，抹掉它前后各半个最小间隔，直到低于
// “最响一下减 rel_db”或绝对下限。结果只取决于样本序列，与分块方式无关。
#pragma once

#include <cstddef>
#include <vector>

#include "level/Dsp.h"

namespace miciri::core
{
struct ClapEvent
{
    std::size_t index = 0; // 分析采样率下的样本序号
    double peakDbfs = 0.0;
    // 峰前后 clip_window_ms 内碰到数字削波电平（clip_min_run_samples 个样本就算，比增益那一步的判据严：
    // 拍手是冲击声，碰到满幅时直达声已经被削掉了）
    bool clipped = false;
};

class ClapCapture
{
public:
    explicit ClapCapture(double sampleRate);
    void push(const double* samples, std::size_t count);

    double rate() const { return input_.rate(); }
    const std::vector<double>& samples() const { return analysed_; }
    std::vector<ClapEvent> findClaps() const;
    // [startSec, endSec) 与某个削波段或严格数字零段有重叠（时间从第一个样本算起）
    bool clipWithin(double startSec, double endSec) const
    {
        return overlaps(clips_, clipOpen_, minClipRun_, startSec, endSec);
    }
    bool zeroRunWithin(double startSec, double endSec) const
    {
        return overlaps(zeros_, zeroOpen_, minZeroRun_, startSec, endSec);
    }

private:
    struct Span
    {
        std::size_t start = 0;
        std::size_t length = 0;
    };
    bool overlaps(const std::vector<Span>& closed, const Span& open, std::size_t minLength, double startSec,
                  double endSec) const;
    static void track(bool inRun, std::size_t index, std::size_t minLength, Span& open, std::vector<Span>& closed);

    double sampleRate_;
    dsp::AnalysisInput input_;
    std::vector<double> analysed_;
    std::size_t minClipRun_;
    std::size_t minZeroRun_;
    std::size_t received_ = 0;
    Span clipOpen_, zeroOpen_;
    std::vector<Span> clips_, zeros_;
};
} // namespace miciri::core
