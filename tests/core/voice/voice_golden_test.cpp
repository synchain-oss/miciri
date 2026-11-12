// SPDX-License-Identifier: GPL-3.0-or-later
//
// 人声三项的 golden：几段固定种子的合成信号，把 measures 逐项锁在下面的表里。档位不进 golden（改阈值不该让它变红）。
// 输入避开阈值边界，所以平台间的舍入差异不会翻转计数。DSP 数值有意改变时，按失败信息里的新值改表，
// 并在 PR 里写明新旧差异。
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "core/voice/VoiceSynth.h"
#include "voice/Voice.h"

using namespace miciri;
using namespace miciri::testing;

namespace
{
// 绝对误差 1e-4（dB、Hz、秒）加相对误差 1e-6；计数类字段因此仍是精确比较
constexpr double kAbsTol = 1e-4;
constexpr double kRelTol = 1e-6;

using Values = std::vector<std::pair<std::string, double>>;

Values measure()
{
    const double fs = 48000.0;
    Values v;
    const auto p = core::analysePlosive(wordList(fs, {-200.0, 8.0, -200.0, 2.0, -200.0, 12.0}), fs);
    v.emplace_back("plosive.worstRatioDb", p.measures.worstRatioDb);
    v.emplace_back("plosive.events", static_cast<double>(p.measures.events));

    std::vector<double> s = whiteNoise(static_cast<std::size_t>(5.0 * fs), 3e-5, 81);
    addSineBand(s, fs, 100.0, 9000.0, 50.0, 0.05, 0.3, 0.9, 82);
    for (int i = 0; i < 4; ++i)
        addNoiseBurst(s, fs, 1.4 + 0.9 * i, 0.12 + 0.03 * i, 0.02 * (i + 1), 90 + static_cast<std::uint64_t>(i));
    const auto sib = core::analyseSibilance(s, fs);
    v.emplace_back("sibilance.midHighDb", sib.measures.midHighDb);
    v.emplace_back("sibilance.highDb", sib.measures.highDb);
    v.emplace_back("sibilance.effectiveBandwidthHz", sib.measures.effectiveBandwidthHz);
    v.emplace_back("sibilance.events", static_cast<double>(sib.measures.events));

    std::vector<double> c = whiteNoise(static_cast<std::size_t>(6.0 * fs), 1e-4, 91);
    for (int i = 0; i < 5; ++i)
        addVowel(c, fs, 0.4 + 1.1 * i, 0.35, 125.0 + 15.0 * i, 0.12, 0.03);
    for (int i = 0; i < 4; ++i)
        addTick(c, fs, 0.95 + 1.1 * i, 0.03 * (i + 1));
    const auto cl = core::analyseClicks(c, fs);
    v.emplace_back("click.count", static_cast<double>(cl.measures.count));
    v.emplace_back("click.perMinute", cl.measures.perMinute);
    v.emplace_back("click.loudestDbfs", cl.measures.loudestDbfs);
    v.emplace_back("click.loudestRelDb", cl.measures.loudestRelDb);
    v.emplace_back("click.analysedSeconds", cl.measures.analysedSeconds);
    return v;
}

// 期望值：由本文件在 48 kHz 上生成
const Values kGolden{
    {"plosive.worstRatioDb", 17.9298235631},
    {"plosive.events", 6.0},
    {"sibilance.midHighDb", -35.8026582897},
    {"sibilance.highDb", -32.5900046635},
    {"sibilance.effectiveBandwidthHz", 24000.0},
    {"sibilance.events", 4.0},
    {"click.count", 4.0},
    {"click.perMinute", 40.0},
    {"click.loudestDbfs", -21.4053231159},
    {"click.loudestRelDb", 7.8875158418},
    {"click.analysedSeconds", 6.0},
};
} // namespace

TEST_CASE("人声 golden：measures 与锁定的值一致", "[voice][golden]")
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
