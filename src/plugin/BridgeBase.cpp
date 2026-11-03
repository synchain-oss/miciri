// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from synchain-oss/rinne; see upstream/plugin.json
//
// 桥公共部分的实现：档位换算、参数校验与回执。
#include "BridgeBase.h"

#include <algorithm>

namespace miciri::bridge
{
namespace
{
constexpr int kPaintDeltaClampMs = 60000;

bool isPreset(double f)
{
    return std::find(design::kPresets.begin(), design::kPresets.end(), f) != design::kPresets.end();
}

bool fits(double f, int availW, int availH)
{
    return juce::roundToInt(design::kDesignW * f) + design::kHostFrameW <= availW &&
           juce::roundToInt(design::kDesignH * f) + design::kHostFrameH <= availH;
}
} // namespace

float clampUiScale(float scale)
{
    return juce::jlimit(static_cast<float>(design::kPresets.front()), static_cast<float>(design::kPresets.back()),
                        scale);
}

bool percentInRange(int percent)
{
    const int lo = juce::roundToInt(static_cast<float>(design::kPresets.front()) * 100.0f);
    const int hi = juce::roundToInt(static_cast<float>(design::kPresets.back()) * 100.0f);
    return percent >= lo && percent <= hi;
}

DesignBoxSize designBoxWindowSize(float scale)
{
    return {juce::roundToInt(static_cast<float>(design::kDesignW) * scale),
            juce::roundToInt(static_cast<float>(design::kDesignH) * scale)};
}

float fitScaleToDisplay(float requested, int availW, int availH)
{
    if (availW <= 0 || availH <= 0 || fits(static_cast<double>(requested), availW, availH))
        return requested;
    for (auto it = design::kPresets.rbegin(); it != design::kPresets.rend(); ++it)
        if (fits(*it, availW, availH))
            return static_cast<float>(*it);
    return static_cast<float>(design::kPresets.front());
}

float scaleFromStoredPercent(int percent)
{
    for (const double f : design::kPresets)
        if (juce::roundToInt(f * 100.0) == percent)
            return static_cast<float>(f);
    return kDefaultUiScale;
}

bool strictBool(const juce::var& v, bool& out)
{
    if (v.isBool())
    {
        out = static_cast<bool>(v);
        return true;
    }
    if (v.isInt() || v.isInt64())
    {
        out = static_cast<juce::int64>(v) != 0;
        return true;
    }
    return false;
}

bool parseUiScaleArg(const juce::Array<juce::var>& args, float& outScale)
{
    if (args.size() < 1)
        return false;
    const auto& v = args[0];
    if (!(v.isDouble() || v.isInt()))
        return false;
    const double f = static_cast<double>(v);
    if (!isPreset(f))
        return false;
    outScale = static_cast<float>(f);
    return true;
}

juce::var okResponse()
{
    auto* o = new juce::DynamicObject();
    o->setProperty("ok", true);
    return juce::var(o);
}

juce::var okResponseWith(const juce::String& key, const juce::var& value)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("ok", true);
    o->setProperty(key, value);
    return juce::var(o);
}

juce::var failResponse(const char* reason)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("ok", false);
    o->setProperty("reason", reason);
    return juce::var(o);
}

juce::var respondNotReady()
{
    return failResponse(kNotReady);
}

juce::var respondSetUiScale(UiScaleModel& model, const juce::Array<juce::var>& args, bool& windowChanged)
{
    windowChanged = false;
    float scale = model.scale();
    if (!parseUiScaleArg(args, scale))
        return failResponse(kBadScale);
    const auto outcome = model.apply(scale);
    if (!outcome.ok())
        return failResponse(outcome.reason);
    windowChanged = true;
    return okResponseWith("scale", scaleForJson(outcome.scale));
}

juce::var respondCommitUiScale(UiScaleModel& model, const std::function<void(float)>& persist)
{
    const auto outcome = model.commit();
    if (!outcome.ok())
        return failResponse(outcome.reason);
    if (persist)
        persist(outcome.scale);
    return okResponseWith("scale", scaleForJson(outcome.scale));
}

juce::var respondSetLang(const juce::Array<juce::var>& args, const std::function<void(const juce::String&)>& accepted)
{
    if (args.size() < 1 || !args[0].isString())
        return failResponse(kBadLang);
    const juce::String code = args[0].toString();
    if (!isSupportedLang(code))
        return failResponse(kBadLang);
    if (accepted)
        accepted(code);
    return okResponseWith("lang", code);
}

juce::String paintDeltaNote(const juce::var& payload, const char* fieldName)
{
    const auto delta = payload.getProperty(juce::Identifier(fieldName), juce::var());
    const bool usable =
        delta.isInt() || delta.isInt64() || (delta.isDouble() && std::isfinite(static_cast<double>(delta)));
    if (!usable)
        return " (no paint record)";
    const auto n = static_cast<int>(juce::jlimit(-static_cast<double>(kPaintDeltaClampMs),
                                                 static_cast<double>(kPaintDeltaClampMs), static_cast<double>(delta)));
    return juce::String(" (signal-firstPaint ") + (n >= 0 ? "+" : "") + juce::String(n) + " ms)";
}
} // namespace miciri::bridge
