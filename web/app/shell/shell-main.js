// SPDX-License-Identifier: GPL-3.0-or-later
//
// 外壳入口：建桥、建外壳、订阅快照、请求首个快照。
//
// 信号（写在 documentElement 的 data 属性上，冒烟和宿主都读它们）：
//   data-first-frame="1"  外壳骨架已画出（还没有快照时用默认语言）；
//   data-ready="1"        首个快照已应用；宿主此刻显示兜底面板（not-ready）时不会出现；
//   data-boot-error       启动失败的原因，页面不再继续。
// 外壳就绪后在 document 上派发 miciri:shell-ready（detail 是外壳对象），后面的页面由它拿到挂载点。

import { createBridge } from "../../shared/bridge.js";
import { createShell } from "./shell-app.js";

const de = document.documentElement;

async function boot() {
    const bridge = createBridge();
    const shell = createShell({ bridge });
    bridge.on("miciri.state", (s) => shell.setState(s));
    bridge.on("miciri.error", (e) => shell.onBridgeError(e));
    de.dataset.firstFrame = "1";
    document.dispatchEvent(new CustomEvent("miciri:shell-ready", { detail: shell }));
    // 首个快照：页面首次装载与编辑器重建后都要请求一次，桥不代调
    const snap = await bridge.requestInitialState();
    if (snap && snap.ok !== false) {
        shell.setState(snap);
        de.dataset.ready = "1";
    }
}

boot().catch((e) => {
    de.dataset.bootError = String((e && e.message) || e);
    console.error(e);
});
