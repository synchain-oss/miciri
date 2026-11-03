// SPDX-License-Identifier: GPL-3.0-or-later
#include "noise/Noise.h"

#include <algorithm>
#include <cmath>

#include "config/Thresholds.h"

namespace miciri::core
{
namespace
{
std::size_t frameLength(double frameMs, double rate)
{
    return static_cast<std::size_t>(std::llround(frameMs * 1e-3 * rate));
}

std::vector<double> toDb(const std::vector<double>& powers)
{
    std::vector<double> db;
    db.reserve(powers.size());
    for (double p : powers)
        db.push_back(dsp::powerToDb(p));
    return db;
}
} // namespace

NoiseAnalyser::NoiseAnalyser(double sampleRate, NoiseContext ctx)
    : ctx_(ctx), input_(sampleRate, thr::noise::analysis_rate_max_hz), weighting_(dsp::makeAWeighting(input_.rate())),
      weighted_(frameLength(thr::noise::frame_ms, input_.rate())),
      plain_(frameLength(thr::noise::frame_ms, input_.rate()))
{
}

void NoiseAnalyser::push(const double* samples, std::size_t count)
{
    decimated_.clear();
    input_.push(samples, count, decimated_);
    for (double s : decimated_)
    {
        plain_.add(s);
        weighted_.add(weighting_.process(s));
    }
}

NoiseResult NoiseAnalyser::finish() const
{
    NoiseResult result;
    if (weighted_.frames().empty())
    {
        result.confidence = retest(RetestReason::TooShort);
        return result;
    }
    const std::vector<double> plainPower = plain_.frames();
    const std::vector<double> weightedPower = weighted_.frames();
    // 整帧严格零不计入底噪：门限或降噪把一段压成零时，那不是“底噪很低”，只会把 P10 拖到 -300 dB，
    // 还会让设备处理把真正的零段当成量化零而漏报。以未加权的帧为准，A 加权滤波器在零段里的拖尾不算。
    std::vector<double> livePlain;
    std::vector<double> liveWeighted;
    for (std::size_t i = 0; i < plainPower.size(); ++i)
    {
        if (plainPower[i] > 0.0)
        {
            livePlain.push_back(plainPower[i]);
            liveWeighted.push_back(weightedPower[i]);
        }
    }
    NoiseMeasures& m = result.measures;
    m.noiseDbfsA =
        liveWeighted.empty() ? dsp::powerToDb(0.0) : dsp::percentile(toDb(liveWeighted), thr::noise::percentile);
    m.noiseDbfs = livePlain.empty() ? dsp::powerToDb(0.0) : dsp::percentile(toDb(livePlain), thr::noise::percentile);
    const std::vector<double> plainDb = toDb(plainPower);
    m.noisePeakDbfs = *std::max_element(plainDb.begin(), plainDb.end());

    if (ctx_.speechLevelDbfsA)
    {
        m.snrDb = *ctx_.speechLevelDbfsA - m.noiseDbfsA;
        result.verdict.band = m.snrDb < thr::noise::snr_bad_below_db    ? NoiseBand::Bad
                              : m.snrDb > thr::noise::snr_good_above_db ? NoiseBand::Good
                                                                        : NoiseBand::Fair;
    }
    else
    {
        result.verdict.band = NoiseBand::Fair; // 没有说话电平，分不出档
        result.confidence = retest(RetestReason::Ambiguous);
    }
    if (weighted_.frames().size() < static_cast<std::size_t>(thr::noise::min_frames))
        result.confidence = retest(RetestReason::TooShort);
    return result;
}

NoiseResult analyseNoise(const std::vector<double>& samples, double sampleRate, const NoiseContext& ctx)
{
    NoiseAnalyser analyser(sampleRate, ctx);
    analyser.push(samples.data(), samples.size());
    return analyser.finish();
}

SpeechLevelAnalyser::SpeechLevelAnalyser(double sampleRate)
    : input_(sampleRate, thr::noise::analysis_rate_max_hz), weighting_(dsp::makeAWeighting(input_.rate())),
      frames_(frameLength(thr::noise::speech_frame_ms, input_.rate()))
{
}

void SpeechLevelAnalyser::push(const double* samples, std::size_t count)
{
    decimated_.clear();
    input_.push(samples, count, decimated_);
    for (double s : decimated_)
        frames_.add(weighting_.process(s));
}

std::optional<double> SpeechLevelAnalyser::finish() const
{
    const std::vector<double>& powers = frames_.frames();
    if (powers.empty())
        return std::nullopt;
    const std::vector<double> db = toDb(powers);
    const double top = dsp::percentile(db, thr::noise::speech_top_percentile);
    const double floorDb = dsp::percentile(db, thr::noise::percentile);
    double sum = 0.0;
    std::size_t active = 0;
    for (std::size_t i = 0; i < powers.size(); ++i)
    {
        if (db[i] >= top - thr::noise::speech_within_top_db && db[i] >= floorDb + thr::noise::speech_over_p10_db)
        {
            sum += powers[i];
            ++active;
        }
    }
    if (active == 0)
        return std::nullopt;
    return dsp::powerToDb(sum / static_cast<double>(active));
}

std::optional<double> speechLevelDbfsA(const std::vector<double>& samples, double sampleRate)
{
    SpeechLevelAnalyser analyser(sampleRate);
    analyser.push(samples.data(), samples.size());
    return analyser.finish();
}
} // namespace miciri::core
