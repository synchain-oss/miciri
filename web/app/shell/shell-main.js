// SPDX-License-Identifier: GPL-3.0-or-later
//
// 外壳入口：建桥、建外壳、订阅快照、请求首个快照。
//
// 信号（写在 documentElement 的 data 属性上，冒烟读它们）：
//   data-first-frame="1"  外壳骨架已画出（还没有快照时用默认语言）；
//   data-ready="1"        首个快照已应用；宿主此刻显示兜底面板（not-ready）时不会出现；
//   data-boot-error       启动失败的原因，页面不再继续。
// 同样三件事也经启动信号脚本（web/shared/boot-signal.js 挂的 window.__miciriBoot）报给原生宿主：
// 首帧信号让宿主揭开窗口，ready 让宿主记下启动完成，失败让宿主立刻换兜底面板而不是等看门狗超时。
// 外壳就绪后在 document 上派发 miciri:shell-ready（detail 是外壳对象），后面的页面由它拿到挂载点。

import { createBridge } from "../../shared/bridge.js";
import { createShell } from "./shell-app.js";

const de = document.documentElement;
// 启动信号脚本没加载上时用空实现，页面照常启动
const signal = window.__miciriBoot || { shellDrawn() {}, ready() {}, fail() {} };

async function boot() {
    const bridge = createBridge();
    const shell = createShell({ bridge });
    bridge.on("miciri.state", (s) => shell.setState(s));
    bridge.on("miciri.error", (e) => shell.onBridgeError(e));
    de.dataset.firstFrame = "1";
    signal.shellDrawn();
    document.dispatchEvent(new CustomEvent("miciri:shell-ready", { detail: shell }));
    // 首个快照：页面首次装载与编辑器重建后都要请求一次，桥不代调
    const snap = await bridge.requestInitialState();
    if (snap && snap.ok !== false) {
        shell.setState(snap);
        de.dataset.ready = "1";
        signal.ready();
    }
}

boot().catch((e) => {
    de.dataset.bootError = String((e && e.message) || e);
    signal.fail("shell", e);
    console.error(e);
});
