// SPDX-License-Identifier: GPL-3.0-or-later
//
// 分析结果的形状。每项结果由四块组成：
//   measures    连续量（dB、Hz、秒、次数），double 精度，只含算术类型；golden 只锁这一块
//   verdict     档位判定，由 measures 加 thresholds.json 的阈值得出；改阈值只动它，不动 measures
//   confidence  可信，或“再测一次”加原因
//   evidence    证据标记（时间段、频率、电平），给证据波形和详情用
// 不依赖 JUCE。字段名的 camelCase 就是桥快照里的字段名（见 docs/contracts/RESULT.md 与 BRIDGE.md）。
// 各项的算法在各自的模块里：level、noise、hum、channels、room、direction、voice，最该改与距离标尺在 decide。
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <vector>

#include "result/Enums.h"

namespace miciri::core
{
struct Confidence
{
    Reliability level = Reliability::Trusted;
    RetestReason reason = RetestReason::None; // level 为 Trusted 时必须是 None
};
constexpr Confidence trusted()
{
    return {};
}
// 再测必须带原因：传入 None 时退化为 Ambiguous，保证界面总有一句话可说
constexpr Confidence retest(RetestReason why)
{
    return {Reliability::Retest, why == RetestReason::None ? RetestReason::Ambiguous : why};
}
constexpr bool isConsistent(Confidence c)
{
    return (c.level == Reliability::Trusted) == (c.reason == RetestReason::None);
}

enum class EvidenceKind : std::uint8_t
{
    DigitalClip,
    FlatTop,
    HumPeak,
    ZeroRun,
    Click,
    Plosive,
    Sibilant
};
struct EvidenceMark
{
    EvidenceKind kind = EvidenceKind::DigitalClip;
    double startSec = 0.0; // 相对该步开始的采样计时
    double endSec = 0.0;
    double value = 0.0; // 取决于 kind：电平 dB、频率 Hz、比值 dB
    int channel = -1; // 0 左，1 右，-1 与声道无关
};
struct Evidence
{
    std::vector<EvidenceMark> marks;
};

// 只检查可平凡复制与标准布局；成员是否全是数值，C++ 无法枚举，
// 由评审和 golden 把关。
template<class T>
inline constexpr bool isMeasures = std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>;

template<class Measures, class Verdict>
struct Result
{
    static_assert(isMeasures<Measures>, "measures must stay plain numbers so a golden can lock them");
    Measures measures{};
    Verdict verdict{};
    Confidence confidence{};
    Evidence evidence{};
};

// ---- 无信号诊断 ----
// 这里只放由样本算出的量。“没有回调”时没有样本，它靠墙钟判断，不进 measures（否则 golden 会依赖时序）：
// 距上次回调的秒数在桥快照的 connect.callbackAgeSec，由编辑器的定时器读心跳得出，state 的 no-callback 也由那一侧给出。
struct NoSignalMeasures
{
    double zeroSeconds = 0.0; // 严格全 0 的累计时长
    double maxFrameDbfs = 0.0;
    double p10FrameDbfs = 0.0;
    double riseDb = 0.0; // maxFrame 减 P10
};
struct NoSignalVerdict
{
    SignalState state = SignalState::Waiting;
};
using NoSignalResult = Result<NoSignalMeasures, NoSignalVerdict>;

// ---- 声道检查（只在立体声轨上做；单声道轨没有这项结果）----
struct ChannelsMeasures
{
    double floorDbfsL = 0.0;
    double floorDbfsR = 0.0;
    double activeDbfsL = 0.0;
    double activeDbfsR = 0.0;
    double deltaDb = 0.0; // 两路活跃电平差，L 减 R
    double weakRiseDb = 0.0; // 较弱一路说话时比自己底噪高多少
    double sumOverFloorDb = 0.0; // (L+R) 比两路底噪之和高多少
    double diffOverFloorDb = 0.0; // (L-R) 同上
    bool usedFallback = false; // 没有停顿可估底噪，走了相对信号的回退阈值
};
struct ChannelsVerdict
{
    ChannelCase kind = ChannelCase::Undetermined;
    ChannelStatus statusL = ChannelStatus::Unknown;
    ChannelStatus statusR = ChannelStatus::Unknown;
    AnalysedChannel analysedChannel = AnalysedChannel::Left; // 之后的测试只分析这一路，不把两路相加
};
using ChannelsResult = Result<ChannelsMeasures, ChannelsVerdict>;

// ---- 增益：数字削波与模拟平顶分开计数，平顶最多到琥珀 ----
struct GainMeasures
{
    double samplePeakDbfsL = 0.0;
    double samplePeakDbfsR = 0.0;
    double truePeakDbfsL = 0.0;
    double truePeakDbfsR = 0.0;
    double loudnessLkfs = 0.0; // 活跃段 K 加权响度
    std::int64_t digitalClipRunsL = 0; // 连续超过阈值的段数（主判据）
    std::int64_t digitalClipRunsR = 0;
    std::int64_t flatPlateauPeaksL = 0; // 落在同一平台电平上的波峰数（辅助判据）
    std::int64_t flatPlateauPeaksR = 0;
};
struct GainVerdict
{
    GainBand band = GainBand::Good;
    bool digitalClip = false; // 任一声道有数字削波：红，点亮削波灯
    bool maybeFlatTop = false; // 只写“可能削波”，不点灯，不单独变红
};
using GainResult = Result<GainMeasures, GainVerdict>;

// ---- 噪声 ----
struct NoiseMeasures
{
    double noiseDbfs = 0.0; // 全频，信息项，不分档；也是电平管上底噪区间的上沿
    double noiseDbfsA = 0.0; // A 加权，注明不是 dBA
    double snrDb = 0.0; // A 加权 SNR，唯一的分档依据
    double noisePeakDbfs = 0.0; // 全频最响一帧，电平管悬停时的峰值线
};
struct NoiseVerdict
{
    NoiseBand band = NoiseBand::Good;
};
using NoiseResult = Result<NoiseMeasures, NoiseVerdict>;

// ---- 电流声 ----
struct HumMeasures
{
    double fundamentalHz = 0.0; // 啸叫时是那条窄峰的频率
    double overFloorDb = 0.0; // 峰高出本底
    // 单位是百分比（0 到 100）。阈值 hum.slight_energy_share_frac 是比例，
    // 比较前先除以 100。
    double energySharePct = 0.0; // 占噪声能量的百分比
    double highestHarmonicHz = 0.0;
    double levelDbfs = 0.0; // 电流声成分的均值，电平管上区间的上沿
    double peakDbfs = 0.0; // 电平管悬停时的峰值线
};
struct HumVerdict
{
    HumKind kind = HumKind::None;
    bool slight = false; // 占比低于阈值：仍报，标“轻微”，不进“最该改”
};
using HumResult = Result<HumMeasures, HumVerdict>;

// ---- 设备处理：只作横幅，不进“最该改” ----
struct DeviceProcessingMeasures
{
    double longestZeroMs = 0.0; // 夹在非零样本之间的最长严格数字零
    std::int64_t zeroRuns = 0;
};
struct DeviceProcessingVerdict
{
    bool detected = false;
};
using DeviceProcessingResult = Result<DeviceProcessingMeasures, DeviceProcessingVerdict>;

// ---- 房间与位置（拍手）----
struct ReverbMeasures
{
    std::array<double, 4> t20Sec{}; // 500、1k、2k、4k；该频带给不出值时为 0
    double tMidSec = 0.0; // (500 + 1k) / 2；两个频带都有值才有，否则为 0
    std::array<double, 4> bandPnrDb{}; // 各频带峰噪比的中位数（有噪声参考的每一下）
    std::array<double, 4> bandR2{}; // 各频带留下的几下拟合 R² 的中位数
    double repeatMaxOverMin = 0.0; // 去掉偏离中位数的几下之后，各频带留下的最大比最小，取最差的频带
};
struct ReverbVerdict
{
    ReverbBand band = ReverbBand::Normal;
    // 门限（尾部数字零或骤降）只作废那一下的那个频带；本标志表示门限作废的太多，主指标给不出（再测原因 gate-detected）
    bool gated = false;
};
using ReverbResult = Result<ReverbMeasures, ReverbVerdict>;

struct DirectRatioMeasures
{
    double drDb = 0.0;
    bool lowerBoundOnly = false; // 尾部测不到时只报下限
};
struct DirectRatioVerdict
{
    DrBand band = DrBand::Mid;
};
using DirectRatioResult = Result<DirectRatioMeasures, DirectRatioVerdict>;

// ---- 算法未实现的几项：只定字段 ----
struct PlosiveMeasures
{
    double worstRatioDb = 0.0; // 取最大值，不取中位数
    std::int64_t events = 0;
};
struct PlosiveVerdict
{
    Severity severity = Severity::Light;
    bool lowSensitivity = false; // 低基频下检出率下降
};
using PlosiveResult = Result<PlosiveMeasures, PlosiveVerdict>;

struct SibilanceMeasures
{
    double midHighDb = 0.0; // 3-6 kHz
    double highDb = 0.0; // 6-12 kHz
    double effectiveBandwidthHz = 0.0;
    std::int64_t events = 0;
};
struct SibilanceVerdict
{
    Severity severity = Severity::Light;
    bool highBandDisabled = false; // 有效带宽不足，高频项关闭
};
using SibilanceResult = Result<SibilanceMeasures, SibilanceVerdict>;

struct ClickMeasures
{
    double perMinute = 0.0;
    // count 为 0 时下面两项不是测量值，留默认的 0：先看 count
    double loudestRelDb = 0.0; // 最响一次相对本人语音
    double loudestDbfs = 0.0; // 最响一次的绝对电平，电平管外的圆点
    double analysedSeconds = 0.0;
    std::int64_t count = 0;
};
struct ClickVerdict
{
    Severity severity = Severity::Light;
};
using ClickResult = Result<ClickMeasures, ClickVerdict>;

// ---- 方向 ----
struct DirectionMeasures
{
    std::array<double, 4> levelDb{}; // 四个方向，活跃段 K 加权中位数
    std::array<double, 4> brightnessDb{}; // 3-10 kHz 相对 300-1000 Hz
    int currentIndex = 0; // 用户当前的方向
    double bestOtherAdvantageDb = 0.0; // 最响的别的方向比当前方向响多少
};
struct DirectionVerdict
{
    DirectionCase kind = DirectionCase::Uncertain;
};
using DirectionResult = Result<DirectionMeasures, DirectionVerdict>;

// ---- 距离标尺：由喷麦、齿音、口水音与 D/R、SNR 的证据合成 ----
struct DistanceMeasures
{
    int closeEvidence = 0; // 指向太近的独立证据数：喷麦、齿音、口水音
    int farEvidence = 0; // 指向太远的独立证据数：D/R 低；SNR 低只作次要
};
struct DistanceVerdict
{
    DistanceCase kind = DistanceCase::Unknown;
};
using DistanceResult = Result<DistanceMeasures, DistanceVerdict>;

// Session 持有的全部结果：没做过的步骤是空，过期与否由 StepStatus 表达，结果本身不改
struct AllResults
{
    std::optional<NoSignalResult> noSignal;
    std::optional<ChannelsResult> channels;
    std::optional<GainResult> gain;
    std::optional<NoiseResult> noise;
    std::optional<HumResult> hum;
    std::optional<DeviceProcessingResult> deviceProcessing;
    std::optional<ReverbResult> reverb;
    std::optional<DirectRatioResult> directRatio;
    std::optional<PlosiveResult> plosive;
    std::optional<SibilanceResult> sibilance;
    std::optional<ClickResult> click;
    std::optional<DirectionResult> direction;
    std::optional<DistanceResult> distance;
};

// ---- 色调（档位到颜色的固定映射）----
Tone worse(Tone a, Tone b); // 红 > 琥珀 > 绿 > 无
Tone toneOf(const NoSignalVerdict& v);
Tone toneOf(const ChannelsVerdict& v);
Tone toneOf(const GainVerdict& v);
Tone toneOf(const NoiseVerdict& v);
Tone toneOf(const HumVerdict& v);
Tone toneOf(const ReverbVerdict& v);
Tone toneOf(const DirectRatioVerdict& v);
Tone toneOf(const DirectionVerdict& v);
Tone toneOf(Severity s);
// 卡片的颜色：再测的结果没有结论，一律无色；可信时才按档位映射。toneOf 只看档位、不看置信度，
// 卡片与桥取颜色用这个。
template<class Measures, class Verdict>
Tone cardTone(const Result<Measures, Verdict>& r)
{
    return r.confidence.level == Reliability::Retest ? Tone::None : toneOf(r.verdict);
}
} // namespace miciri::core
