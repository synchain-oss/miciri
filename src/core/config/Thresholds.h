// SPDX-License-Identifier: GPL-3.0-or-later
//
// 阈值的唯一入口：thresholds.json 在配置期生成 <miciri/thresholds_generated.h>，这里只转一手。
// 用法：thr::gain::clip_digital_threshold。值为 null（还没有值）的项没有生成，引用会编译失败。
// 代码里不写魔法数：要用的数先进 thresholds.json（带来源、日期、状态），再从这里取。
#pragma once

#include <miciri/thresholds_generated.h>
