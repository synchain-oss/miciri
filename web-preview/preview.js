// SPDX-License-Identifier: GPL-3.0-or-later
//
// 预览器：把假后端放进 window.__MICIRI_MOCK__，再用同源 iframe 装载真源页面，并扮演宿主：
// iframe 的尺寸是设计盒乘档位，setUiScale 被接受后 iframe 跟着变，页面里的 shell-fit 随之重算倍率。
// 先放 mock 再给 iframe 赋 src，页面求值时 bridge.js 一定读得到父窗口上的 mock，没有时序问题。
// web/ 是页面代码的唯一真源，这里不复制任何界面代码。必须经 http 打开（node web-preview/serve.mjs）：
// file:// 下 iframe 变成跨源，读不到父窗口。
//
// 查询参数：lang=zh|en 开窗语言；scale=档位（须在档位表里）；locked=1 模拟缩放被锁（setUiScale 回 scale-locked）；guide=fresh 演首次启动（指南未看，开窗后自动打开一次）；
// autorun=1 让模拟的测试自己走（每秒一步）。

import { DESIGN } from "../web/shared/design-box.js";
import { createMockBackend } from "./mock/backend.js";

const query = new URLSearchParams(location.search);
const lang = query.get("lang") === "en" ? "en" : "zh";
const asked = Number(query.get("scale"));
const uiScale = DESIGN.shell.presets.includes(asked) ? asked : 1;
const frame = document.getElementById("frame");

function resize(f) {
    frame.style.width = `${DESIGN.shell.w * f}px`;
    frame.style.height = `${DESIGN.shell.h * f}px`;
}

resize(uiScale);
window.__MICIRI_MOCK__ = createMockBackend({
    lang,
    uiScale,
    guideSeen: query.get("guide") !== "fresh",
    onResize: resize,
    scaleLocked: query.get("locked") === "1",
});
frame.src = "../web/app/shell/index.html";

// autorun=1：每秒把流程模拟推进 1 秒，手动点开始就能看完整的一轮（冒烟不用它，直接调 sim.advance）
if (query.get("autorun") === "1") setInterval(() => window.__MICIRI_MOCK__.sim.advance(1), 1000);
