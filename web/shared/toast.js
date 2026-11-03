// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from synchain-oss/rinne; see upstream/web.json
//
// 深色提示条：右下角，4 秒后消失。
//
// 外观走 base.css 的 .sc-toast（深色玻璃底，前导圆点按语气着色，.dk 把焦点环换成深底配色），位置由页面样式里的 .mc-toast 决定，
// 不压页脚。role=status，读屏会播报，不抢焦点。语气只有 green、amber、red 三种，缺省 green。

import { el } from "./dom.js";

/** 提示条停留时间（毫秒）。 */
export const TOAST_MS = 4000;

/**
 * @param {{host:HTMLElement, timers?:{setTimeout:Function, clearTimeout:Function}}} o
 */
export function createToast(o) {
    const timers = o.timers || globalThis;
    const dot = el("span", { class: "sc-dot", aria: { hidden: "true" } });
    const text = el("span", { class: "mc-toast__text" });
    const root = el(
        "div",
        { class: "sc-toast mc-toast dk", role: "status", hidden: true, data: { part: "toast" } },
        dot,
        text,
    );
    o.host.append(root);
    let timer = 0;

    function hide() {
        if (timer) timers.clearTimeout(timer);
        timer = 0;
        root.hidden = true;
        text.textContent = "";
    }

    return {
        el: root,
        /** @param {{key:string, vals?:object, tone?:"green"|"red"|"amber", t:(k:string,v?:object)=>string}} p */
        show({ key, vals, tone, t }) {
            const tn = tone || "green";
            root.setAttribute("data-state", key);
            root.setAttribute("data-tone", tn);
            dot.setAttribute("data-tone", tn);
            text.textContent = t(key, vals);
            root.hidden = false;
            if (timer) timers.clearTimeout(timer);
            timer = timers.setTimeout(hide, TOAST_MS);
        },
        hide,
        isShown: () => !root.hidden,
    };
}
