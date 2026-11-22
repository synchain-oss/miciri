// SPDX-License-Identifier: GPL-3.0-or-later
//
// 各模式共用的 JSON 拼接与声道选择。结果的外壳（measures、verdict、confidence、evidence）和线上名字一致，
// 键按添加顺序输出，同一输入的输出逐字节稳定。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Wav.h"
#include "result/Results.h"

namespace miciri::tools
{
// 一个数的 JSON 写法：九位有效数字；非有限的写成 null，不会让输出变成非法 JSON
std::string formatNumber(double v);

// 手写的最小 JSON 对象：键按添加顺序输出
class Obj
{
public:
    Obj& add(const char* key, double v) { return raw(key, formatNumber(v)); }
    Obj& add(const char* key, std::int64_t v) { return raw(key, std::to_string(v)); }
    Obj& add(const char* key, bool v) { return raw(key, v ? "true" : "false"); }
    Obj& add(const char* key, std::string_view v) { return raw(key, "\"" + std::string(v) + "\""); }
    Obj& add(const char* key, const Obj& v) { return raw(key, v.str()); }
    Obj& addRaw(const char* key, const std::string& value) { return raw(key, value); }
    std::string str() const { return "{" + body_ + "}"; }

private:
    Obj& raw(const char* key, const std::string& value)
    {
        if (!body_.empty())
            body_ += ",";
        body_ += "\"" + std::string(key) + "\":" + value;
        return *this;
    }
    std::string body_;
};

// 一串数的 JSON 数组
std::string numberArray(const double* values, std::size_t count);

Obj confidenceJson(const core::Confidence& c);
Obj evidenceJson(const core::Evidence& e);

template<class R>
Obj envelope(const R& r, const Obj& measures, const Obj& verdict)
{
    return Obj()
        .add("measures", measures)
        .add("verdict", verdict)
        .add("confidence", confidenceJson(r.confidence))
        .add("evidence", evidenceJson(r.evidence));
}

// 要分析哪一路：单声道取它；立体声先做声道检查，取检查选出的一路（相同或相反时取左路）。
// 多于两路的文件只看前两路。
struct ChannelPick
{
    const std::vector<double>* samples = nullptr;
    bool stereo = false;
    core::AnalysedChannel channel = core::AnalysedChannel::Left;
    std::string channelCheck; // 立体声时是声道检查的 JSON 对象；单声道为空
};
ChannelPick pickChannel(const WavData& wav);
} // namespace miciri::tools
