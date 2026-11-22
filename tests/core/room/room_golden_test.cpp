// SPDX-License-Identifier: GPL-3.0-or-later
//
// 房间、位置与方向的 golden：几段固定种子的合成信号，把 measures 逐项锁在下面的表里。
// 档位不进 golden（改阈值不该让它变红）。输入避开阈值边界：T60 约 0.35 s，落在“正常”一档的中间，
// 离两侧档界各约 0.1 s；拍手的尾巴远高于底噪，各频带的门限都不贴边，平台间的舍入差异不会翻转作废的拍手数。
// DSP 数值有意改变时，按失败信息里的新值改表，并在 PR 里写明新旧差异。
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "core/room/ClapSynth.h"
#include "core/voice/VoiceSynth.h"
#include "direction/Direction.h"
#include "room/Room.h"

using namespace miciri;
using namespace miciri::testing;

namespace
{
// 绝对误差 1e-4（dB、秒）加相对误差 1e-6；计数类字段因此仍是精确比较
constexpr double kAbsTol = 1e-4;
constexpr double kRelTol = 1e-6;

using Values = std::vector<std::pair<std::string, double>>;

Values measure()
{
    const double fs = 48000.0;
    Values v;
    ClapTrain c;
    c.fs = fs;
    c.t60 = [](int i) { return 0.35 + 0.004 * i; };
    const auto claps = makeClaps(c);
    const auto reverb = core::analyseReverb(claps, fs).result;
    v.emplace_back("reverb.tMidSec", reverb.measures.tMidSec);
    for (std::size_t b = 0; b < reverb.measures.t20Sec.size(); ++b)
        v.emplace_back("reverb.t20Sec." + std::to_string(b), reverb.measures.t20Sec[b]);
    const auto dr = core::analyseDirectRatio(claps, fs).result;
    v.emplace_back("directRatio.drDb", dr.measures.drDb);
    v.emplace_back("directRatio.lowerBoundOnly", dr.measures.lowerBoundOnly ? 1.0 : 0.0);

    // 方向：同一段元音按四个增益各唱一次，第 2 个方向另加一点高频
    std::array<core::DirectionTake, 4> takes{};
    for (int d = 0; d < 4; ++d)
    {
        std::vector<double> x = whiteNoise(static_cast<std::size_t>(4.0 * fs), 1e-4, 300);
        for (int i = 0; i < 3; ++i)
            addVowel(x, fs, 0.3 + 1.2 * i, 0.8, 150.0 + 20.0 * i, 0.1 * std::pow(10.0, -2.0 * d / 20.0));
        if (d == 2)
            addSineBand(x, fs, 5000.0, 9000.0, 250.0, 0.02, 0.3, 3.7, 301);
        takes[static_cast<std::size_t>(d)] = core::measureDirectionTake(x, fs);
        v.emplace_back("direction.levelDb." + std::to_string(d), takes[static_cast<std::size_t>(d)].levelDb);
        v.emplace_back("direction.brightnessDb." + std::to_string(d), takes[static_cast<std::size_t>(d)].brightnessDb);
    }
    const auto dir = core::decideDirection(takes, 3).result;
    v.emplace_back("direction.bestOtherAdvantageDb", dir.measures.bestOtherAdvantageDb);
    return v;
}

// 期望值：由本文件在 48 kHz 上生成
const Values kGolden{
    {"reverb.tMidSec", 0.3674613326},           {"reverb.t20Sec.0", 0.3626626637},
    {"reverb.t20Sec.1", 0.3722600014},          {"reverb.t20Sec.2", 0.371594873},
    {"reverb.t20Sec.3", 0.3646005096},          {"directRatio.drDb", 0.9220717309},
    {"directRatio.lowerBoundOnly", 0},          {"direction.levelDb.0", -26.98221563},
    {"direction.brightnessDb.0", -14.78570291}, {"direction.levelDb.1", -28.98220493},
    {"direction.brightnessDb.1", -14.78526498}, {"direction.levelDb.2", -30.97202239},
    {"direction.brightnessDb.2", -14.34576692}, {"direction.levelDb.3", -32.98213643},
    {"direction.brightnessDb.3", -14.78348062}, {"direction.bestOtherAdvantageDb", 5.9999208},
};
} // namespace

TEST_CASE("房间与方向 golden：measures 与锁定的值一致", "[room][direction][golden]")
{
    const Values actual = measure();
    REQUIRE(actual.size() == kGolden.size());
    for (std::size_t i = 0; i < actual.size(); ++i)
    {
        std::ostringstream shown;
        shown.precision(12);
        shown << actual[i].first << " = " << actual[i].second;
        INFO(shown.str());
        CHECK(actual[i].first == kGolden[i].first);
        CHECK(std::fabs(actual[i].second - kGolden[i].second) <= kAbsTol + kRelTol * std::fabs(kGolden[i].second));
    }
}
