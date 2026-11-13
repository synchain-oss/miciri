// SPDX-License-Identifier: GPL-3.0-or-later
//
// 应用内指南的界面：暗幕加圆角亮洞（画布），旁边一张说明框。
// 点任意处下一步，Esc 跳过，左右键翻页；引导期间背景 inert，Tab 只在说明框的按钮里循环；
// 找不到目标的步骤整步跳过。所有文字走词条，节点一律用 DOM API 搭，不拼 HTML。
//
// 画布按设计盒 1120×700 绘制，后备存储的倍率取 dpr × 外壳缩放（与证据画布同口径），
// 目标矩形按外壳缩放换回设计坐标（holeFromRect）。

import { el } from "../../shared/dom.js";
import { DESIGN } from "../../shared/design-box.js";
import { backingFitFactor, shellFitFactor } from "../../shared/shell-fit.js";
import { GEOM, holeFromRect, placeCallout, presentSteps, turn } from "./guide-core.js";

const MASK_ALPHA = 0.7;

/** 设计令牌里的“r, g, b”三元组与颜色值都认。 */
function tokenColor(style, name, alpha) {
    const v = style.getPropertyValue(name).trim();
    return /^[\d.]+\s*,\s*[\d.]+\s*,\s*[\d.]+$/.test(v) ? `rgba(${v}, ${alpha})` : v;
}

/** 圆角矩形路径：不用 roundRect，旧一点的系统 WebKit 没有它。 */
function roundedRect(g, x, y, w, h, r0) {
    const r = Math.min(r0, w / 2, h / 2);
    g.beginPath();
    g.moveTo(x + r, y);
    g.arcTo(x + w, y, x + w, y + h, r);
    g.arcTo(x + w, y + h, x, y + h, r);
    g.arcTo(x, y + h, x, y, r);
    g.arcTo(x, y, x + w, y, r);
    g.closePath();
}

function paintMask(canvas, hole, style) {
    const { w: W, h: H } = DESIGN.shell;
    const k = (window.devicePixelRatio || 1) * backingFitFactor();
    canvas.width = Math.round(W * k);
    canvas.height = Math.round(H * k);
    const g = canvas.getContext("2d");
    if (!g) return;
    g.setTransform(k, 0, 0, k, 0, 0);
    g.fillStyle = tokenColor(style, "--void", MASK_ALPHA);
    g.fillRect(0, 0, W, H);
    if (!hole) return;
    // 挖洞：destination-out 加阴影，洞的边缘就是羽化的
    g.globalCompositeOperation = "destination-out";
    g.fillStyle = "#000";
    g.shadowColor = "#000";
    // shadowBlur 不随画布变换缩放，羽化宽度要自己乘倍率，才和设计坐标里的宽度一致
    g.shadowBlur = GEOM.feather * k;
    roundedRect(g, hole.x, hole.y, hole.w, hole.h, GEOM.radius);
    g.fill();
    g.globalCompositeOperation = "source-over";
}

/**
 * @param {object} o
 * @param {object} o.shell 外壳（root、regions、t、getLang、onState）
 * @param {()=>boolean} o.isBusy 有别的模态或远距离模式在占着窗口时为真，此时不开
 * @param {()=>void} [o.onClose] 关闭时调用（看完、跳过、Esc 都算），用来记“指南已看”
 */
export function createGuide(o) {
    const { shell } = o;
    const { root, regions } = shell;
    const bgNodes = [regions.header, regions.left, regions.main, regions.footer];
    let s = null; // 打开期间的状态：{ ov, canvas, call, list, i, lang, back, onKey }

    function targetOf(step) {
        for (const sel of step.select) {
            const n = root.querySelector(sel);
            if (!n) continue;
            const r = n.getBoundingClientRect();
            if (r.width > 0 && r.height > 0) return n;
        }
        return null;
    }

    /** 箭头：贴在说明框朝向亮洞的那一边，沿边的位置对准亮洞中心。 */
    function arrow(pos, callH) {
        const a = el("i", { class: "guide-call__arrow", aria: { hidden: "true" }, data: { decor: "1" } });
        const along = (v, size) => `${Math.min(size - 22, Math.max(22, v)) - 6}px`;
        if (pos.side === "right") Object.assign(a.style, { left: "-6px", top: along(pos.cy - pos.y, callH) });
        else if (pos.side === "left") Object.assign(a.style, { right: "-6px", top: along(pos.cy - pos.y, callH) });
        else if (pos.side === "bottom")
            Object.assign(a.style, { top: "-6px", left: along(pos.cx - pos.x, GEOM.callW) });
        else Object.assign(a.style, { bottom: "-6px", left: along(pos.cx - pos.x, GEOM.callW) });
        return a;
    }

    // keepFocus：窗口大小或语言变了的重画，焦点留在原来的按钮上；翻页的重画才把焦点给说明框
    function render(keepFocus = false) {
        if (!s) return;
        const t = shell.t;
        const step = s.list[s.i];
        const had =
            keepFocus && s.call && s.call.contains(document.activeElement) ? document.activeElement.dataset.part : null;
        const n = targetOf(step);
        const hole = n ? holeFromRect(n.getBoundingClientRect(), root.getBoundingClientRect(), shellFitFactor()) : null;
        paintMask(s.canvas, hole, getComputedStyle(root));
        s.lang = shell.getLang();
        const last = s.i === s.list.length - 1;
        const btn = (cls, key, on, part) =>
            el("button", {
                type: "button",
                class: `sc-btn ${cls}`,
                text: t(key),
                data: { glow: "1", part },
                on: {
                    click: (e) => {
                        e.stopPropagation();
                        on();
                    },
                },
            });
        const call = el(
            "div",
            { class: "guide-call dk", tabindex: "-1", data: { part: "guide-callout", step: step.id } },
            el("h2", { class: "guide-call__title", id: "guide-title", text: t(`guide.${step.id}.t`) }),
            el("p", { class: "guide-call__body", id: "guide-body", text: t(`guide.${step.id}.b`) }),
            el("p", { class: "guide-call__hint", text: t("guide.hint") }),
            el(
                "div",
                { class: "guide-call__foot" },
                el(
                    "div",
                    { class: "guide-call__where" },
                    el("span", {
                        class: "guide-call__step",
                        text: t("guide.step", { n: s.i + 1, total: s.list.length }),
                    }),
                    el(
                        "span",
                        { class: "guide-call__dots", aria: { hidden: "true" } },
                        s.list.map((_, j) => el("i", { class: "guide-call__dot", data: { current: j === s.i } })),
                    ),
                ),
                el(
                    "div",
                    { class: "guide-call__acts" },
                    s.i > 0 ? btn("sc-btn--dark", "guide.prev", () => go(-1), "guide-prev") : null,
                    last ? null : btn("sc-btn--dark", "guide.skip", () => close(), "guide-skip"),
                    btn("sc-btn--cta", last ? "guide.done" : "guide.next", () => go(1), "guide-next"),
                ),
            ),
        );
        if (s.call) s.call.remove();
        call.style.visibility = "hidden";
        s.ov.append(call);
        s.call = call;
        const callH = call.offsetHeight;
        const pos = placeCallout(hole, callH);
        call.style.left = `${Math.round(pos.x)}px`;
        call.style.top = `${Math.round(pos.y)}px`;
        call.style.visibility = "";
        if (pos.side) call.append(arrow(pos, callH));
        const same = had ? call.querySelector(`[data-part="${had}"]`) : null;
        (same || call).focus({ preventScroll: true });
    }

    function go(dir) {
        if (!s) return;
        const j = turn(s.i, dir, s.list.length);
        if (j === null) close();
        else {
            s.i = j;
            render();
        }
    }

    function open() {
        if (s || o.isBusy()) return false;
        const list = presentSteps((step) => !!targetOf(step));
        if (list.length === 0) return false;
        const canvas = el("canvas", { class: "guide__mask", aria: { hidden: "true" } });
        const ov = el(
            "div",
            {
                class: "guide",
                role: "dialog",
                aria: { modal: "true", labelledby: "guide-title", describedby: "guide-body" },
                data: { part: "guide" },
                on: { click: () => go(1) },
            },
            canvas,
        );
        const onKey = (e) => {
            if (e.key === "Escape") {
                e.preventDefault();
                e.stopPropagation();
                close();
            } else if (e.key === "ArrowRight" || e.key === "ArrowLeft") {
                e.preventDefault();
                go(e.key === "ArrowRight" ? 1 : -1);
            } else if (e.key === "Tab" && s.call) {
                // Tab 只在说明框的按钮里循环
                const items = Array.from(s.call.querySelectorAll("button"));
                const i = items.indexOf(document.activeElement);
                const dir = e.shiftKey ? -1 : 1;
                e.preventDefault();
                if (items.length)
                    items[i < 0 ? (dir < 0 ? items.length - 1 : 0) : (i + dir + items.length) % items.length].focus();
            }
        };
        s = { ov, canvas, call: null, list, i: 0, lang: shell.getLang(), back: document.activeElement, onKey };
        for (const n of bgNodes) n.setAttribute("inert", "");
        regions.layers.tour.append(ov);
        document.addEventListener("keydown", onKey, true);
        render();
        return true;
    }

    function close() {
        if (!s) return;
        const { ov, onKey, back } = s;
        s = null;
        document.removeEventListener("keydown", onKey, true);
        ov.remove();
        for (const n of bgNodes) n.removeAttribute("inert");
        // 点按钮在 macOS 的 WebKit 里不给按钮焦点，打开时的焦点可能只是 body；那就还给“?”
        const usable = back && back !== document.body && back.isConnected;
        const target = usable ? back : root.querySelector('[data-part="help"]');
        if (target && typeof target.focus === "function") target.focus({ preventScroll: true });
        if (typeof o.onClose === "function") o.onClose();
    }

    // 换语言、改窗口大小时按当前页重画
    shell.onState(() => {
        if (s && shell.getLang() !== s.lang) render(true);
    });
    window.addEventListener("resize", () => {
        if (s) window.requestAnimationFrame(() => render(true));
    });

    return { open, close, isOpen: () => !!s };
}
