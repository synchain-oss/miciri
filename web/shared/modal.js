// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from synchain-oss/rinne; see upstream/web.json
//
// 模态的公共骨架：遮罩加深色玻璃面板，关于窗与缩放确认窗共用。
//
// 约定：
//   - 遮罩铺满外壳，面板在遮罩里居中；
//   - 打开时把 inert 列表里的节点设为 inert，背景点不到、Tab 进不去。inert 按节点计数：
//     两个模态同时开着时，先关的那个不能把 inert 清掉，数到 0 才移除；
//   - 打开时记住焦点并把焦点交给面板（或 o.initialFocus 指定的元素），关闭时还回去（o.returnTo 优先）；
//   - Tab 在面板内循环：页脚等不在 inert 列表里的节点也不会被 Tab 走到；
//   - Esc 调 onEsc，由调用方决定是取消还是关闭；
//   - 面板 role=dialog、aria-modal=true，名字由调用方用 aria-labelledby 指到标题。

import { el } from "./dom.js";

/** 每个节点被几个打开着的模态要求 inert。 */
const inertCount = new WeakMap();

function lockInert(node) {
    inertCount.set(node, (inertCount.get(node) || 0) + 1);
    node.setAttribute("inert", "");
}

function unlockInert(node) {
    const left = (inertCount.get(node) || 0) - 1;
    if (left > 0) {
        inertCount.set(node, left);
        return;
    }
    inertCount.delete(node);
    node.removeAttribute("inert");
}

const FOCUSABLE = "button:not([disabled]), [href], input:not([disabled]), [tabindex]:not([tabindex='-1'])";

/**
 * @param {object} o
 * @param {HTMLElement} o.host 挂载点（外壳的模态层）
 * @param {string} o.part 面板的 data-part，冒烟用它定位
 * @param {number} o.width 面板宽（设计 px）
 * @param {HTMLElement[]} [o.inert] 打开时设为 inert 的节点
 * @param {()=>void} [o.onEsc] 按 Esc 时调用
 * @param {()=>(HTMLElement|null)} [o.returnTo] 关闭后把焦点交给谁；缺省还给打开前的焦点元素
 * @param {()=>(HTMLElement|null)} [o.initialFocus] 打开后把焦点交给谁；缺省给面板本身
 */
export function createModal(o) {
    const panel = el("div", {
        class: "sc-modal dk",
        role: "dialog",
        tabindex: "-1",
        aria: { modal: "true" },
        data: { part: o.part },
        style: { width: `${o.width}px` },
    });
    const scrim = el("div", { class: "sc-scrim dk", data: { part: "scrim" }, hidden: true }, panel);
    o.host.append(scrim);
    const inertNodes = o.inert || [];
    let fallbackFocus = null;

    scrim.addEventListener("keydown", (e) => {
        if (e.key === "Escape" && typeof o.onEsc === "function") {
            e.preventDefault();
            e.stopPropagation();
            o.onEsc();
            return;
        }
        if (e.key !== "Tab") return;
        const items = Array.from(panel.querySelectorAll(FOCUSABLE));
        if (items.length === 0) {
            e.preventDefault();
            return;
        }
        const i = items.indexOf(document.activeElement);
        if (e.shiftKey && i <= 0) {
            e.preventDefault();
            items[items.length - 1].focus();
        } else if (!e.shiftKey && i === items.length - 1) {
            e.preventDefault();
            items[0].focus();
        }
    });

    return {
        scrim,
        panel,
        isShown: () => !scrim.hidden,
        show() {
            if (!scrim.hidden) return;
            fallbackFocus = document.activeElement;
            scrim.hidden = false;
            for (const n of inertNodes) lockInert(n);
            const first = typeof o.initialFocus === "function" ? o.initialFocus() : null;
            (first || panel).focus({ preventScroll: true });
        },
        hide() {
            if (scrim.hidden) return;
            scrim.hidden = true;
            for (const n of inertNodes) unlockInert(n);
            const back = (typeof o.returnTo === "function" ? o.returnTo() : null) || fallbackFocus;
            if (back && back.isConnected && typeof back.focus === "function") back.focus({ preventScroll: true });
            fallbackFocus = null;
        },
    };
}
