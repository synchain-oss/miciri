// SPDX-License-Identifier: GPL-3.0-or-later
#include "Report.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#include "channels/Channels.h"
#include "hum/Hum.h"
#include "level/Level.h"
#include "noise/Noise.h"
#include "result/Results.h"

namespace miciri::tools
{
namespace
{
using namespace miciri::core;

// 手写的最小 JSON 对象：键按添加顺序输出
class Obj
{
public:
    Obj& add(const char* key, double v)
    {
        char buf[48];
        if (std::isfinite(v))
            std::snprintf(buf, sizeof buf, "%.9g", v);
        else
            std::snprintf(buf, sizeof buf, "null");
        return raw(key, buf);
    }
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

Obj confidenceJson(const Confidence& c)
{
    return Obj().add("level", toString(c.level)).add("reason", toString(c.reason));
}

Obj evidenceJson(const Evidence& e)
{
    std::string list = "[";
    for (std::size_t i = 0; i < e.marks.size(); ++i)
    {
        const EvidenceMark& m = e.marks[i];
        const char* kind = "digitalClip";
        switch (m.kind)
        {
        case EvidenceKind::DigitalClip:
            kind = "digitalClip";
            break;
        case EvidenceKind::FlatTop:
            kind = "flatTop";
            break;
        case EvidenceKind::HumPeak:
            kind = "humPeak";
            break;
        case EvidenceKind::ZeroRun:
            kind = "zeroRun";
            break;
        case EvidenceKind::Click:
            kind = "click";
            break;
        case EvidenceKind::Plosive:
            kind = "plosive";
            break;
        case EvidenceKind::Sibilant:
            kind = "sibilant";
            break;
        }
        list += (i ? "," : "") + Obj()
                                     .add("kind", std::string_view(kind))
                                     .add("startSec", m.startSec)
                                     .add("endSec", m.endSec)
                                     .add("value", m.value)
                                     .add("channel", static_cast<std::int64_t>(m.channel))
                                     .str();
    }
    list += "]";
    return Obj().addRaw("marks", list).add("count", static_cast<std::int64_t>(e.marks.size()));
}

template<class R>
Obj envelope(const R& r, const Obj& measures, const Obj& verdict)
{
    return Obj()
        .add("measures", measures)
        .add("verdict", verdict)
        .add("confidence", confidenceJson(r.confidence))
        .add("evidence", evidenceJson(r.evidence));
}
} // namespace

std::string analyseToJson(const WavData& wav)
{
    const bool stereo = wav.channels.size() >= 2;
    const std::vector<double>& left = wav.channels.at(0);
    const std::vector<double>& right = stereo ? wav.channels[1] : left;
    const double sr = wav.sampleRate;

    Obj root;
    root.add("sampleRate", sr)
        .add("channels", static_cast<std::int64_t>(wav.channels.size()))
        .add("seconds", static_cast<double>(left.size()) / sr);

    const std::vector<double>* analysed = &left;
    if (stereo)
    {
        const ChannelsResult c = analyseChannels(left, right, sr);
        const ChannelsMeasures& m = c.measures;
        root.add("channelCheck", envelope(c,
                                          Obj()
                                              .add("floorDbfsL", m.floorDbfsL)
                                              .add("floorDbfsR", m.floorDbfsR)
                                              .add("activeDbfsL", m.activeDbfsL)
                                              .add("activeDbfsR", m.activeDbfsR)
                                              .add("deltaDb", m.deltaDb)
                                              .add("weakRiseDb", m.weakRiseDb)
                                              .add("sumOverFloorDb", m.sumOverFloorDb)
                                              .add("diffOverFloorDb", m.diffOverFloorDb)
                                              .add("usedFallback", m.usedFallback),
                                          Obj()
                                              .add("kind", toString(c.verdict.kind))
                                              .add("statusL", toString(c.verdict.statusL))
                                              .add("statusR", toString(c.verdict.statusR))
                                              .add("analysedChannel", toString(c.verdict.analysedChannel))));
        if (c.verdict.analysedChannel == AnalysedChannel::Right)
            analysed = &right;
    }

    const NoSignalResult ns = analyseNoSignal(*analysed, sr);
    root.add("noSignal", envelope(ns,
                                  Obj()
                                      .add("zeroSeconds", ns.measures.zeroSeconds)
                                      .add("maxFrameDbfs", ns.measures.maxFrameDbfs)
                                      .add("p10FrameDbfs", ns.measures.p10FrameDbfs)
                                      .add("riseDb", ns.measures.riseDb),
                                  Obj().add("state", toString(ns.verdict.state))));

    const GainResult g = analyseGain(left, stereo ? &right : nullptr, sr);
    const GainMeasures& gm = g.measures;
    root.add("gain", envelope(g,
                              Obj()
                                  .add("samplePeakDbfsL", gm.samplePeakDbfsL)
                                  .add("samplePeakDbfsR", gm.samplePeakDbfsR)
                                  .add("truePeakDbfsL", gm.truePeakDbfsL)
                                  .add("truePeakDbfsR", gm.truePeakDbfsR)
                                  .add("loudnessLkfs", gm.loudnessLkfs)
                                  .add("digitalClipRunsL", gm.digitalClipRunsL)
                                  .add("digitalClipRunsR", gm.digitalClipRunsR)
                                  .add("flatPlateauPeaksL", gm.flatPlateauPeaksL)
                                  .add("flatPlateauPeaksR", gm.flatPlateauPeaksR),
                              Obj()
                                  .add("band", toString(g.verdict.band))
                                  .add("digitalClip", g.verdict.digitalClip)
                                  .add("maybeFlatTop", g.verdict.maybeFlatTop)));

    NoiseContext noiseContext;
    noiseContext.speechLevelDbfsA = speechLevelDbfsA(*analysed, sr);
    const NoiseResult nz = analyseNoise(*analysed, sr, noiseContext);
    root.add("noise", envelope(nz,
                               Obj()
                                   .add("noiseDbfs", nz.measures.noiseDbfs)
                                   .add("noiseDbfsA", nz.measures.noiseDbfsA)
                                   .add("snrDb", noiseContext.speechLevelDbfsA ? nz.measures.snrDb : std::nan(""))
                                   .add("noisePeakDbfs", nz.measures.noisePeakDbfs),
                               Obj().add("band", toString(nz.verdict.band))));

    const HumResult h = analyseHum(*analysed, sr);
    const HumMeasures& hm = h.measures;
    root.add("hum", envelope(h,
                             Obj()
                                 .add("fundamentalHz", hm.fundamentalHz)
                                 .add("overFloorDb", hm.overFloorDb)
                                 .add("energySharePct", hm.energySharePct)
                                 .add("highestHarmonicHz", hm.highestHarmonicHz)
                                 .add("levelDbfs", hm.levelDbfs)
                                 .add("peakDbfs", hm.peakDbfs),
                             Obj().add("kind", toString(h.verdict.kind)).add("slight", h.verdict.slight)));

    DeviceProcessingContext deviceContext;
    deviceContext.noiseFloorDbfs = nz.measures.noiseDbfs;
    const DeviceProcessingResult dp = analyseDeviceProcessing(*analysed, sr, deviceContext);
    root.add("deviceProcessing",
             envelope(dp, Obj().add("longestZeroMs", dp.measures.longestZeroMs).add("zeroRuns", dp.measures.zeroRuns),
                      Obj().add("detected", dp.verdict.detected)));
    return root.str() + "\n";
}
} // namespace miciri::tools
