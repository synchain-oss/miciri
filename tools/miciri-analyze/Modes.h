// SPDX-License-Identifier: GPL-3.0-or-later
//
// 命令行工具的分析模式。level 是整段录音的电平类分析（Report.h）；其余每个模式对应一项只吃整段样本的
// 纯函数，输出它的连续量与计数（字段名与 Result 结构一致），不加档位之外的新逻辑。
// 所有模式取同一路声道：单声道取它，立体声按声道检查选（pickChannel）。
#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "Wav.h"

namespace miciri::tools
{
enum class Mode
{
    Level,
    Room,
    Dr,
    Plosive,
    Sibilance,
    Clicks,
    Direction
};

// 命令行上的名字，按帮助里列出的顺序
inline constexpr std::array<std::pair<std::string_view, Mode>, 7> kModes{{{"level", Mode::Level},
                                                                          {"room", Mode::Room},
                                                                          {"dr", Mode::Dr},
                                                                          {"plosive", Mode::Plosive},
                                                                          {"sibilance", Mode::Sibilance},
                                                                          {"clicks", Mode::Clicks},
                                                                          {"direction", Mode::Direction}}};

// 认不出时为空
std::optional<Mode> parseMode(std::string_view name);
std::string_view modeName(Mode mode);

// 除 level 以外的模式。level 走 analyseToJson（它多一个 floorDbfs 参数）；传 level 时返回空串
std::string analyseModeToJson(Mode mode, const WavData& wav);
} // namespace miciri::tools
