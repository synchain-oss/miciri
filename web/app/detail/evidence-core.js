// SPDX-License-Identifier: GPL-3.0-or-later
//
// 证据画布的纯逻辑：哪张卡画什么、回执载荷的核对与归一、缩放与平移的数学、时间刻度的小数位、
// 每个像素列的包络极值、事件标记的位置与压暗。不碰 DOM 与画布，Node 里可以直接 import 做测试。
//
// 视图用 {z, c} 表示：z 是缩放倍数（1 到 ZOOM_MAX），c 是可见范围的中心，都按整段时长归一到 0..1。
// 可见范围是 [c - 0.5/z, c + 0.5/z]，c 总被夹在让范围不越界的区间里。

import { EVIDENCE_EVENT_KINDS } from "../../shared/bridge-api.js";

export const ZOOM_MAX = 16;
/** 每一档的倍率；Ctrl+滚轮每累计 WHEEL_UNIT 个滚轮单位走一档。 */
export const ZOOM_STEP = 1.25;
export const WHEEL_UNIT = 100;
/** 滚轮平移：每 WHEEL_UNIT 个单位移动可见范围的这个比例；键盘每按一下移动的比例。 */
export const PAN_WHEEL = 0.12;
export const PAN_KEY = 0.08;
/** 两次滚轮事件间隔超过这个时间（ms），累计清零。 */
const WHEEL_IDLE_MS = 250;
/** 滚轮的 deltaMode 为行、页时折成像素的倍数。 */
const LINE_UNITS = 33;
const PAGE_UNITS = 100;
/** 时间轴的刻度数（含两端）与小数位上限；标出来的数与真实位置的误差不超过刻度间距的这个比例。 */
export const TICKS = 5;
const MAX_DECIMALS = 3;
const TICK_TOLERANCE = 0.02;
/** 包络的整数单位：千分之一满刻度。 */
const FULL_SCALE = 1000;

export const EVENT_KINDS = EVIDENCE_EVENT_KINDS;

/**
 * 哪些卡画证据：取哪一步的证据，画哪些种类的事件，哪一类是“当前类”（别的类压暗）。
 * 增益只有包络，没有事件；喷麦、齿音、口水音三张卡取位置步，三种事件都画，只亮自己那一类。
 * 噪声、电流声、房间、D/R、方向的证据（频谱、衰减曲线）不在桥的载荷里，只给表格。
 */
export const EVIDENCE_OF = {
    level: { step: "gain", kinds: [], focus: null },
    plosive: { step: "position", kinds: EVENT_KINDS, focus: "plosive" },
    sib: { step: "position", kinds: EVENT_KINDS, focus: "sibilance" },
    mouth: { step: "position", kinds: EVENT_KINDS, focus: "mouth" },
};

/** 这张卡的证据配置；这一步没有证据（evidenceRev 为 0）或这张卡不画时是 null。 */
export function evidenceSpec(id, state) {
    const spec = EVIDENCE_OF[id];
    const step = spec && state && state.steps && state.steps[spec.step];
    const rev = step && Number.isInteger(step.evidenceRev) ? step.evidenceRev : 0;
    return spec && rev > 0 ? { ...spec, rev } : null;
}

const fin = Number.isFinite;
const clamp = (x, lo, hi) => Math.min(hi, Math.max(lo, x));
const unit = (x) => (fin(x) ? clamp(x / FULL_SCALE, -1, 1) : 0);

/**
 * 核对并归一回执里的 evidence：形状不对（时长、格宽非正，最小最大不等长或为空）返回 null；
 * 包络转成 -1..1 的 Float32Array，非有限的格按 0；事件只留已知种类且时间有限的。
 */
export function normalize(pack) {
    const env = pack && pack.envelope;
    if (!env || !fin(pack.durationSec) || pack.durationSec <= 0) return null;
    if (!fin(env.hopSec) || env.hopSec <= 0 || !Array.isArray(env.min) || !Array.isArray(env.max)) return null;
    if (env.min.length === 0 || env.min.length !== env.max.length) return null;
    const events = (Array.isArray(pack.events) ? pack.events : [])
        .filter((e) => e && EVENT_KINDS.includes(e.kind) && fin(e.timeSec) && e.timeSec >= 0)
        .map((e) => ({ kind: e.kind, timeSec: e.timeSec, durSec: fin(e.durSec) ? Math.max(0, e.durSec) : 0 }));
    return {
        rev: pack.rev,
        durationSec: pack.durationSec,
        hopSec: env.hopSec,
        min: Float32Array.from(env.min, unit),
        max: Float32Array.from(env.max, unit),
        events,
    };
}

// ---------------------------------------------------------------------------------------------------------------------
// 视图：缩放与平移

export const FULL_VIEW = Object.freeze({ z: 1, c: 0.5 });

export function clampView(v) {
    let z = clamp(fin(v.z) ? v.z : 1, 1, ZOOM_MAX);
    if (z < 1 + 1e-9) z = 1;
    const half = 0.5 / z;
    return { z, c: clamp(fin(v.c) ? v.c : 0.5, half, 1 - half) };
}

export const rangeOf = (v) => [v.c - 0.5 / v.z, v.c + 0.5 / v.z];

/** 缩放 n 档（正为放大），p 是光标在可见范围里的位置（0..1），缩放后光标下的时间点不动。 */
export function zoomAt(v, n, p = 0.5) {
    const at = rangeOf(v)[0] + p / v.z;
    const z = clamp(v.z * ZOOM_STEP ** n, 1, ZOOM_MAX);
    return clampView({ z, c: at - (p - 0.5) / z });
}

/** 平移 f 个可见范围宽度（正为向后）。 */
export const panBy = (v, f) => clampView({ z: v.z, c: v.c + f / v.z });

/** 缩放读数：两位小数，跟随浮点误差不抖（1.00、1.25、1.56……16.00）。 */
export const zoomText = (v) => v.z.toFixed(2);

/**
 * Ctrl+滚轮的累计器：每累计 WHEEL_UNIT 个单位走一档，鼠标一格（约 100）正好一档，触摸板的小增量攒够才走。
 * feed 返回这次事件走了几档（正为放大：滚轮向上，deltaY 为负）。
 */
export function createWheel() {
    let acc = 0;
    let last = -Infinity;
    return {
        feed(delta, mode, now) {
            if (!fin(delta)) return 0;
            if (now - last > WHEEL_IDLE_MS) acc = 0;
            last = now;
            acc += mode === 1 ? delta * LINE_UNITS : mode === 2 ? delta * PAGE_UNITS : delta;
            let n = 0;
            while (Math.abs(acc) >= WHEEL_UNIT) {
                n += acc < 0 ? 1 : -1;
                acc -= Math.sign(acc) * WHEEL_UNIT;
            }
            return n;
        },
        reset() {
            acc = 0;
            last = -Infinity;
        },
    };
}

/** 键盘：返回新视图，按键不认识返回 null（交给浏览器）。 */
export function keyView(v, key) {
    if (key === "+" || key === "=") return zoomAt(v, 1);
    if (key === "-" || key === "_") return zoomAt(v, -1);
    if (key === "ArrowLeft") return panBy(v, -PAN_KEY);
    if (key === "ArrowRight") return panBy(v, PAN_KEY);
    if (key === "0") return { ...FULL_VIEW };
    return null;
}

// ---------------------------------------------------------------------------------------------------------------------
// 时间刻度

/**
 * TICKS 个刻度（含两端）的标签。小数位取最少的、让标出来的数与真实位置的误差不超过刻度间距的 2%，
 * 所以放大后小数位增加，相邻两个标签不会写成同一个数，标签与刻度位置对得上。
 * @returns {{values: number[], decimals: number, labels: string[]}}
 */
export function timeTicks(durationSec, [a, b]) {
    const n = TICKS - 1;
    const step = ((b - a) * durationSec) / n;
    const values = Array.from({ length: TICKS }, (_, i) => (a + ((b - a) * i) / n) * durationSec);
    let decimals = 0;
    while (
        decimals < MAX_DECIMALS &&
        values.some((v) => Math.abs(Number(v.toFixed(decimals)) - v) > step * TICK_TOLERANCE)
    )
        decimals++;
    return { values, decimals, labels: values.map((v) => v.toFixed(decimals)) };
}

// ---------------------------------------------------------------------------------------------------------------------
// 绘制用的数据

/**
 * 每个像素列的包络极值：列 x 覆盖可见范围内的一段时间，取这段里所有格的最小与最大。
 * 超出数据末端的列没有数据（has 为 0）。放大到一格比一个像素还宽时，相邻几列取同一格。
 */
export function columns(data, [a, b], width) {
    const lo = new Float32Array(width);
    const hi = new Float32Array(width);
    const has = new Uint8Array(width);
    const cells = data.min.length;
    const perUnit = data.durationSec / data.hopSec;
    for (let x = 0; x < width; x++) {
        const i0 = Math.floor((a + ((b - a) * x) / width) * perUnit);
        const i1 = Math.max(i0 + 1, Math.ceil((a + ((b - a) * (x + 1)) / width) * perUnit));
        if (i0 >= cells || i1 <= 0) continue;
        let mn = 1;
        let mx = -1;
        for (let i = Math.max(0, i0); i < Math.min(cells, i1); i++) {
            if (data.min[i] < mn) mn = data.min[i];
            if (data.max[i] > mx) mx = data.max[i];
        }
        lo[x] = mn;
        hi[x] = mx;
        has[x] = 1;
    }
    return { lo, hi, has };
}

/** 可见范围内的事件标记：x 是画布横坐标，dim 表示不是当前类（压暗）。 */
export function markers(data, kinds, focus, [a, b], width) {
    const out = [];
    for (const e of data.events) {
        if (!kinds.includes(e.kind)) continue;
        const u = e.timeSec / data.durationSec;
        if (u < a || u > b) continue;
        out.push({ kind: e.kind, x: ((u - a) / (b - a)) * width, dim: focus !== null && e.kind !== focus });
    }
    return out;
}
