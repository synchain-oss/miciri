// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from synchain-oss/rinne; see upstream/plugin.json
//
// 桥的 C++ 公共部分：事件名与错误原因常量、缩放档位模型、回执构造。
// 名字以 BridgeApi.h 的契约表为准，这里用 static_assert 钉住；快照的字段由编辑器按契约表填写。
#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string_view>

#include "BridgeApi.h"
#include "DesignBox.h"

namespace miciri::bridge
{
template<class Table>
constexpr bool listed(const Table& table, std::string_view name)
{
    for (const auto& item : table)
        if (item == name)
            return true;
    return false;
}

inline constexpr const char* kStateEvent = "miciri.state";
inline constexpr const char* kMetersEvent = "miciri.meters";
inline constexpr const char* kErrorEvent = "miciri.error";
static_assert(listed(kEvents, kStateEvent) && listed(kEvents, kMetersEvent) && listed(kEvents, kErrorEvent),
              "事件名必须出现在 BridgeApi.h 的 kEvents 里");

inline constexpr const char* kBadLang = "bad-lang";
inline constexpr const char* kBadScale = "bad-scale";
inline constexpr const char* kBadArg = "bad-arg";
inline constexpr const char* kNoPending = "no-pending";
inline constexpr const char* kScaleLocked = "scale-locked";
inline constexpr const char* kNotReady = "not-ready";
static_assert(listed(kReasons, kBadLang) && listed(kReasons, kBadScale) && listed(kReasons, kBadArg) &&
                  listed(kReasons, kNoPending) && listed(kReasons, kScaleLocked) && listed(kReasons, kNotReady),
              "错误原因必须出现在 BridgeApi.h 的 kReasons 里");

inline constexpr float kDefaultUiScale = 1.0f;

float clampUiScale(float scale);

// 存盘的缩放百分比是否落在档位表的范围内。
bool percentInRange(int percent);

inline bool isSupportedLang(const juce::String& code)
{
    return code == "zh" || code == "en";
}

struct DesignBoxSize
{
    int width = 0;
    int height = 0;
};

DesignBoxSize designBoxWindowSize(float scale);

// 请求的档位在当前屏幕放不下时，降到放得下的最大档位；一档都放不下就用最小档。
float fitScaleToDisplay(float requested, int availW, int availH);

// 把存盘的百分比吸附到档位表；不在表里返回默认档位。
float scaleFromStoredPercent(int percent);

struct ScaleOutcome
{
    const char* reason = nullptr;
    float scale = kDefaultUiScale;
    bool ok() const noexcept { return reason == nullptr; }
};

// 缩放的 10 秒确认回退：apply 只预览，commit 才算数。窗口被锁定缩放时一律拒绝，并按默认档位显示。
class UiScaleModel
{
public:
    explicit UiScaleModel(float saved) noexcept
        : requested_(clampUiScale(saved)), applied_(requested_), settled_(requested_)
    {
    }

    float scale() const noexcept { return locked_ ? kDefaultUiScale : applied_; }
    bool pending() const noexcept { return pending_; }
    bool locked() const noexcept { return locked_; }

    float fitToDisplay(int availW, int availH)
    {
        applied_ = clampUiScale(fitScaleToDisplay(requested_, availW, availH));
        settled_ = applied_;
        pending_ = false;
        return scale();
    }

    void setLocked(bool shouldLock) noexcept
    {
        locked_ = shouldLock;
        if (locked_)
        {
            pending_ = false;
            applied_ = settled_;
        }
    }

    ScaleOutcome apply(float next) noexcept
    {
        if (locked_)
            return {kScaleLocked, scale()};
        applied_ = clampUiScale(next);
        pending_ = true;
        return {nullptr, applied_};
    }

    ScaleOutcome commit() noexcept
    {
        if (locked_)
            return {kScaleLocked, scale()};
        if (!pending_)
            return {kNoPending, scale()};
        pending_ = false;
        settled_ = applied_;
        return {nullptr, applied_};
    }

private:
    float requested_;
    float applied_; // 没锁定时窗口用的档位；有待确认的预览时是预览档位
    float settled_; // 最近一次确认（或开窗夹取、构造）的档位
    bool pending_ = false;
    bool locked_ = false;
};

inline int scaleToPercent(float scale)
{
    return juce::roundToInt(scale * 100.0f);
}

inline double scaleForJson(float scale)
{
    return std::round(static_cast<double>(scale) * 10000.0) / 10000.0;
}

// 布尔或整数（非零为真）才算数，其他类型一律拒绝。
bool strictBool(const juce::var& v, bool& out);

// 参数必须是档位表里的数。
bool parseUiScaleArg(const juce::Array<juce::var>& args, float& outScale);

juce::var okResponse();
juce::var okResponseWith(const juce::String& key, const juce::var& value);
juce::var failResponse(const char* reason);
juce::var respondNotReady();

juce::var respondSetUiScale(UiScaleModel& model, const juce::Array<juce::var>& args, bool& windowChanged);
juce::var respondCommitUiScale(UiScaleModel& model, const std::function<void(float)>& persist);
juce::var respondSetLang(const juce::Array<juce::var>& args, const std::function<void(const juce::String&)>& accepted);

// 首帧信号里带的“信号到首次绘制”毫秒差，整形成一段日志后缀；缺失或不是有限数时写“无绘制记录”。
juce::String paintDeltaNote(const juce::var& payload, const char* fieldName);
} // namespace miciri::bridge
