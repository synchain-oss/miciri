// SPDX-License-Identifier: GPL-3.0-or-later
//
// 证据画布：一块 2D canvas，画被分析那一路的包络和三种事件标记（喷麦菱形、齿音三角、口水音圆），
// 上方是图例、缩放读数与复位按钮，下方是时间刻度和操作提示。只用 2D，不用 3D 与滤镜；颜色全部取自样式表的令牌。
//
// 数据由调用方给（setData），画布不读桥，也不自己拉取。缩放：Ctrl+滚轮以光标为中心，普通滚轮平移（没放大时让给面板滚动），
// 键盘 + − ← → 0，复位按钮。图例、读数、提示、时间刻度都是 DOM 文字；canvas 本身是 role=img，带标签，可聚焦。
// 返回的节点在面板重画时被原样搬进新结构，所以缩放状态不会因快照更新而丢。

import { clear, el, svg } from "../../shared/dom.js";
import {
    FULL_VIEW,
    PAN_WHEEL,
    TICKS,
    WHEEL_UNIT,
    clampView,
    columns,
    createWheel,
    keyView,
    markers,
    panBy,
    rangeOf,
    timeTicks,
    zoomAt,
    zoomText,
} from "./evidence-core.js";

/** 画布排版（设计 px）：顶部一条放事件标记，包络画在下面；没量出尺寸时的回退宽高。 */
const MARK_BAND = 26;
const BOTTOM_PAD = 4;
const MARK_Y = 13;
const MARK_R = 6;
const FALLBACK_W = 576;
const FALLBACK_H = 160;
/** 非当前类事件的透明度。 */
const DIM_ALPHA = 0.38;
/** 包络内芯相对外柱的高度比例。 */
const CORE_RATIO = 0.45;

/** 事件种类到词条键与图例形状。键写成字面量，方便词条扫描核对。 */
const LEGEND = {
    plosive: { key: "flow.card.plosive", d: "M9 2.8l6.2 6.2L9 15.2 2.8 9z" },
    sibilance: { key: "flow.card.sib", d: "M9 3l6.4 11H2.6z" },
    mouth: { key: "flow.card.mouth", d: "M9 3.4a5.6 5.6 0 1 0 0 11.2A5.6 5.6 0 0 0 9 3.4z" },
};

function glyph(kind) {
    return svg(
        "svg",
        { viewBox: "0 0 18 18", width: 12, height: 12, "aria-hidden": "true", focusable: "false" },
        svg("path", { d: LEGEND[kind].d }),
    );
}

/** 画一个事件标记：喷麦菱形、齿音三角、口水音圆（形状和图例一致）。 */
function drawMark(g, kind, x) {
    g.beginPath();
    if (kind === "plosive") {
        g.moveTo(x, MARK_Y - MARK_R - 1);
        g.lineTo(x + MARK_R, MARK_Y);
        g.lineTo(x, MARK_Y + MARK_R + 1);
        g.lineTo(x - MARK_R, MARK_Y);
        g.closePath();
    } else if (kind === "sibilance") {
        g.moveTo(x, MARK_Y - MARK_R - 1);
        g.lineTo(x + MARK_R + 0.5, MARK_Y + MARK_R);
        g.lineTo(x - MARK_R - 0.5, MARK_Y + MARK_R);
        g.closePath();
    } else g.arc(x, MARK_Y, MARK_R, 0, Math.PI * 2);
    g.fill();
}

/** 把 rgba(...) 的透明度去掉，标记用实色，压暗另走 globalAlpha。 */
const opaque = (c) => c.replace(/rgba\(\s*([^,]+),\s*([^,]+),\s*([^,)]+)[^)]*\)/, "rgb($1, $2, $3)");

/**
 * @param {{kinds: string[], focus: string|null}} o kinds 是图例与画出的事件种类，focus 是当前类（别的类压暗）
 * @returns {{el: HTMLElement, setData(data): void, refresh(t): void, destroy(): void}}
 */
export function createEvidence(o) {
    const { kinds, focus } = o;
    let t = (k) => k;
    let data = null;
    let view = { ...FULL_VIEW };
    const wheel = createWheel();

    const cv = el("canvas", { role: "img", tabindex: "0", class: "dt__canvas", data: { part: "evidence-canvas" } });
    const title = el("span", { class: "sc-eyebrow" });
    const names = kinds.map((k) => el("span"));
    const shapes = kinds.map(glyph);
    const legend = kinds.length
        ? el(
              "div",
              { class: "dt__leg", data: { part: "evidence-legend" } },
              kinds.map((k, i) => el("span", { data: { k, cur: k === focus } }, shapes[i], names[i])),
          )
        : el("div", { class: "dt__leg" });
    const zoom = el("span", {
        class: "dt__zoom",
        role: "status",
        aria: { live: "polite" },
        data: { part: "evidence-zoom" },
    });
    const reset = el("button", {
        type: "button",
        class: "sc-btn sc-btn--dark dt__reset",
        data: { glow: "1", part: "evidence-reset" },
        on: { click: () => set({ ...FULL_VIEW }) },
    });
    const axis = el("div", { class: "dt__ax", aria: { hidden: "true" } });
    const hint = el("p", { class: "dt__hint" });
    const root = el(
        "div",
        { class: "dt__ev", data: { part: "evidence" } },
        el("div", { class: "dt__evh" }, title, legend, zoom, reset),
        el("div", { class: "dt__cv" }, cv),
        axis,
        hint,
    );

    function palette() {
        const s = getComputedStyle(cv);
        const v = (n) => s.getPropertyValue(n).trim();
        return {
            env: v("--wave-env"),
            core: v("--wave-env-core"),
            grid: v("--dark-grid"),
            axis: v("--dark-grid-axis"),
            kind: {
                plosive: `rgb(${v("--sem-amber")})`,
                sibilance: opaque(v("--wave-env-pink")),
                mouth: `rgb(${v("--wave-vad")})`,
            },
        };
    }

    function label() {
        title.textContent = t("detail.ev.title");
        kinds.forEach((k, i) => (names[i].textContent = t(LEGEND[k].key)));
        // 图例的形状与画布上的标记同色（令牌里的粉色带透明度，这里取实色）
        const pal = palette();
        kinds.forEach((k, i) => pal.kind[k] && (shapes[i].style.color = pal.kind[k]));
        reset.textContent = t("detail.ev.reset");
        hint.textContent = t("detail.ev.hint");
        cv.setAttribute(
            "aria-label",
            data ? t(kinds.length ? "detail.ev.aria.ev" : "detail.ev.aria", { d: data.durationSec.toFixed(1) }) : "",
        );
    }

    function draw() {
        if (!data) return;
        view = clampView(view);
        const w = cv.clientWidth || FALLBACK_W;
        const h = cv.clientHeight || FALLBACK_H;
        const box = cv.getBoundingClientRect();
        // 外壳可能整体缩放：画布的像素密度跟着屏幕上的实际大小走
        const k = (window.devicePixelRatio || 1) * (cv.clientWidth && box.width ? box.width / cv.clientWidth : 1);
        cv.width = Math.round(w * k);
        cv.height = Math.round(h * k);
        const g = cv.getContext("2d");
        g.setTransform(k, 0, 0, k, 0, 0);
        g.clearRect(0, 0, w, h);

        const range = rangeOf(view);
        const pal = palette();
        const bottom = h - BOTTOM_PAD;
        const mid = (MARK_BAND + bottom) / 2;
        const half = (bottom - MARK_BAND) / 2;
        const ticks = timeTicks(data.durationSec, range);

        // 刻度竖线：与下面的时间标签一一对应，两端贴边
        g.lineWidth = 1;
        g.strokeStyle = pal.grid;
        ticks.values.forEach((_, i) => {
            const x = Math.min(w - 0.5, Math.max(0.5, (i / (TICKS - 1)) * w));
            g.beginPath();
            g.moveTo(x, 0);
            g.lineTo(x, h);
            g.stroke();
        });
        g.strokeStyle = pal.axis;
        g.beginPath();
        g.moveTo(0, mid + 0.5);
        g.lineTo(w, mid + 0.5);
        g.stroke();

        const cols = Math.max(1, Math.floor(w));
        const c = columns(data, range, cols);
        for (let x = 0; x < cols; x++) {
            if (!c.has[x]) continue;
            const top = mid - c.hi[x] * half;
            const len = (c.hi[x] - c.lo[x]) * half + 1;
            g.fillStyle = pal.env;
            g.fillRect(x, top, 1, len);
            g.fillStyle = pal.core;
            g.fillRect(x, mid - c.hi[x] * half * CORE_RATIO, 1, (c.hi[x] - c.lo[x]) * half * CORE_RATIO + 1);
        }

        for (const m of markers(data, kinds, focus, range, w)) {
            g.globalAlpha = m.dim ? DIM_ALPHA : 1;
            g.fillStyle = pal.kind[m.kind];
            g.strokeStyle = pal.kind[m.kind];
            g.lineWidth = 1;
            g.setLineDash([2, 3]);
            g.beginPath();
            g.moveTo(m.x + 0.5, MARK_Y + MARK_R + 1);
            g.lineTo(m.x + 0.5, mid);
            g.stroke();
            g.setLineDash([]);
            drawMark(g, m.kind, Math.min(w - MARK_R - 1, Math.max(MARK_R + 1, m.x))); // 贴边的标记整个画出来，虚线仍在真实位置
        }
        g.globalAlpha = 1;

        clear(axis);
        ticks.labels.forEach((s, i) =>
            axis.append(el("span", { text: `${s} s`, style: { left: `${(i * 100) / (TICKS - 1)}%` } })),
        );
        zoom.textContent = t("detail.ev.zoom", { z: zoomText(view) });
        reset.setAttribute("aria-disabled", view.z <= 1 ? "true" : "false");
    }

    function set(next) {
        view = clampView(next);
        draw();
    }

    cv.addEventListener(
        "wheel",
        (e) => {
            if (!data) return;
            if (e.ctrlKey) {
                e.preventDefault();
                const box = cv.getBoundingClientRect();
                const p = box.width ? Math.min(1, Math.max(0, (e.clientX - box.left) / box.width)) : 0.5;
                const n = wheel.feed(e.deltaY, e.deltaMode, performance.now());
                if (n) set(zoomAt(view, n, p));
            } else if (view.z > 1) {
                // 没放大时没有可平移的，把滚轮让给面板滚动
                e.preventDefault();
                set(panBy(view, ((e.deltaX || e.deltaY) / WHEEL_UNIT) * PAN_WHEEL));
            }
        },
        { passive: false },
    );
    cv.addEventListener("keydown", (e) => {
        if (!data || e.ctrlKey || e.metaKey || e.altKey) return;
        const next = keyView(view, e.key);
        if (!next) return;
        e.preventDefault();
        set(next);
    });
    const watch = typeof ResizeObserver === "function" ? new ResizeObserver(() => draw()) : null;
    if (watch) watch.observe(cv);

    return {
        el: root,
        /** 新数据（或 null）：视图回到整段。 */
        setData(next) {
            data = next;
            view = { ...FULL_VIEW };
            wheel.reset();
            label();
            draw();
        },
        /** 换语言或面板重画后：文字按新语言写，画布按当前尺寸重画。 */
        refresh(nextT) {
            t = nextT;
            label();
            draw();
        },
        destroy() {
            if (watch) watch.disconnect();
        },
    };
}
