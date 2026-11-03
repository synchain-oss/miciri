// SPDX-License-Identifier: GPL-3.0-or-later
//
// DSP golden：几段固定种子的合成信号，把全部电平类分析的连续量（measures）逐项记在
// tests/golden/dsp/measures.txt，之后每次与它比对。档位不进 golden（改阈值不该让它变红）。
// 信号由本文件生成，不依赖任何音频文件；输入都避开阈值边界，所以采样率和平台的舍入差异不会翻转判定。
// 重录：cmake -DMICIRI_UPDATE_GOLDEN=ON，运行 golden_dsp，再关掉，并在 PR 里写明新旧差异。
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>

#include "channels/Channels.h"
#include "core/level/Synth.h"
#include "hum/Hum.h"
#include "level/Level.h"
#include "noise/Noise.h"

using namespace miciri;
using namespace miciri::testing;

namespace
{
constexpr const char* kGoldenPath = "tests/golden/dsp/measures.txt";
// 绝对误差 1e-4（dB、Hz、秒）加相对误差 1e-6；计数类字段小于 1 的差意味着整数不同，所以仍是精确比较
constexpr double kAbsTol = 1e-4;
constexpr double kRelTol = 1e-6;

using Values = std::vector<std::pair<std::string, double>>;

class Recorder
{
public:
    explicit Recorder(std::string fixture) : fixture_(std::move(fixture)) {}
    void add(const char* group, const char* key, double v)
    {
        values_.emplace_back(fixture_ + "." + group + "." + key, v);
    }
    void add(const char* group, const char* key, std::int64_t v) { add(group, key, static_cast<double>(v)); }
    void add(const char* group, const char* key, bool v) { add(group, key, v ? 1.0 : 0.0); }
    Values take() { return std::move(values_); }

private:
    std::string fixture_;
    Values values_;
};

void appendAll(Values& out, Values&& more)
{
    out.insert(out.end(), more.begin(), more.end());
}

// 与命令行工具相同的一组分析；立体声时先做声道检查，其余取较响的一路
Values analyseFixture(const char* name, const std::vector<double>& left, const std::vector<double>* right, double fs)
{
    Recorder r(name);
    const std::vector<double>* analysed = &left;
    if (right != nullptr)
    {
        const core::ChannelsResult c = core::analyseChannels(left, *right, fs);
        const core::ChannelsMeasures& m = c.measures;
        r.add("channels", "floorDbfsL", m.floorDbfsL);
        r.add("channels", "floorDbfsR", m.floorDbfsR);
        r.add("channels", "activeDbfsL", m.activeDbfsL);
        r.add("channels", "activeDbfsR", m.activeDbfsR);
        r.add("channels", "deltaDb", m.deltaDb);
        r.add("channels", "weakRiseDb", m.weakRiseDb);
        r.add("channels", "sumOverFloorDb", m.sumOverFloorDb);
        r.add("channels", "diffOverFloorDb", m.diffOverFloorDb);
        r.add("channels", "usedFallback", m.usedFallback);
        if (c.verdict.analysedChannel == core::AnalysedChannel::Right)
            analysed = right;
    }
    const core::NoSignalResult ns = core::analyseNoSignal(*analysed, fs);
    r.add("noSignal", "zeroSeconds", ns.measures.zeroSeconds);
    r.add("noSignal", "maxFrameDbfs", ns.measures.maxFrameDbfs);
    r.add("noSignal", "p10FrameDbfs", ns.measures.p10FrameDbfs);
    r.add("noSignal", "riseDb", ns.measures.riseDb);

    const core::GainResult g = core::analyseGain(left, right, fs);
    const core::GainMeasures& gm = g.measures;
    r.add("gain", "samplePeakDbfsL", gm.samplePeakDbfsL);
    r.add("gain", "samplePeakDbfsR", gm.samplePeakDbfsR);
    r.add("gain", "truePeakDbfsL", gm.truePeakDbfsL);
    r.add("gain", "truePeakDbfsR", gm.truePeakDbfsR);
    r.add("gain", "loudnessLkfs", gm.loudnessLkfs);
    r.add("gain", "digitalClipRunsL", gm.digitalClipRunsL);
    r.add("gain", "digitalClipRunsR", gm.digitalClipRunsR);
    r.add("gain", "flatPlateauPeaksL", gm.flatPlateauPeaksL);
    r.add("gain", "flatPlateauPeaksR", gm.flatPlateauPeaksR);

    core::NoiseContext context;
    context.speechLevelDbfsA = core::speechLevelDbfsA(*analysed, fs);
    const core::NoiseResult nz = core::analyseNoise(*analysed, fs, context);
    r.add("noise", "noiseDbfs", nz.measures.noiseDbfs);
    r.add("noise", "noiseDbfsA", nz.measures.noiseDbfsA);
    r.add("noise", "snrDb", nz.measures.snrDb);
    r.add("noise", "noisePeakDbfs", nz.measures.noisePeakDbfs);

    const core::HumResult h = core::analyseHum(*analysed, fs);
    r.add("hum", "fundamentalHz", h.measures.fundamentalHz);
    r.add("hum", "overFloorDb", h.measures.overFloorDb);
    r.add("hum", "energySharePct", h.measures.energySharePct);
    r.add("hum", "highestHarmonicHz", h.measures.highestHarmonicHz);
    r.add("hum", "levelDbfs", h.measures.levelDbfs);
    r.add("hum", "peakDbfs", h.measures.peakDbfs);

    core::DeviceProcessingContext device;
    device.noiseFloorDbfs = nz.measures.noiseDbfs;
    const core::DeviceProcessingResult dp = core::analyseDeviceProcessing(*analysed, fs, device);
    r.add("device", "longestZeroMs", dp.measures.longestZeroMs);
    r.add("device", "zeroRuns", dp.measures.zeroRuns);
    return r.take();
}

std::vector<double> noisy(std::vector<double> x, double floorDbfs, double cutoffHz, std::uint64_t seed)
{
    mix(x, lowpassNoise(48000.0, x.size(), fromDb(floorDbfs), cutoffHz, seed));
    return x;
}

// 同一组信号在三个采样率上各出一份，名字带采样率
Values buildAll()
{
    Values out;

    // 说话加房间底噪：停顿里的底噪给 SNR，说话给响度
    for (double fs : {44100.0, 48000.0, 96000.0})
    {
        std::vector<double> x = voice(fs, 8.0, fromDb(-20.0), 11, 1.0);
        mix(x, lowpassNoise(fs, x.size(), fromDb(-62.0), 2000.0, 12));
        appendAll(out, analyseFixture(("voice_" + std::to_string(static_cast<int>(fs))).c_str(), x, nullptr, fs));
    }

    // 安静步里的电流声（用户这时不说话）：50 与 60 Hz，其中一份在 96 kHz 上走抽取
    for (const auto& [name, fs, f0] :
         {std::tuple{"hum50_48000", 48000.0, 50.0}, std::tuple{"hum60_44100", 44100.0, 60.0},
          std::tuple{"hum50_96000", 96000.0, 50.0}})
    {
        std::vector<double> x = humSignal(fs, 10.0, f0, kHumProfileDb, fromDb(-48.0) * fromDb(-48.0));
        mix(x, lowpassNoise(fs, x.size(), fromDb(-60.0), 2000.0, 14));
        appendAll(out, analyseFixture(name, x, nullptr, fs));
    }

    // 模拟平顶：说话硬削在 -3 dBFS（没有碰到满刻度，所以不算数字削波）
    {
        const double fs = 48000.0;
        std::vector<double> x = voice(fs, 8.0, fromDb(-8.0), 17, 1.0);
        for (double& v : x)
            v = std::clamp(v, -fromDb(-3.0), fromDb(-3.0));
        mix(x, lowpassNoise(fs, x.size(), fromDb(-70.0), 2000.0, 18));
        appendAll(out, analyseFixture("flattop_48000", x, nullptr, fs));
    }

    // 数字削波：说话推过满刻度再硬削
    for (double fs : {44100.0, 96000.0})
    {
        std::vector<double> x = voice(fs, 8.0, fromDb(-4.0), 27, 1.0);
        mix(x, lowpassNoise(fs, x.size(), fromDb(-70.0), 2000.0, 28));
        for (double& v : x)
            v = std::clamp(v, -1.0, 1.0);
        appendAll(out, analyseFixture(("digitalclip_" + std::to_string(static_cast<int>(fs))).c_str(), x, nullptr, fs));
    }

    // 说话里夹一段严格数字零（降噪或门限的痕迹）
    {
        const double fs = 48000.0;
        std::vector<double> x = noisy(voice(fs, 8.0, fromDb(-24.0), 19, 0.5), -66.0, 3000.0, 20);
        const auto from = static_cast<std::size_t>(3.0 * fs);
        for (std::size_t i = from; i < from + static_cast<std::size_t>(0.2 * fs); ++i)
            x[i] = 0.0;
        appendAll(out, analyseFixture("gated_48000", x, nullptr, fs));
    }

    // 立体声：真立体声、相同的两路、相位相反
    {
        const double fs = 48000.0;
        const std::vector<double> a = noisy(voice(fs, 8.0, fromDb(-24.0), 21, 1.5), -78.0, 4000.0, 22);
        const std::vector<double> b = noisy(voice(fs, 8.0, fromDb(-26.0), 23, 1.5), -78.0, 4000.0, 24);
        appendAll(out, analyseFixture("stereo_true_48000", a, &b, fs));
        std::vector<double> copy = voice(fs, 8.0, fromDb(-24.0), 21, 1.5);
        mix(copy, lowpassNoise(fs, copy.size(), fromDb(-78.0), 4000.0, 25));
        appendAll(out, analyseFixture("stereo_identical_48000", a, &copy, fs));
        std::vector<double> inverted = voice(fs, 8.0, fromDb(-24.0), 21, 1.5);
        for (double& v : inverted)
            v = -v;
        mix(inverted, lowpassNoise(fs, inverted.size(), fromDb(-78.0), 4000.0, 26));
        appendAll(out, analyseFixture("stereo_antiphase_48000", a, &inverted, fs));
    }
    return out;
}

const Values& allValues()
{
    static const Values values = buildAll();
    return values;
}

[[maybe_unused]] std::string formatAll(const Values& values)
{
    std::ostringstream text;
    text << "# DSP golden: continuous measures of fixed synthetic signals; see golden_test.cpp.\n";
    text << "# One line per value: <fixture>.<group>.<key> <value>\n";
    for (const auto& [key, value] : values)
    {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.9g", value);
        text << key << ' ' << buf << '\n';
    }
    return text.str();
}

std::map<std::string, double> readGolden(const std::string& path)
{
    std::map<std::string, double> golden;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream fields(line);
        std::string key;
        double value = 0.0;
        if (fields >> key >> value)
            golden[key] = value;
    }
    return golden;
}
} // namespace

TEST_CASE("DSP golden: the continuous measures of the synthetic fixtures have not moved")
{
    const Values& current = allValues();
    REQUIRE(current.size() > 150);
#ifdef MICIRI_UPDATE_GOLDEN
    std::ofstream out(kGoldenPath, std::ios::binary | std::ios::trunc);
    REQUIRE(out.good());
    out << formatAll(current);
    WARN("golden rewritten: " << kGoldenPath << " (turn MICIRI_UPDATE_GOLDEN off again)");
#else
    const std::map<std::string, double> golden = readGolden(kGoldenPath);
    REQUIRE_FALSE(golden.empty());
    CHECK(golden.size() == current.size());
    for (const auto& [key, value] : current)
    {
        INFO(key);
        const auto it = golden.find(key);
        REQUIRE(it != golden.end());
        const double expected = it->second;
        CHECK(std::fabs(value - expected) <= kAbsTol + kRelTol * std::fabs(expected));
    }
#endif
}

TEST_CASE("DSP golden: the measures carry only finite numbers")
{
    for (const auto& [key, value] : allValues())
    {
        INFO(key);
        CHECK(std::isfinite(value));
    }
}
