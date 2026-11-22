// SPDX-License-Identifier: GPL-3.0-or-later
//
// 桥快照的拼装：Session 写的 JSON（步骤、结果、档位、实时信息、接通诊断、建议、上次、推荐距离），
// 再并上处理器一侧的非界面字段（micType、device）。键名与键序按 BridgeApi.h 的 kSnapshotRoot。
// 编辑器（requestInitialState 与 miciri.state）和测试构建的宿主探针共用这一份，两边不会漂移；
// 只有编辑器才有的东西由调用方给：界面字段（lang、uiScale、guideSeen），以及按墙钟观察的回调年龄。
#pragma once

#include <juce_core/juce_core.h>

#include <optional>

namespace miciri
{
class MiciriProcessor;

// 只有编辑器才有的界面字段，按契约插在 apiVersion 与 micType 之间
struct SnapshotUi
{
    juce::String lang;
    double uiScale = 1.0;
    bool guideSeen = false;
};

// ui 为空：不带界面字段。callbackAgeSec 为空：connect.callbackAgeSec 保留 Session 的占位值，
// 也不判“没有回调”（那只有墙钟看得出）。消息线程或宿主的准备线程上调；读 Session 的一致快照，不改任何状态。
juce::var buildBridgeSnapshot(MiciriProcessor& processor, const juce::String& hostName, const SnapshotUi* ui,
                              std::optional<double> callbackAgeSec);
} // namespace miciri
