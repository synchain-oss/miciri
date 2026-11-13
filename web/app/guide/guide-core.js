// SPDX-License-Identifier: GPL-3.0-or-later
//
// 应用内指南的纯逻辑：七个步骤和它们的目标、亮洞的几何、说明框的避让、翻页。
// 不碰 DOM，Node 里可以直接 import；页面部分在 guide-ui.js。

import { DESIGN } from "../../shared/design-box.js";

/**
 * 七个步骤，次序固定。select 是目标的候选选择器，第一个能找到且有大小的算数；
 * 找不到目标的步骤（比如当前页没有那块东西）整步跳过，不报错。
 */
export const STEPS = [
    { id: "input", select: ['[data-part="input-card"]'] },
    { id: "meter", select: ['[data-part="meter"]'] },
    { id: "mic", select: ['[data-part="mic-button"]'] },
    { id: "main", select: ['[data-part="card"] .pn__viz', '[data-part="card"]'] },
    { id: "cta", select: ['[data-part="cta"]'] },
    { id: "scale", select: ['[data-part="scale-button"]'] },
    { id: "help", select: ['[data-part="help"]'] },
];

/** 几何常量（设计 px）：亮洞外扩、圆角、羽化，说明框的宽、离亮洞的间距、离窗口边的最小距离。 */
export const GEOM = { pad: 10, radius: 16, feather: 12, callW: 360, gap: 14, edge: 13 };

export const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));

/**
 * 把目标的屏幕矩形换成设计坐标里的亮洞。
 * rect 与 shellRect 是 getBoundingClientRect 的结果，zoom 是外壳当前的缩放；外壳起点是设计坐标的 (0,0)。
 */
export function holeFromRect(rect, shellRect, zoom, geom = GEOM) {
    const { w: W, h: H } = DESIGN.shell;
    const z = zoom > 0 ? zoom : 1;
    const x = clamp((rect.left - shellRect.left) / z - geom.pad, 0, W);
    const y = clamp((rect.top - shellRect.top) / z - geom.pad, 0, H);
    return {
        x,
        y,
        w: Math.max(0, Math.min(W - x, rect.width / z + 2 * geom.pad)),
        h: Math.max(0, Math.min(H - y, rect.height / z + 2 * geom.pad)),
    };
}

/**
 * 说明框的位置：依次试右、左、下、上，取第一个不压住亮洞的；都压就取压得最少的。
 * 没有亮洞时居中。返回左上角、所在方向（箭头朝向）和亮洞中心（算箭头位置用）。
 */
export function placeCallout(hole, callH, geom = GEOM) {
    const { w: W, h: H } = DESIGN.shell;
    const cw = geom.callW;
    if (!hole) return { side: null, x: (W - cw) / 2, y: (H - callH) / 2, cx: W / 2, cy: H / 2, overlap: 0 };
    const cx = hole.x + hole.w / 2;
    const cy = hole.y + hole.h / 2;
    const cand = [
        ["right", hole.x + hole.w + geom.gap, cy - callH / 2],
        ["left", hole.x - geom.gap - cw, cy - callH / 2],
        ["bottom", cx - cw / 2, hole.y + hole.h + geom.gap],
        ["top", cx - cw / 2, hole.y - geom.gap - callH],
    ];
    let best = null;
    for (const [side, x0, y0] of cand) {
        const x = clamp(x0, geom.edge, W - cw - geom.edge);
        const y = clamp(y0, geom.edge, H - callH - geom.edge);
        const ox = Math.max(0, Math.min(x + cw, hole.x + hole.w) - Math.max(x, hole.x));
        const oy = Math.max(0, Math.min(y + callH, hole.y + hole.h) - Math.max(y, hole.y));
        const overlap = ox * oy;
        if (!best || overlap < best.overlap) best = { side, x, y, cx, cy, overlap };
        if (overlap === 0) break;
    }
    return best;
}

/** 目标在场的步骤（按次序），用 has(step) 判断；返回步骤对象的数组。 */
export function presentSteps(has, steps = STEPS) {
    return steps.filter((s) => has(s));
}

/** 翻页：从第 i 页走 dir（+1 或 -1）。越过最后一页返回 null（结束），越过第一页停在第一页。 */
export function turn(i, dir, total) {
    const j = i + dir;
    if (j >= total) return null;
    return Math.max(0, j);
}
