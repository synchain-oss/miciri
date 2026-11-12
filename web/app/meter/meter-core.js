// SPDX-License-Identifier: GPL-3.0-or-later
//
// 左栏输入卡的纯逻辑：刻度、弹道、信号状态药丸、电平管上的结果标记。
// 不碰 DOM，Node 里可以直接 import 做测试。所有数值来自快照，这里只有显示用的几何常数。
//
// 弹道：液面瞬时升到目标、按固定速度回落；峰值线保持一段时间后慢速回落。
// 音频线程只发电平，弹道在界面里算，空闲（电平停在底、没有新事件）时不需要任何定时器。

/** 刻度地板（dBFS）：左栏电平管线性刻度的最低点；远距离全宽条是另一套（−60..0）。 */
export const FLOOR = -80;
/** 远距离模式全宽电平条的刻度下限（dBFS，上限是 0）；两处显示共用这一个数。 */
export const BAR_FLOOR = -60;
/** 显示上限：真峰值可以略高于 0 dBFS（采样点之间的过冲），读数照实显示，液面画满即止。 */
export const CEIL = 6;
export const TICKS = [0, -6, -12, -24, -36, -48, -60, -80];
/** 液面回落速度（dB/s）、峰值保持时间（ms）、保持结束后的回落速度（dB/s）。 */
export const FALL_DB_S = 120;
export const PEAK_HOLD_MS = 2200;
export const PEAK_FALL_DB_S = 20;
/** 管内上下各留的边（px），与 CSS 里 .lt__slot 的内缩一致；标签间的最小间距（px）。 */
export const TUBE_PAD = 4;
export const LABEL_GAP = 14;

const clamp = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);

/** dB 在线性刻度上的位置，0（底）到 1（顶）。非数值当作底。 */
export function ratio(db, floor = FLOOR) {
    return Number.isFinite(db) ? clamp((db - floor) / -floor, 0, 1) : 0;
}

/** 一路电平的初始状态：全在底。 */
export const rest = () => ({ db: FLOOR, peakDb: FLOOR, heldMs: 0 });
export const atRest = (s) => s.db <= FLOOR && s.peakDb <= FLOOR;

/**
 * 推进一帧。targetDb 是最新的电平，targetPeakDb 是最新的峰值（缺省取电平）。
 * 纯函数，返回新状态。
 */
export function advance(prev, targetDb, targetPeakDb, dtMs) {
    const lvl = clamp(Number.isFinite(targetDb) ? targetDb : FLOOR, FLOOR, CEIL);
    const pk = clamp(Number.isFinite(targetPeakDb) ? targetPeakDb : lvl, FLOOR, CEIL);
    const dt = Math.max(0, dtMs) / 1000;
    const db = lvl >= prev.db ? lvl : Math.max(lvl, prev.db - FALL_DB_S * dt);
    let peakDb = prev.peakDb;
    let heldMs = prev.heldMs + dtMs;
    if (Math.max(pk, db) >= peakDb) {
        peakDb = Math.max(pk, db);
        heldMs = 0;
    } else if (heldMs > PEAK_HOLD_MS) {
        peakDb = Math.max(db, peakDb - PEAK_FALL_DB_S * dt);
    }
    return { db, peakDb, heldMs };
}

/** 数字格式：负号用 U+2212，一位小数。 */
export function fmtDb(v, digits = 1) {
    if (!Number.isFinite(v)) return fmtDb(FLOOR, digits);
    const s = Math.abs(v).toFixed(digits);
    return (v < 0 && Number(s) !== 0 ? "−" : "") + s;
}

/** 目标区在刻度上的位置，取自 bands（tpQuietDbfs 到 tpLoudDbfs）。没有 bands 时返回 null，不画。 */
export function targetZone(bands) {
    if (!bands || !Number.isFinite(bands.tpQuietDbfs) || !Number.isFinite(bands.tpLoudDbfs)) return null;
    return { lo: bands.tpQuietDbfs, hi: bands.tpLoudDbfs };
}

/**
 * 信号状态药丸。优先级：没收到音频或全零（红）> 信号极低（琥珀）> 等你出声（灰）> 声道提示（琥珀，可点）> 信号正常（绿）。
 * @returns {{tone: string, key: string, act: boolean, ch: "left"|"right"|null}}
 */
export function signalPill(state) {
    const c = (state && state.connect) || {};
    const stereo = state && state.device && state.device.layout === "stereo";
    switch (c.signalState) {
        case "no-callback":
            return { tone: "red", key: "sig.nocb", act: false, ch: null, pulse: false };
        case "all-zero-from-start":
        case "all-zero-after-signal":
            return { tone: "red", key: "sig.zero", act: false, ch: null, pulse: false };
        case "very-low-noise":
            return { tone: "amber", key: "sig.low", act: false, ch: null, pulse: false };
        case "ok":
            break;
        default:
            return { tone: "gray", key: "sig.idle", act: false, ch: null, pulse: false };
    }
    if (stereo) {
        const prompt = c.channelPrompt;
        // 哪一路有声音：被分析的那一路就是有声音（或较响）的那一路
        const ch = c.analysedChannel === "right" ? "right" : "left";
        if (prompt === "one-silent" || prompt === "one-silent-host" || prompt === "one-dominant")
            return { tone: "amber", key: "sig.one", act: true, ch, pulse: false };
        if (prompt === "antiphase") return { tone: "amber", key: "sig.anti", act: true, ch, pulse: false };
    }
    return { tone: "green", key: "sig.ok", act: false, ch: null, pulse: true };
}

const isNum = Number.isFinite;
const staleOf = (state, stepId, section) =>
    !!(section && section.stale) || (state.steps && state.steps[stepId] && state.steps[stepId].status === "stale");
const hasDone = (state, stepId) => {
    const s = state.steps && state.steps[stepId];
    return !!s && (s.status === "done" || s.status === "stale");
};

/**
 * 电平管上的结果标记。底噪和电流声是管内区间（从管底到均值，峰值只在悬停时画），口水音是管外的圆点。
 * 只在相应步骤有结果后出现；房间、齿音不上管。电流声检出就是红色，不跟卡片档位。
 * @returns {Array<{id: string, kind: "band"|"dot", mean: number, peak?: number, f?: number, tone: string, stale: boolean}>}
 */
export function markList(state) {
    const out = [];
    const r = (state && state.results) || {};
    if (hasDone(state, "quiet") && r.noise && isNum(r.noise.noiseDbfs)) {
        out.push({
            id: "noise",
            kind: "band",
            mean: r.noise.noiseDbfs,
            peak: isNum(r.noise.noisePeakDbfs) ? r.noise.noisePeakDbfs : r.noise.noiseDbfs,
            tone: r.noise.tone,
            stale: staleOf(state, "quiet", r.noise),
        });
        if (r.hum && r.hum.kind !== "none" && isNum(r.hum.levelDbfs)) {
            out.push({
                id: "hum",
                kind: "band",
                mean: r.hum.levelDbfs,
                peak: isNum(r.hum.peakDbfs) ? r.hum.peakDbfs : r.hum.levelDbfs,
                f: r.hum.fundamentalHz,
                tone: "red",
                stale: staleOf(state, "quiet", r.hum),
            });
        }
    }
    // 0 次时“最响一次”不是测量值（原生留 0），不画圆点
    if (hasDone(state, "position") && r.click && r.click.count > 0 && isNum(r.click.loudestDbfs)) {
        out.push({
            id: "mouth",
            kind: "dot",
            mean: r.click.loudestDbfs,
            tone: r.click.tone,
            stale: staleOf(state, "position", r.click),
        });
    }
    return out;
}

/**
 * 标签的纵向位置（px，从管底算）：按均值排序，相邻不足 LABEL_GAP 时向上错开。
 * @param {Array<{mean:number}>} marks
 * @param {number} tubeH 管内可用高度（px）
 * @returns {number[]} 与 marks 同序
 */
export function layoutLabels(marks, tubeH) {
    const ys = marks.map((m) => TUBE_PAD + ratio(m.mean) * (tubeH - 2 * TUBE_PAD));
    const order = ys.map((_, i) => i).sort((a, b) => ys[a] - ys[b]);
    for (let n = 1; n < order.length; n++) {
        const lo = order[n - 1];
        const hi = order[n];
        if (ys[hi] - ys[lo] < LABEL_GAP) ys[hi] = ys[lo] + LABEL_GAP;
    }
    return ys;
}

/** 区间画在被分析的那一路；单声道只有 L。 */
export function analysedLane(state) {
    const stereo = state && state.device && state.device.layout === "stereo";
    return stereo && state.connect && state.connect.analysedChannel === "right" ? "R" : "L";
}
