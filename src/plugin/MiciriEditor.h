// SPDX-License-Identifier: GPL-3.0-or-later
//
// 编辑器：网页编辑器宿主加上本插件的快照、电平推送和桥函数。
// 只读处理器的状态：电平、心跳、设备信息；关窗不丢任何东西，重建后页面重新调 requestInitialState 取快照。
// 唯一的墙钟在这里：定时器按墙钟观察心跳计数有没有在走，得出 connect.callbackAgeSec。
#pragma once

#include <cstdint>

#include "MiciriProcessor.h"
#include "WebViewHost.h"

namespace miciri
{
class MiciriEditor : public webview::WebViewHost
{
public:
    MiciriEditor(MiciriProcessor& processor, Config config);
    ~MiciriEditor() override;

    // 定时器调用：状态每隔多少个节拍推一次（约每秒一次）；设备信息变化时立即推；
    // 测试进行中 Session 的状态在变，最多每 kSessionStateTicks 个节拍推一次（约每秒十次）。
    static constexpr int kStateEveryTicks = kTickHz;
    static constexpr int kSessionStateTicks = kTickHz / 10;

    // openExternal 只接受 https 链接，长度有上限。
    static constexpr int kExternalUrlMaxChars = 2048;

protected:
    juce::var buildSnapshot() override;
    void emitTick() override;
    void handleBridgeCall(const juce::String& name, const juce::Array<juce::var>& args,
                          NativeCompletion complete) override;

private:
    // 读心跳并更新“最近一次看到心跳变化”的墙钟时刻；返回现在距那一刻的秒数。
    // 只许在消息线程调用：定时器与构建快照都在消息线程上，它们共用并改写下面三个成员，不加锁。
    double observeCallbackAgeSec();
    juce::var buildMeters();

    MiciriProcessor& processor_;
    juce::String hostName_;
    bool guideSeen_ = false;

    std::uint64_t lastBeat_ = 0;
    double lastBeatChangeMs_ = 0.0;
    double lastAgeSec_ = 0.0;

    MiciriProcessor::DeviceInfo lastDevice_;
    int ticksSinceState_ = 0;
    std::uint64_t lastSessionVersion_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MiciriEditor)
};
} // namespace miciri
