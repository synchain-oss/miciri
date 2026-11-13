// SPDX-License-Identifier: GPL-3.0-or-later
//
// 报告页用的快照夹具：四步做完的快照，再按场景改 results、fixes、advice、previous。形状经 fields 核对，数值是占位。
// fixes 与 advice 是原生侧算出来的，这里按 BRIDGE 契约里写的决策表直接给，不在预览里重算。

import { STALE_SECTIONS, resultsFor, withResult } from "./results.js";
import { buildSnapshot, fields } from "./snapshot.js";
import { SNAPSHOT_ADVICE } from "../../web/shared/bridge-api.js";

const advice = (room = "unknown", distance = "none", directionPrompt = "none") =>
    fields(SNAPSHOT_ADVICE, { room, distance, directionPrompt }, "advice");

/** 四步做完（方向没测）的快照。 */
export function measured(patch = {}) {
    const s = buildSnapshot();
    for (const id of ["gain", "quiet", "room", "position"]) {
        s.steps[id].status = "done";
        Object.assign(s.results, resultsFor(id));
    }
    return { ...s, ...patch };
}

/** 改某个区段的字段，返回新快照（不改原对象）。 */
export function withSection(snap, name, patch) {
    return {
        ...snap,
        results: { ...snap.results, [name]: withResult(name, { ...(snap.results[name] || {}), ...patch }) },
    };
}

/** 按过期规则标过期（与假桥的 reportChange 一致），返回新快照。 */
export function staled(snap, kind) {
    const out = structuredClone(snap);
    for (const name of STALE_SECTIONS[kind]) if (out.results[name]) out.results[name].stale = true;
    for (const id of ["gain", "quiet", "room", "position", "direction"]) {
        const secs = {
            gain: ["gain"],
            quiet: ["noise", "hum"],
            room: ["reverb"],
            position: ["directRatio", "plosive", "sibilance", "click", "distance"],
            direction: ["direction"],
        }[id];
        if (out.steps[id].status === "done" && secs.some((n) => out.results[n] && out.results[n].stale))
            out.steps[id].status = "stale";
    }
    return out;
}

/** 做完的各步都有证据（假桥的 getEvidence 看 evidenceRev），并且各做完过 runs 次。 */
export function withEvidence(snap, runs = 1) {
    const out = structuredClone(snap);
    for (const id of ["gain", "quiet", "room", "position", "direction"]) {
        if (out.steps[id].status === "idle") continue;
        out.steps[id].evidenceRev = runs;
        out.steps[id].runs = runs;
    }
    return out;
}

const green = (s) => {
    let o = withSection(s, "noise", {
        noiseDbfs: -62,
        noiseDbfsA: -67,
        snrDb: 47.6,
        noisePeakDbfs: -52,
        band: "good",
        tone: "green",
    });
    o = withSection(o, "hum", { kind: "none", slight: false, overFloorDb: 0, energySharePct: 0, tone: "green" });
    o = withSection(o, "reverb", { t20Sec: [0.4, 0.38, 0.35, 0.3], tMidSec: 0.38, band: "normal", tone: "green" });
    o = withSection(o, "directRatio", { drDb: 14.2, band: "high", tone: "green" });
    return withSection(o, "distance", { closeEvidence: 0, farEvidence: 0, kind: "fine", tone: "none" });
};

/** 场景：名字对应报告里的情形。 */
export const SCENES = {
    /** T20 长 + D/R 低：先换地方并靠近。 */
    t20longDrlow: () =>
        measured({ fixes: ["room"], advice: advice("change-room-and-move-closer", "none", "card-button") }),
    /** T20 正常 + D/R 低：靠近，或方向放反；方向检查放在主按钮。 */
    t20okDrlow: () =>
        measured({
            results: withSection(measured(), "reverb", {
                t20Sec: [0.4, 0.38, 0.35, 0.3],
                tMidSec: 0.38,
                band: "normal",
                tone: "green",
            }).results,
            fixes: ["room"],
            advice: advice("move-closer-check-direction", "none", "main-button"),
        }),
    /** 都好，各步有证据：详情里画证据画布。 */
    evidence: () => withEvidence(SCENES.ready()),
    /** 增益与底噪重测过：增益有上次，底噪与电流声是首次测量。 */
    retestedQuiet: () => {
        const s = withEvidence(SCENES.ready());
        s.steps.gain.runs = 2;
        s.steps.quiet.runs = 2;
        s.previous = { gain: structuredClone(s.results.gain) };
        return s;
    },
    /** 都好：可以开始录音。 */
    ready: () => ({ ...green(measured()), advice: advice("good", "none", "card-button") }),
    /** 离话筒太近：喷麦重。 */
    tooClose: () => {
        let s = green(measured());
        s = withSection(s, "plosive", { worstRatioDb: 10.3, severity: "heavy", tone: "red" });
        s = withSection(s, "distance", { closeEvidence: 1, farEvidence: 0, kind: "too-close", tone: "none" });
        return { ...s, fixes: ["distance"], advice: advice("good", "back-off-a-little", "card-button") };
    },
    /** 证据矛盾：又像太近又像太远；方向提示是动作加次要按钮。 */
    conflict: () => {
        let s = withSection(measured(), "plosive", { worstRatioDb: 6.2, severity: "moderate", tone: "amber" });
        s = withSection(s, "distance", { closeEvidence: 1, farEvidence: 1, kind: "conflicting", tone: "none" });
        return {
            ...s,
            fixes: ["distance"],
            advice: advice("change-room-and-move-closer", "fix-room-or-direction-first", "action-with-button"),
        };
    },
    /** 房间要换且方向提示是动作加次要按钮：顶部卡的动作最多。 */
    room1Dir: () =>
        measured({ fixes: ["room"], advice: advice("change-room-and-move-closer", "none", "action-with-button") }),
    /** 有读数要再测：噪声和房间。 */
    lowConfidence: () => {
        let s = withSection(measured(), "noise", {
            reliability: "retest",
            retestReason: "voice-detected",
            tone: "none",
        });
        s = withSection(s, "reverb", { reliability: "retest", retestReason: "low-pnr", tone: "none" });
        return { ...s, fixes: [] };
    },
    /** 调过增益：增益、底噪、电流声、齿音过期。 */
    staleGain: () => ({ ...staled(green(measured()), "gain"), fixes: [] }),
    /** 挪过位置。 */
    stalePosition: () => ({ ...staled(green(measured()), "position"), fixes: [] }),
    /** 换过房间：全部过期。 */
    staleRoom: () => ({ ...staled(green(measured()), "room"), fixes: [] }),
};
