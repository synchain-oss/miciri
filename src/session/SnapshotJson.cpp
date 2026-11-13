// SPDX-License-Identifier: GPL-3.0-or-later
//
// Session 的快照写成 JSON。字段名与顺序按桥契约（src/plugin/BridgeApi.h 的 SNAPSHOT_*、RESULT_* 表），
// 会话测试逐表对拍，任何一边多一项、少一项都判红。数字按 C 区域格式化；非有限数写 null。
// getEvidence 的回执载荷（证据波形）也在这里写，字段按 EVIDENCE_* 表。
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string_view>

#include "config/Thresholds.h"
#include "plugin/BridgeApi.h"
#include "session/Advice.h"
#include "session/Grading.h"
#include "session/Session.h"

namespace miciri::session
{
namespace
{
using namespace core;

class Json
{
public:
    Json()
    {
        out_.imbue(std::locale::classic());
        out_ << std::setprecision(12);
    }
    std::string str() const { return out_.str(); }

    Json& key(std::string_view k)
    {
        sep();
        out_ << '"' << k << "\":";
        return *this;
    }
    Json& open()
    {
        sep();
        out_ << '{';
        return *this;
    }
    Json& close()
    {
        out_ << '}';
        comma_ = true;
        return *this;
    }
    Json& openArray()
    {
        sep();
        out_ << '[';
        return *this;
    }
    Json& closeArray()
    {
        out_ << ']';
        comma_ = true;
        return *this;
    }
    Json& num(double v)
    {
        sep();
        if (std::isfinite(v))
            out_ << v;
        else
            out_ << "null";
        comma_ = true;
        return *this;
    }
    Json& integer(long long v)
    {
        sep();
        out_ << v;
        comma_ = true;
        return *this;
    }
    Json& boolean(bool v)
    {
        sep();
        out_ << (v ? "true" : "false");
        comma_ = true;
        return *this;
    }
    // 只用于枚举的线上名字与契约里的键，它们不含需要转义的字符
    Json& name(std::string_view v)
    {
        sep();
        out_ << '"' << v << '"';
        comma_ = true;
        return *this;
    }
    template<class E>
    Json& wire(E v)
    {
        return name(toString(v));
    }
    template<std::size_t N>
    Json& nums(const std::array<double, N>& v)
    {
        openArray();
        for (double x : v)
            num(x);
        return closeArray();
    }

private:
    void sep()
    {
        if (comma_)
            out_ << ',';
        comma_ = false;
    }
    std::ostringstream out_;
    bool comma_ = false;
};

Tone severityTone(Severity s, bool graded)
{
    return graded ? toneOf(s) : Tone::None;
}

// RESULT_COMMON。再测的结果没有结论，一律无色
template<class R>
void common(Json& j, const R& r, Tone tone, bool stale)
{
    const bool retest = r.confidence.level == Reliability::Retest;
    j.key("tone").wire(retest ? Tone::None : tone);
    j.key("reliability").wire(r.confidence.level);
    j.key("retestReason").wire(r.confidence.reason);
    j.key("stale").boolean(stale);
}

void put(Json& j, const NoSignalResult& r, bool stale)
{
    const auto& m = r.measures;
    j.open();
    j.key("zeroSeconds").num(m.zeroSeconds).key("maxFrameDbfs").num(m.maxFrameDbfs);
    j.key("p10FrameDbfs").num(m.p10FrameDbfs).key("riseDb").num(m.riseDb);
    j.key("state").wire(r.verdict.state);
    common(j, r, toneOf(r.verdict), stale);
    j.close();
}

void put(Json& j, const ChannelsResult& r, bool stale)
{
    const auto& m = r.measures;
    j.open();
    j.key("floorDbfsL").num(m.floorDbfsL).key("floorDbfsR").num(m.floorDbfsR);
    j.key("activeDbfsL").num(m.activeDbfsL).key("activeDbfsR").num(m.activeDbfsR);
    j.key("deltaDb").num(m.deltaDb).key("weakRiseDb").num(m.weakRiseDb);
    j.key("sumOverFloorDb").num(m.sumOverFloorDb).key("diffOverFloorDb").num(m.diffOverFloorDb);
    j.key("usedFallback").boolean(m.usedFallback);
    j.key("kind").wire(r.verdict.kind).key("statusL").wire(r.verdict.statusL).key("statusR").wire(r.verdict.statusR);
    j.key("analysedChannel").wire(r.verdict.analysedChannel);
    common(j, r, toneOf(r.verdict), stale);
    j.close();
}

void put(Json& j, const GainResult& r, bool stale)
{
    const auto& m = r.measures;
    j.open();
    j.key("samplePeakDbfsL").num(m.samplePeakDbfsL).key("samplePeakDbfsR").num(m.samplePeakDbfsR);
    j.key("truePeakDbfsL").num(m.truePeakDbfsL).key("truePeakDbfsR").num(m.truePeakDbfsR);
    j.key("loudnessLkfs").num(m.loudnessLkfs);
    j.key("digitalClipRunsL").integer(m.digitalClipRunsL).key("digitalClipRunsR").integer(m.digitalClipRunsR);
    j.key("flatPlateauPeaksL").integer(m.flatPlateauPeaksL).key("flatPlateauPeaksR").integer(m.flatPlateauPeaksR);
    j.key("band").wire(r.verdict.band).key("digitalClip").boolean(r.verdict.digitalClip);
    j.key("maybeFlatTop").boolean(r.verdict.maybeFlatTop);
    common(j, r, toneOf(r.verdict), stale);
    j.close();
}

void put(Json& j, const NoiseResult& r, bool stale)
{
    const auto& m = r.measures;
    j.open();
    j.key("noiseDbfs").num(m.noiseDbfs).key("noiseDbfsA").num(m.noiseDbfsA).key("snrDb").num(m.snrDb);
    j.key("noisePeakDbfs").num(m.noisePeakDbfs).key("band").wire(r.verdict.band);
    common(j, r, toneOf(r.verdict), stale);
    j.close();
}

void put(Json& j, const HumResult& r, bool stale)
{
    const auto& m = r.measures;
    j.open();
    j.key("fundamentalHz").num(m.fundamentalHz).key("overFloorDb").num(m.overFloorDb);
    j.key("energySharePct").num(m.energySharePct).key("highestHarmonicHz").num(m.highestHarmonicHz);
    j.key("levelDbfs").num(m.levelDbfs).key("peakDbfs").num(m.peakDbfs);
    j.key("kind").wire(r.verdict.kind).key("slight").boolean(r.verdict.slight);
    common(j, r, toneOf(r.verdict), stale);
    j.close();
}

// 设备处理只作横幅，没有色调
void put(Json& j, const DeviceProcessingResult& r, bool stale)
{
    j.open();
    j.key("longestZeroMs").num(r.measures.longestZeroMs).key("zeroRuns").integer(r.measures.zeroRuns);
    j.key("detected").boolean(r.verdict.detected);
    common(j, r, Tone::None, stale);
    j.close();
}

void put(Json& j, const ReverbResult& r, bool stale)
{
    const auto& m = r.measures;
    j.open();
    j.key("t20Sec").nums(m.t20Sec).key("tMidSec").num(m.tMidSec);
    j.key("bandPnrDb").nums(m.bandPnrDb).key("bandR2").nums(m.bandR2);
    j.key("repeatMaxOverMin").num(m.repeatMaxOverMin);
    j.key("band").wire(r.verdict.band).key("gated").boolean(r.verdict.gated);
    common(j, r, toneOf(r.verdict), stale);
    j.close();
}

void put(Json& j, const DirectRatioResult& r, bool stale)
{
    j.open();
    j.key("drDb").num(r.measures.drDb).key("lowerBoundOnly").boolean(r.measures.lowerBoundOnly);
    j.key("band").wire(r.verdict.band);
    common(j, r, toneOf(r.verdict), stale);
    j.close();
}

void put(Json& j, const PlosiveResult& r, bool stale)
{
    j.open();
    j.key("worstRatioDb").num(r.measures.worstRatioDb).key("events").integer(r.measures.events);
    j.key("severity").wire(r.verdict.severity).key("lowSensitivity").boolean(r.verdict.lowSensitivity);
    common(j, r, severityTone(r.verdict.severity, kSeverityGraded.plosive), stale);
    j.close();
}

void put(Json& j, const SibilanceResult& r, bool stale)
{
    const auto& m = r.measures;
    j.open();
    j.key("midHighDb").num(m.midHighDb).key("highDb").num(m.highDb);
    j.key("effectiveBandwidthHz").num(m.effectiveBandwidthHz).key("events").integer(m.events);
    j.key("severity").wire(r.verdict.severity).key("highBandDisabled").boolean(r.verdict.highBandDisabled);
    common(j, r, severityTone(r.verdict.severity, kSeverityGraded.sibilance), stale);
    j.close();
}

void put(Json& j, const ClickResult& r, bool stale)
{
    const auto& m = r.measures;
    j.open();
    j.key("perMinute").num(m.perMinute).key("loudestRelDb").num(m.loudestRelDb);
    j.key("loudestDbfs").num(m.loudestDbfs).key("analysedSeconds").num(m.analysedSeconds);
    j.key("count").integer(m.count).key("severity").wire(r.verdict.severity);
    common(j, r, severityTone(r.verdict.severity, kSeverityGraded.click), stale);
    j.close();
}

// analysis 给出要重唱或已确认的那一对（当前方向与 repeatIndex）；优势只在确认成立时给出，其余为 null
void put(Json& j, const DirectionResult& r, bool stale, const core::DirectionAnalysis* analysis = nullptr)
{
    const auto& m = r.measures;
    j.open();
    j.key("levelDb").nums(m.levelDb).key("brightnessDb").nums(m.brightnessDb);
    j.key("currentIndex").integer(m.currentIndex).key("bestOtherAdvantageDb").num(m.bestOtherAdvantageDb);
    j.key("kind").wire(r.verdict.kind);
    // decideDirection 不进确认阶段也会填最响的别的方向，所以只在还要重唱、或确认成立（可能放反）时给出这一对
    const bool confirmed = analysis != nullptr && r.verdict.kind == core::DirectionCase::MaybeReversed;
    const bool pair = analysis != nullptr && (analysis->needsRepeat || confirmed);
    j.key("repeatIndex").integer(pair ? analysis->repeatIndex : -1);
    j.key("confirmedAdvantageDb").num(confirmed ? analysis->confirmedAdvantageDb : std::nan(""));
    common(j, r, toneOf(r.verdict), stale);
    j.close();
}

// 距离没有色调
void put(Json& j, const DistanceResult& r, bool stale)
{
    j.open();
    j.key("closeEvidence").integer(r.measures.closeEvidence).key("farEvidence").integer(r.measures.farEvidence);
    j.key("kind").wire(r.verdict.kind);
    common(j, r, Tone::None, stale);
    j.close();
}

template<class R>
void section(Json& j, const char* name, const std::optional<R>& r, const SectionSet& stale, Section s)
{
    if (r)
        put(j.key(name), *r, stale[static_cast<std::size_t>(s)]);
}

// 一步的显示状态：做完了但它产出的某一项过期了，就是 stale
StepStatus shownStatus(const SessionView& v, std::size_t step)
{
    const StepStatus s = v.steps[step].status;
    if (s != StepStatus::Done)
        return s;
    for (std::size_t i = 0; i < kSectionCount; ++i)
        if (v.stale[i] && static_cast<std::size_t>(producerOf(static_cast<Section>(i))) == step)
            return StepStatus::Stale;
    return s;
}

// 声道提示：声道检查的结论加宿主能力。没有单声道轨的宿主只保留“只有一路有声音”（换成不建议改轨道的说法），
// 同一个声音的提示没有意义（改不了单声道轨），相反相位照常提示
ChannelPrompt promptOf(const std::optional<ChannelsResult>& r, bool noMonoTracks)
{
    if (!r || r->confidence.level != Reliability::Trusted)
        return ChannelPrompt::None;
    switch (r->verdict.kind)
    {
    case ChannelCase::OneSilent:
    case ChannelCase::OneDominant:
        if (noMonoTracks)
            return ChannelPrompt::OneSilentHost;
        return r->verdict.kind == ChannelCase::OneSilent ? ChannelPrompt::OneSilent : ChannelPrompt::OneDominant;
    case ChannelCase::AntiPhase:
        return ChannelPrompt::AntiPhase;
    case ChannelCase::Identical:
        return noMonoTracks ? ChannelPrompt::None : ChannelPrompt::Identical;
    case ChannelCase::Undetermined:
    case ChannelCase::TrueStereo:
        break;
    }
    return ChannelPrompt::None;
}

void putConnect(Json& j, const SessionView& v)
{
    const auto& ns = v.results.noSignal;
    const auto& ch = v.results.channels;
    j.open();
    j.key("signalState").wire(ns ? ns->verdict.state : SignalState::Waiting);
    j.key("callbackAgeSec").num(std::nan("")); // 占位保住名表的键序；值由编辑器按墙钟填
    j.key("allZero").boolean(ns && ns->measures.zeroSeconds >= thr::no_signal::strict_zero_window_s &&
                             ns->verdict.state != SignalState::Ok);
    j.key("noiseDbfs");
    if (ns)
        j.num(ns->measures.p10FrameDbfs);
    else
        j.num(std::nan(""));
    j.key("channelStatusL").wire(ch ? ch->verdict.statusL : ChannelStatus::Unknown);
    j.key("channelStatusR").wire(ch ? ch->verdict.statusR : ChannelStatus::Unknown);
    j.key("channelPrompt").wire(promptOf(ch, v.noMonoTracks));
    j.key("channelPromptDismissed").boolean(v.channelPromptDismissed);
    j.key("analysedChannel").wire(ch ? ch->verdict.analysedChannel : AnalysedChannel::Left);
    j.close();
}

void putBands(Json& j)
{
    namespace g = thr::gain;
    j.open();
    j.key("tpTooQuietDbfs").num(g::tp_too_quiet_below_dbfs).key("tpQuietDbfs").num(g::tp_quiet_below_dbfs);
    j.key("tpLoudDbfs").num(g::tp_loud_above_dbfs).key("tpTooLoudDbfs").num(g::tp_too_loud_above_dbfs);
    j.key("targetPeakDbfs").num(g::target_peak_dbfs);
    j.key("snrBadBelowDb").num(thr::noise::snr_bad_below_db).key("snrGoodAboveDb").num(thr::noise::snr_good_above_db);
    j.key("t20DryBelowS").num(thr::reverb::dry_below_s).key("t20NormalBelowS").num(thr::reverb::normal_below_s);
    j.key("t20BoomyBelowS").num(thr::reverb::boomy_below_s);
    j.key("drLowBelowDb").num(thr::direct_ratio::low_below_db);
    j.key("drHighAboveDb").num(thr::direct_ratio::high_above_db);
    j.key("clipAdviceDropDb").num(g::clip_advice_drop_db).key("t20BandsHz").nums(thr::reverb::bands_hz);
    j.close();
}

void putLive(Json& j, const Live& l)
{
    j.open();
    j.key("phase").wire(l.phase).key("round").integer(l.round).key("rounds").integer(l.rounds);
    j.key("phaseElapsedSec").num(l.phaseElapsedSec).key("phaseDurationSec").num(l.phaseDurationSec);
    j.key("claps").openArray();
    for (ClapState c : l.claps)
        j.wire(c);
    j.closeArray();
    j.key("clapsNeeded").integer(std::lround(thr::session::room_claps_needed));
    j.key("clapsMax").integer(std::lround(thr::session::room_claps_max));
    j.key("directionIndex").integer(l.directionIndex);
    j.key("notice").wire(l.notice).key("noticeSec").num(l.noticeSec);
    j.key("clapDetails").openArray();
    for (const auto& c : l.clapDetails)
    {
        j.open();
        j.key("timeSec").num(c.timeSec).key("peakDbfs").num(c.peakDbfs).key("clipped").boolean(c.clipped);
        j.key("bandStatus").openArray();
        for (const auto& b : c.bands)
            j.wire(b.status);
        j.closeArray();
        j.key("t20Sec").openArray();
        for (const auto& b : c.bands)
            j.num(b.t20Sec);
        j.closeArray();
        j.close();
    }
    j.closeArray();
    j.close();
}

// 结果的各区段；previous 用同一形状，没有“上次”的区段不出现
void putResults(Json& j, const core::AllResults& r, const SectionSet& stale, const core::DirectionAnalysis* direction)
{
    j.open();
    section(j, "noSignal", r.noSignal, stale, Section::NoSignal);
    section(j, "channels", r.channels, stale, Section::Channels);
    section(j, "gain", r.gain, stale, Section::Gain);
    section(j, "noise", r.noise, stale, Section::Noise);
    section(j, "hum", r.hum, stale, Section::Hum);
    section(j, "deviceProcessing", r.deviceProcessing, stale, Section::DeviceProcessing);
    section(j, "reverb", r.reverb, stale, Section::Reverb);
    section(j, "directRatio", r.directRatio, stale, Section::DirectRatio);
    section(j, "plosive", r.plosive, stale, Section::Plosive);
    section(j, "sibilance", r.sibilance, stale, Section::Sibilance);
    section(j, "click", r.click, stale, Section::Click);
    if (r.direction)
        put(j.key("direction"), *r.direction, stale[static_cast<std::size_t>(Section::Direction)], direction);
    section(j, "distance", r.distance, stale, Section::Distance);
    j.close();
}

void putAdvice(Json& j, const Advice& a)
{
    j.open();
    j.key("room").wire(a.room).key("distance").wire(a.distance).key("directionPrompt").wire(a.directionPrompt);
    j.close();
}
} // namespace

std::string snapshotJson(const SessionView& v, core::MicType mic)
{
    Json j;
    j.open();
    j.key("steps").open();
    for (std::size_t i = 0; i < kStepCount; ++i)
    {
        const StepState& s = v.steps[i];
        j.key(kStepIdNames[i]).open();
        j.key("status").wire(shownStatus(v, i)).key("progressSec").num(s.progressSec);
        j.key("durationSec").num(v.durationSec[i]).key("invalidReason").wire(s.invalidReason);
        j.key("evidenceRev").integer(static_cast<long long>(v.evidenceRev[i]));
        j.key("runs").integer(static_cast<long long>(v.runs[i]));
        j.close();
    }
    j.close();

    putResults(j.key("results"), v.results, v.stale, v.direction ? &*v.direction : nullptr);

    putBands(j.key("bands"));
    putLive(j.key("live"), v.live);
    putConnect(j.key("connect"), v);
    j.key("offlineRender").boolean(v.offlineRender);
    const Advice advice = decideAdvice(v, mic);
    j.key("fixes").openArray();
    for (const auto f : advice.fixes)
        j.wire(f);
    j.closeArray();
    putAdvice(j.key("advice"), advice);
    putResults(j.key("previous"), v.previous, SectionSet{}, nullptr);
    j.close();
    return j.str();
}

// 事件种类与名表一一对应：枚举加了一项而名表没跟上时编不过，不会越界读
static_assert(bridge::kEvidenceEventKinds.size() == static_cast<std::size_t>(EvidenceEventKind::Count));

std::string evidenceJson(const EvidencePack& p)
{
    Json j;
    j.open();
    j.key("stepId")
        .wire(p.step)
        .key("rev")
        .integer(static_cast<long long>(p.rev))
        .key("durationSec")
        .num(p.durationSec);
    j.key("envelope").open().key("hopSec").num(p.hopSec);
    j.key("min").openArray();
    for (const auto v : p.min)
        j.integer(v);
    j.closeArray();
    j.key("max").openArray();
    for (const auto v : p.max)
        j.integer(v);
    j.closeArray();
    j.close();
    j.key("events").openArray();
    for (const auto& e : p.events)
    {
        j.open().key("kind").name(bridge::kEvidenceEventKinds[static_cast<std::size_t>(e.kind)]);
        j.key("timeSec").num(e.timeSec).key("durSec").num(e.durSec).key("levelDb").num(e.levelDb);
        j.close();
    }
    j.closeArray();
    j.close();
    return j.str();
}
} // namespace miciri::session
