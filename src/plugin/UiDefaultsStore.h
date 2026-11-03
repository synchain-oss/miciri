// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from synchain-oss/rinne; see upstream/plugin.json
//
// 界面的全局默认值：语言、指南已看、缩放档位。存在用户级的设置文件里，所有实例与宿主进程共用，
// 所以关窗、换工程、重开宿主都不丢；它不属于工程状态，不随工程保存。
// 多个宿主进程会同时读写同一个文件，所以每次访问都先取进程间锁。取锁等不到时进入一段避让期：
// 读直接放弃、写只等很短的时间，避免某个卡住的进程把所有编辑器的开窗都拖慢。
#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>

namespace miciri::uidefaults
{
inline constexpr const char* kProcessLockName = "MiciriUiDefaults";
inline constexpr int kProcessLockWriteWaitMs = 500;
inline constexpr int kProcessLockReadWaitMs = 100;
inline constexpr int kProcessLockBackoffMs = 5000;
inline constexpr int kProcessLockBackoffWriteWaitMs = 50;
inline constexpr int kProcessLockBackoffReadWaitMs = 0;

// 语言：langGlobal 没存过或值不认识时返回空串；chooseLangGlobal 把语言值和“用户显式选过”一次写完。
bool langChosenGlobal();
juce::String langGlobal();
void chooseLangGlobal(const juce::String& lang);

bool guideSeenGlobal();
void setGuideSeenGlobal(bool seen);

// 缩放百分比；0 表示没存过或存的值越界。写入越界值被忽略。
int uiScalePercent();
void setUiScalePercent(int percent);

// 测试用：把存储挪到指定目录，不碰用户真实的设置文件。传空 File 恢复默认。
void setStorageDirForTests(const juce::File& dir);
} // namespace miciri::uidefaults
