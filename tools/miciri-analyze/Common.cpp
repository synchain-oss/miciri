// SPDX-License-Identifier: GPL-3.0-or-later
#include "Common.h"

#include <cmath>
#include <cstdio>

#include "channels/Channels.h"

namespace miciri::tools
{
using namespace miciri::core;

std::string formatNumber(double v)
{
    if (!std::isfinite(v))
        return "null";
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.9g", v);
    return buf;
}

std::string numberArray(const double* values, std::size_t count)
{
    std::string list = "[";
    for (std::size_t i = 0; i < count; ++i)
        list += (i ? "," : "") + formatNumber(values[i]);
    return list + "]";
}

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

ChannelPick pickChannel(const WavData& wav)
{
    ChannelPick pick;
    const std::vector<double>& left = wav.channels.at(0);
    pick.samples = &left;
    pick.stereo = wav.channels.size() >= 2;
    if (!pick.stereo)
        return pick;
    const std::vector<double>& right = wav.channels[1];
    const ChannelsResult c = analyseChannels(left, right, wav.sampleRate);
    const ChannelsMeasures& m = c.measures;
    pick.channelCheck = envelope(c,
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
                                     .add("analysedChannel", toString(c.verdict.analysedChannel)))
                            .str();
    pick.channel = c.verdict.analysedChannel;
    if (pick.channel == AnalysedChannel::Right)
        pick.samples = &right;
    return pick;
}
} // namespace miciri::tools
