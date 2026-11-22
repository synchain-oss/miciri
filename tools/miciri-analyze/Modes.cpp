// SPDX-License-Identifier: GPL-3.0-or-later
#include "Modes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Common.h"
#include "config/Thresholds.h"
#include "direction/Direction.h"
#include "room/Room.h"
#include "voice/Voice.h"

namespace miciri::tools
{
namespace
{
using namespace miciri::core;

std::int64_t count(std::size_t n)
{
    return static_cast<std::int64_t>(n);
}

// 把每个元素用 one 写成 JSON，拼成数组
template<class T, class F>
std::string listOf(const std::vector<T>& items, F one)
{
    std::string list = "[";
    for (std::size_t i = 0; i < items.size(); ++i)
        list += (i ? "," : "") + one(items[i]);
    return list + "]";
}

// 房间：各频带 T20、Tmid，逐下逐频带的状态；validClaps 是每个频带留下来取中位数的拍数
Obj roomJson(const std::vector<double>& x, double sr)
{
    const ReverbAnalysis a = analyseReverb(x, sr);
    const ReverbMeasures& m = a.result.measures;
    std::array<double, 4> valid{};
    for (const ReverbClap& clap : a.claps)
        for (std::size_t b = 0; b < valid.size(); ++b)
            valid[b] += clap.bands[b].status == ClapBandStatus::Valid ? 1.0 : 0.0;
    Obj out = envelope(a.result,
                       Obj()
                           .addRaw("t20Sec", numberArray(m.t20Sec.data(), m.t20Sec.size()))
                           .add("tMidSec", m.tMidSec)
                           .addRaw("bandPnrDb", numberArray(m.bandPnrDb.data(), m.bandPnrDb.size()))
                           .addRaw("bandR2", numberArray(m.bandR2.data(), m.bandR2.size()))
                           .add("repeatMaxOverMin", m.repeatMaxOverMin),
                       Obj().add("band", toString(a.result.verdict.band)).add("gated", a.result.verdict.gated));
    out.addRaw("bandsHz", numberArray(thr::reverb::bands_hz.data(), thr::reverb::bands_hz.size()))
        .add("clapCount", count(a.claps.size()))
        .addRaw("validClaps", numberArray(valid.data(), valid.size()))
        .addRaw("claps", listOf(a.claps, [](const ReverbClap& c) {
                    std::string bands = "[";
                    for (std::size_t b = 0; b < c.bands.size(); ++b)
                        bands += (b ? "," : "") + Obj()
                                                      .add("t20Sec", c.bands[b].t20Sec)
                                                      .add("pnrDb", c.bands[b].pnrDb)
                                                      .add("r2", c.bands[b].r2)
                                                      .add("status", toString(c.bands[b].status))
                                                      .str();
                    return Obj()
                        .add("timeSec", c.timeSec)
                        .add("peakDbfs", c.peakDbfs)
                        .add("clipped", c.clipped)
                        .addRaw("bands", bands + "]")
                        .str();
                }));
    return out;
}

Obj directRatioJson(const std::vector<double>& x, double sr)
{
    const DirectRatioAnalysis a = analyseDirectRatio(x, sr);
    Obj out = envelope(
        a.result, Obj().add("drDb", a.result.measures.drDb).add("lowerBoundOnly", a.result.measures.lowerBoundOnly),
        Obj());
    out.add("clapCount", count(a.claps.size())).addRaw("claps", listOf(a.claps, [](const DirectRatioClap& c) {
                                                           return Obj()
                                                               .add("timeSec", c.timeSec)
                                                               .add("peakDbfs", c.peakDbfs)
                                                               .add("clipped", c.clipped)
                                                               .add("usable", c.usable)
                                                               .add("drDb", c.drDb)
                                                               .add("tailSnrDb", c.tailSnrDb)
                                                               .add("lowerBound", c.lowerBound)
                                                               .str();
                                                       }));
    return out;
}

// 喷麦：一段录音就是一遍字表，worstRatioDb 是这一遍的最大值；eventList 是每个音节的瞬态比
Obj plosiveJson(const std::vector<double>& x, double sr)
{
    const PlosiveResult r = analysePlosive(x, sr);
    Obj out = envelope(r, Obj().add("worstRatioDb", r.measures.worstRatioDb).add("events", r.measures.events),
                       Obj()
                           .add("severity", toString(r.verdict.severity))
                           .add("lowSensitivity", r.verdict.lowSensitivity)
                           .add("overReference", r.verdict.overReference));
    out.addRaw("eventList", listOf(plosiveEvents(x, sr), [](const PlosiveEvent& e) {
                   return Obj().add("startSec", e.startSec).add("endSec", e.endSec).add("ratioDb", e.ratioDb).str();
               }));
    return out;
}

Obj sibilanceJson(const std::vector<double>& x, double sr)
{
    const SibilanceResult r = analyseSibilance(x, sr);
    const SibilanceMeasures& m = r.measures;
    Obj out = envelope(r,
                       Obj()
                           .add("midHighDb", m.midHighDb)
                           .add("highDb", m.highDb)
                           .add("effectiveBandwidthHz", m.effectiveBandwidthHz)
                           .add("events", m.events)
                           .add("highOverVowelDb", m.highOverVowelDb)
                           .add("vowelEvents", m.vowelEvents),
                       Obj()
                           .add("severity", toString(r.verdict.severity))
                           .add("highBandDisabled", r.verdict.highBandDisabled)
                           .add("overReference", r.verdict.overReference));
    out.addRaw("eventList", listOf(sibilantEvents(x, sr), [](const SibilantEvent& e) {
                   return Obj()
                       .add("startSec", e.startSec)
                       .add("endSec", e.endSec)
                       .add("midHighDb", e.midHighDb)
                       .add("highDb", e.highDb)
                       .add("hasVowel", e.hasVowel)
                       .add("highOverVowelDb", e.highOverVowelDb)
                       .str();
               }));
    return out;
}

Obj clicksJson(const std::vector<double>& x, double sr)
{
    const ClickResult r = analyseClicks(x, sr);
    const ClickMeasures& m = r.measures;
    return envelope(r,
                    Obj()
                        .add("perMinute", m.perMinute)
                        .add("loudestRelDb", m.loudestRelDb)
                        .add("loudestDbfs", m.loudestDbfs)
                        .add("analysedSeconds", m.analysedSeconds)
                        .add("count", m.count),
                    Obj().add("severity", toString(r.verdict.severity)).add("overReference", r.verdict.overReference));
}

// 方向只给单段的读数：四个方向的比较与判定由会话做，这里不做
Obj directionJson(const std::vector<double>& x, double sr)
{
    const DirectionTake t = measureDirectionTake(x, sr);
    return Obj().add("active", t.active).add("levelDb", t.levelDb).add("brightnessDb", t.brightnessDb);
}
} // namespace

std::optional<Mode> parseMode(std::string_view name)
{
    for (const auto& [text, mode] : kModes)
        if (text == name)
            return mode;
    return std::nullopt;
}

std::string_view modeName(Mode mode)
{
    for (const auto& [text, m] : kModes)
        if (m == mode)
            return text;
    return {};
}

std::string analyseModeToJson(Mode mode, const WavData& wav)
{
    if (mode == Mode::Level)
        return {};
    const ChannelPick pick = pickChannel(wav);
    const std::vector<double>& x = *pick.samples;
    const double sr = wav.sampleRate;

    Obj root;
    root.add("mode", modeName(mode))
        .add("sampleRate", sr)
        .add("channels", count(wav.channels.size()))
        .add("seconds", static_cast<double>(x.size()) / sr);
    if (pick.stereo)
        root.addRaw("channelCheck", pick.channelCheck);
    root.add("analysedChannel", toString(pick.channel));

    switch (mode)
    {
    case Mode::Room:
        root.add("reverb", roomJson(x, sr));
        break;
    case Mode::Dr:
        root.add("directRatio", directRatioJson(x, sr));
        break;
    case Mode::Plosive:
        root.add("plosive", plosiveJson(x, sr));
        break;
    case Mode::Sibilance:
        root.add("sibilance", sibilanceJson(x, sr));
        break;
    case Mode::Clicks:
        root.add("click", clicksJson(x, sr));
        break;
    case Mode::Direction:
        root.add("direction", directionJson(x, sr));
        break;
    case Mode::Level:
        break;
    }
    return root.str() + "\n";
}
} // namespace miciri::tools
