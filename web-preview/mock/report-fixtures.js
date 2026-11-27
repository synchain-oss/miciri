// SPDX-License-Identifier: GPL-3.0-or-later
//
// 报告页用的快照夹具：四步做完的快照，再按场景改 results、fixes、advice、previous。形状经 fields 核对，数值是占位。
// fixes 与 advice 是原生侧算出来的，这里按 BRIDGE 契约里写的决策表直接给，不在预览里重算。

import { STALE_SECTIONS, resultsFor, withResult } from "./results.js";
import { buildSnapshot, fields } from "./snapshot.js";
import { SNAPSHOT_ADVICE } from "../../web/shared/bridge-api.js";

const advice = (room = "unknown", distance = "none", directionPrompt = "none", voice = {}) =>
    fields(
        SNAPSHOT_ADVICE,
        { room, distance, directionPrompt, plosive: "none", sibilance: "none", click: "none", ...voice },
        "advice",
    );

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
    o = withSection(o, "directRatio", { drDb: 14.2, tone: "none" });
    return withSection(o, "distance", { closeEvidence: 0, farEvidence: 0, empiricalLimitM: 0, kind: "unknown" });
};

/** 场景：名字对应报告里的情形。 */
export const SCENES = {
    /** T20 很混：先换地方并靠近。 */
    veryBoomy: () =>
        measured({ fixes: ["room"], advice: advice("change-room-and-move-closer", "none", "card-button") }),
    /** T20 正常、底噪偏大、可能离远了：先靠近，方向检查放在主按钮。 */
    maybeTooFar: () => {
        let s = green(measured());
        s = withSection(s, "noise", {
            noiseDbfs: -48,
            noiseDbfsA: -52,
            snrDb: 24,
            noisePeakDbfs: -40,
            band: "bad",
            tone: "red",
        });
        s = withSection(s, "distance", { kind: "maybe-too-far", empiricalLimitM: 0.41 });
        return { ...s, fixes: ["noise"], advice: advice("good", "move-closer", "main-button") };
    },
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
    /** 有效带宽不够：齿音没法和元音比，卡上退回 3-6 kHz 的电平，说明写原因（最长的齿音说明）。 */
    sibNarrow: () => withSection(SCENES.ready(), "sibilance", { highBandDisabled: true, effectiveBandwidthHz: 8000 }),
    /** 重测位置后挪近了：D/R 与喷麦指向更近，齿音因为调过增益不比。 */
    closer: () => {
        const s = withSection(green(measured()), "distance", {
            closeEvidence: 2,
            farEvidence: 0,
            drDeltaDb: 4.1,
            plosiveDeltaDb: 7.5,
            sibilanceDeltaDb: 0,
            kind: "closer",
            drSign: "closer",
            plosiveSign: "closer",
            sibilanceSign: "gain-changed",
        });
        return { ...s, fixes: [], advice: advice("good", "none", "card-button") };
    },
    /** 重测位置后挪远了：三项都指向更远。 */
    farther: () => {
        const s = withSection(green(measured()), "distance", {
            closeEvidence: 0,
            farEvidence: 3,
            drDeltaDb: -3.6,
            plosiveDeltaDb: -8.2,
            sibilanceDeltaDb: -6,
            kind: "farther",
            drSign: "farther",
            plosiveSign: "farther",
            sibilanceSign: "farther",
        });
        return { ...s, fixes: [], advice: advice("good", "none", "card-button") };
    },
    /** 重测位置后没有明显变化。 */
    noChange: () => {
        const s = withSection(green(measured()), "distance", {
            drDeltaDb: 0.4,
            plosiveDeltaDb: -1.2,
            sibilanceDeltaDb: 0.8,
            kind: "no-change",
            drSign: "no-change",
            plosiveSign: "no-change",
            sibilanceSign: "no-change",
        });
        return { ...s, fixes: [], advice: advice("good", "none", "card-button") };
    },
    /** 几项读数指向不同的方向：说不准；方向提示是动作加次要按钮。 */
    conflicting: () => {
        const s = withSection(green(measured()), "distance", {
            closeEvidence: 1,
            farEvidence: 1,
            drDeltaDb: 3.8,
            plosiveDeltaDb: -7,
            sibilanceDeltaDb: 0.6,
            kind: "conflicting",
            drSign: "closer",
            plosiveSign: "farther",
            sibilanceSign: "no-change",
        });
        return { ...s, fixes: [], advice: advice("good", "none", "action-with-button") };
    },
    /** 喷麦、齿音、口水音都超过参考值：各自卡上有参考行与轻提示（口水音的参考值在这里设成每分钟 6 次）。 */
    overReference: () => {
        let s = withSection(green(measured()), "plosive", { worstRatioDb: 10.3, overReference: true });
        s = withSection(s, "sibilance", { highOverVowelDb: -1.5, overReference: true });
        s = withSection(s, "click", { perMinute: 9, count: 2, overReference: true });
        return {
            ...s,
            bands: { ...s.bands, clickReferencePerMin: 6 },
            advice: advice("good", "none", "card-button", {
                plosive: "pop-filter-or-back-off",
                sibilance: "angle-soften-de-ess",
                click: "back-off-a-little",
            }),
        };
    },
    /** 同上，耳机麦：没有推荐距离，也没有方向卡；带“退后一点”的提示只说不涉及距离的部分。 */
    overReferenceHeadset: () => ({
        ...SCENES.overReference(),
        micType: "headset",
        recommendedDistance: null,
    }),
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
