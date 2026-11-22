// SPDX-License-Identifier: GPL-3.0-or-later
#include "Report.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#include "Common.h"
#include "hum/Hum.h"
#include "level/Level.h"
#include "noise/Noise.h"
#include "result/Results.h"

namespace miciri::tools
{
using namespace miciri::core;

std::string analyseToJson(const WavData& wav, std::optional<double> floorDbfs)
{
    const ChannelPick pick = pickChannel(wav);
    const bool stereo = pick.stereo;
    const std::vector<double>& left = wav.channels.at(0);
    const std::vector<double>& right = stereo ? wav.channels[1] : left;
    const std::vector<double>* analysed = pick.samples;
    const double sr = wav.sampleRate;

    Obj root;
    root.add("sampleRate", sr)
        .add("channels", static_cast<std::int64_t>(wav.channels.size()))
        .add("seconds", static_cast<double>(left.size()) / sr);
    if (stereo)
        root.addRaw("channelCheck", pick.channelCheck);

    const NoSignalResult ns = analyseNoSignal(*analysed, sr);
    root.add("noSignal", envelope(ns,
                                  Obj()
                                      .add("zeroSeconds", ns.measures.zeroSeconds)
                                      .add("maxFrameDbfs", ns.measures.maxFrameDbfs)
                                      .add("p10FrameDbfs", ns.measures.p10FrameDbfs)
                                      .add("riseDb", ns.measures.riseDb),
                                  Obj().add("state", toString(ns.verdict.state))));

    GainContext gainContext;
    if (floorDbfs)
        gainContext.noiseFloorRms = std::sqrt(dsp::dbToPower(*floorDbfs));
    const GainResult g = analyseGain(left, stereo ? &right : nullptr, sr, gainContext);
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
