// SPDX-License-Identifier: GPL-3.0-or-later
//
// 报告页的纯逻辑：最该改的一件事、距离标尺、九张结果卡、方向提示放在哪。不碰 DOM，Node 里可以直接 import 做测试。
// 页面全部由快照推出：排序在原生侧（fixes、advice），这里只把名字翻成词条键和动作，不重新排序、不写阈值。
//
// 取舍顺序（从上到下第一个命中的就是顶部那张卡）：
//   1. 有步骤过期：灰，写哪些过期、主按钮重测它们（过期的结果本来就不进 fixes）；
//   2. fixes 的第一项是“没信号”或“数字削波”：照 fixes 来（这两项不看置信度，也不看有没有别的要再测）；
//   3. 有读数要再测：灰，先重测它们（要再测的结果没有结论，也不进 fixes）；
//   4. fixes 有第一项：照 fixes 与 advice 来；
//   5. fixes 为空：近距离压住了房间声（advice.room）是琥珀的“别退后”，四步都做过是绿的“可以录”，否则灰的“还没测完”。

import { resultCards, STEP_IDS, visibleSteps } from "../flow/flow-core.js";

/** 九张卡的固定顺序，以及每张卡对应的结果区段与步骤。 */
export const CARD_ORDER = ["level", "noise", "hum", "room", "dr", "plosive", "sib", "mouth", "dir"];
const STEP_OF = {
    level: "gain",
    noise: "quiet",
    hum: "quiet",
    room: "room",
    dr: "position",
    plosive: "position",
    sib: "position",
    mouth: "position",
    dir: "direction",
};
/** 每一步产出的区段（距离只作标尺的证据，不在这里：它的“再测”不催用户重测）。 */
const SECTIONS_OF = {
    gain: ["gain"],
    quiet: ["noise", "hum"],
    room: ["reverb"],
    position: ["directRatio", "plosive", "sibilance", "click"],
};
/** 重测按钮能带上的步骤，按先后；方向是可选的，不在“重测这几项”里。 */
const RETEST_STEPS = ["gain", "quiet", "room", "position"];
export const CHANGE_KINDS = ["gain", "position", "room"];

const statusOf = (state, id) => (state.steps && state.steps[id] && state.steps[id].status) || "idle";
const results = (state) => state.results || {};
const inOrder = (steps) => RETEST_STEPS.filter((id) => steps.includes(id));

/** 一张卡。区段缺失时是“未测”的灰卡；上次（previous）也用同一个函数画。 */
export function cardOf(id, state, from = results(state)) {
    const r = from;
    const built = {
        level: () => r.gain && resultCards.level(state, r.gain),
        noise: () => r.noise && resultCards.noise(r.noise),
        hum: () => {
            if (!r.noise && !r.hum) return null;
            const c = resultCards.hum(r.hum);
            return r.hum ? c : { ...c, stale: !!r.noise.stale };
        },
        room: () => r.reverb && resultCards.room(state, r.reverb),
        dr: () => r.directRatio && resultCards.dr(r.directRatio),
        plosive: () => r.plosive && resultCards.plosive(r.plosive),
        sib: () => r.sibilance && resultCards.sib(r.sibilance),
        mouth: () => r.click && resultCards.mouth(r.click),
        dir: () => r.direction && resultCards.dir(r.direction),
    }[id]();
    return built || { id, word: "flow.w.untested", nums: [], none: true, missing: true, tone: "gray", stale: false };
}

/** 卡是否有结论：未测、过期、要再测的都没有。 */
const conclusive = (c) => !c.missing && !c.stale && !c.retest && !c.none;

export function cardsOf(state) {
    const prevAll = state.previous || {};
    const hideDir = !visibleSteps(state).includes("direction");
    return CARD_ORDER.filter((id) => !(hideDir && id === "dir")).map((id) => {
        const card = cardOf(id, state);
        if (!conclusive(card)) return card;
        const before = cardOf(id, state, prevAll);
        const a = before.nums[0] && before.nums[0].text;
        const b = card.nums[0] && card.nums[0].text;
        return conclusive(before) && a && b ? { ...card, prev: { a, b } } : card;
    });
}

// ---------------------------------------------------------------------------------------------------------------------
// 距离标尺

/**
 * 标尺上的位置是示意（0 到 1，左近右远），不是距离：中间 0.5，每多一条证据多走 POS_STEP；不显示任何距离数值。
 * 这些数只决定标记画在哪，判据在原生侧（distance.kind）。
 */
const POS_MID = 0.5;
const POS_STEP = 0.1;
const POS_NEAR = 0.3;
const POS_FAR = 0.7;
const POS_EDGE = 0.08;

export function rulerPos(d) {
    if (!d) return null;
    if (d.kind === "too-close") return Math.max(POS_EDGE, POS_NEAR - POS_STEP * (Math.max(1, d.closeEvidence) - 1));
    if (d.kind === "too-far") return Math.min(1 - POS_EDGE, POS_FAR + POS_STEP * (Math.max(1, d.farEvidence) - 1));
    return d.kind === "fine" || d.kind === "conflicting" ? POS_MID : null;
}

/**
 * 标尺：证据是否触发，由别的结果推出（距离只给了条数）。齿音与口水音不分档，不作证据，标成“未参与”，不写成“没有”。
 * 状态：fresh；stale（位置挪过）；low（证据弱或要再测）。
 */
export function rulerModel(state) {
    const r = results(state);
    const d = r.distance;
    const live = (s) => s && !s.stale && s.reliability === "trusted";
    const stale = !!(d && d.stale);
    const low = !d || d.reliability === "retest" || d.kind === "unknown";
    const pd = state.previous && state.previous.distance;
    return {
        st: stale ? "stale" : low ? "low" : "fresh",
        pos: d ? rulerPos(d) : null,
        prev: pd ? rulerPos(pd) : null,
        conflict: !!d && d.kind === "conflicting",
        near: [
            { key: "plosive", on: live(r.plosive) && r.plosive.severity !== "light" },
            { key: "sib", on: false, off: true },
            { key: "mouth", on: false, off: true },
        ],
        far: [
            { key: "room", on: live(r.directRatio) && r.directRatio.band === "low" },
            { key: "snr", on: live(r.noise) && r.noise.band === "bad", minor: true },
        ],
    };
}

// ---------------------------------------------------------------------------------------------------------------------
// 最该改的一件事

const act = (key, vars) => ({ key, vars });
const retestCta = (steps) => ({ key: "report.retest", kind: "retest", steps: inOrder(steps) });
const COPY = { key: "report.copy", kind: "copy" };

/** 缺的步骤：四步里还没有结果的。 */
const missingSteps = (state) => RETEST_STEPS.filter((id) => !SECTIONS_OF[id].some((s) => results(state)[s]));
/** 有读数要再测的步骤。 */
const retestSteps = (state) =>
    RETEST_STEPS.filter((id) =>
        SECTIONS_OF[id].some((s) => results(state)[s] && results(state)[s].reliability === "retest"),
    );

function gainNumbers(state) {
    const g = results(state).gain;
    const b = state.bands;
    if (!g || !b) return null;
    const right = state.device && state.device.layout === "stereo" && (state.connect || {}).analysedChannel === "right";
    const peak = g[right ? "truePeakDbfsR" : "truePeakDbfsL"];
    // 快照缺字段或给了非有限数时不写数字，调用处按 null 省略这条动作
    if (!Number.isFinite(b.targetPeakDbfs) || !Number.isFinite(peak)) return null;
    return { n: Math.round(Math.abs(b.targetPeakDbfs - peak)) };
}

/** fixes 的一项到顶部卡的内容。dirOff：方向提示不放在顶部（耳机麦，或方向测过且没过期）。 */
function fixFor(item, state, p) {
    const r = results(state);
    const advice = state.advice || { room: "unknown", distance: "none", directionPrompt: "none" };
    const dirOff = p === "none" || p === "stale";
    const tone = (s, d = "amber") => (s && (s.tone === "red" || s.tone === "amber") ? s.tone : d);
    const close = act("report.fx.closer");
    if (item === "no-signal")
        return {
            tone: "red",
            head: act("report.fx.nosig.h"),
            acts: [act("report.fx.nosig.a1")],
            cta: { key: "report.fx.nosig.cta", kind: "go", to: "connect" },
        };
    if (item === "digital-clip") {
        const usb = state.micType === "usb";
        const drop = state.bands && { n: state.bands.clipAdviceDropDb };
        return {
            tone: "red",
            head: act("report.fx.clip.h"),
            acts: [...(drop ? [act(usb ? "flow.gr.clip.usb.a" : "flow.gr.clip.a", drop)] : []), act("flow.gr.retest")],
            cta: retestCta(["gain"]),
        };
    }
    if (item === "direction")
        return {
            tone: "red",
            head: act("report.fx.dir.h"),
            acts: [act("flow.dr1.a1"), act("flow.dr1.a2")],
            cta: { key: "report.fx.dir.cta", kind: "go", to: "direction" },
        };
    if (item === "room") {
        switch (advice.room) {
            case "change-room-and-move-closer":
                return {
                    tone: "red",
                    head: act("report.fx.room1.h"),
                    acts: [act("flow.rr.a1"), close, act("report.fx.room1.a3")],
                    cta: retestCta(["gain", "quiet", "room", "position"]),
                };
            case "move-closer-check-direction":
                return p === "main-button"
                    ? {
                          tone: "red",
                          head: act("report.fx.room2.h"),
                          acts: [close, act("report.fx.room2.a2")],
                          cta: { key: "report.dir.opt", kind: "go", to: "direction", prep: true },
                      }
                    : { tone: "red", head: act("report.fx.room2.hs"), acts: [close], cta: retestCta(["position"]) };
            case "move-closer-then-treat":
                return {
                    tone: "amber",
                    head: act("report.fx.room3.h"),
                    acts: [close, act("report.fx.room3.a2")],
                    cta: retestCta(["gain", "quiet", "position"]),
                };
            default:
                return { tone: "amber", head: act("report.fx.room0.h"), acts: [close], cta: retestCta(["position"]) };
        }
    }
    if (item === "distance") {
        switch (advice.distance) {
            case "back-off-a-little":
                return {
                    tone: "red",
                    head: act("report.fx.near.h"),
                    acts: [act("report.fx.near.a1"), act("report.fx.near.a2")],
                    cta: retestCta(["position"]),
                };
            case "drink-water":
                return {
                    tone: "red",
                    head: act("report.fx.water.h"),
                    acts: [act("report.fx.water.a1"), act("report.fx.water.a2")],
                    cta: retestCta(["position"]),
                };
            case "fix-room-or-direction-first":
                return {
                    tone: "red",
                    head: act(dirOff ? "report.fx.conf.hs" : "report.fx.conf.h"),
                    acts: [act("report.fx.conf.a1"), act("report.fx.conf.a3")],
                    cta: retestCta(["room", "position"]),
                    dirAct: "report.fx.conf.a4",
                };
            default:
                return { tone: "red", head: act("report.fx.room2.hs"), acts: [close], cta: retestCta(["position"]) };
        }
    }
    if (item === "hum") {
        const by = {
            hum: ["flow.qr.hum.a1", "flow.qr.hum.a2"],
            buzz: ["flow.qr.buzz.a1", "flow.qr.buzz.a2"],
            whistle: ["flow.qr.whine.a1", "flow.qr.whine.a2"],
        }[r.hum ? r.hum.kind : "hum"] || ["flow.qr.hum.a1", "flow.qr.hum.a2"];
        return { tone: "amber", head: act("report.fx.hum.h"), acts: by.map((k) => act(k)), cta: retestCta(["quiet"]) };
    }
    if (item === "gain") {
        const low = r.gain && (r.gain.band === "quiet" || r.gain.band === "too-quiet");
        const num = gainNumbers(state);
        return {
            tone: tone(r.gain),
            head: act(low ? "report.fx.gain.low.h" : "report.fx.gain.high.h"),
            acts: [...(num ? [act(low ? "flow.gr.low.a" : "flow.gr.hot.a", num)] : []), act("flow.gr.retest")],
            cta: retestCta(["gain"]),
        };
    }
    if (item === "noise") {
        const lowGain = r.gain && (r.gain.band === "quiet" || r.gain.band === "too-quiet");
        const num = gainNumbers(state);
        return lowGain
            ? {
                  tone: tone(r.noise),
                  head: act("report.fx.noisequiet.h"),
                  acts: [close, ...(num ? [act("report.fx.noisequiet.a2", num)] : [])],
                  cta: retestCta(["gain", "quiet"]),
                  dirAct: "report.fx.noisequiet.a3",
              }
            : {
                  tone: tone(r.noise),
                  head: act("report.fx.noise.h"),
                  acts: [act("flow.qr.bad.a1")],
                  cta: retestCta(["quiet"]),
              };
    }
    // plosive
    return {
        tone: tone(r.plosive),
        head: act("report.fx.near.h"),
        acts: [act("report.fx.near.a1"), act("report.fx.near.a2")],
        cta: retestCta(["position"]),
    };
}

/** 过期的原因看哪些区段过期：换房间让房间声过期；挪位置让位置相关的过期；调增益只让增益、底噪、电流声、齿音过期。 */
const STALE_CAUSE = [
    ["rs3", ["reverb"]],
    ["rs2", ["directRatio", "plosive", "click", "distance"]],
    ["rs1", ["gain", "noise", "hum", "sibilance"]],
];
/** 重测要带上的步骤：只看它自己的主要区段（齿音单独过期不催用户重测一整步位置）。 */
const STALE_PRIMARY = {
    gain: ["gain"],
    quiet: ["noise", "hum"],
    room: ["reverb"],
    position: ["directRatio", "plosive", "click"],
};

export function fixModel(state) {
    const fixes = state.fixes || [];
    const advice = state.advice || {};
    const p = advice.directionPrompt || "none";
    const isStale = (sec) => !!(results(state)[sec] && results(state)[sec].stale);
    const stale = RETEST_STEPS.filter((id) => STALE_PRIMARY[id].some(isStale));
    const head0 = fixes[0];
    const fixed = head0 === "no-signal" || head0 === "digital-clip";
    const out = (m) => ({ eyebrow: m.tone === "green" ? "report.fix.ok" : "report.fix", sec: null, ...m });

    if (stale.length && !fixed) {
        const k = (STALE_CAUSE.find(([, secs]) => secs.some(isStale)) || STALE_CAUSE[2])[0];
        return out({
            tone: "gray",
            head: act(`report.fx.${k}.h`),
            acts: [act(`report.fx.${k}.a1`, { steps: stale }), ...(k === "rs2" ? [act("report.fx.rs2.a2")] : [])],
            cta: retestCta(stale),
        });
    }
    if (fixed) return withDirection(out(fixFor(head0, state, p)), state, p);
    const rt = retestSteps(state);
    if (rt.length)
        return out({
            tone: "gray",
            head: act("report.fx.low.h", { n: rt.length }),
            acts: [
                ...(rt.includes("quiet") ? [act("report.fx.low.a1")] : []),
                ...(rt.includes("room") ? [act("report.fx.low.a2")] : []),
                act("report.fx.low.a3"),
            ],
            cta: retestCta(rt),
        });
    if (head0) return withDirection(out(fixFor(head0, state, p)), state, p);
    const miss = missingSteps(state);
    if (miss.length === RETEST_STEPS.length)
        return out({
            tone: "gray",
            head: act("report.fx.none.h"),
            acts: [act("report.fx.none.a1")],
            cta: { key: "report.fx.none.cta", kind: "go", to: "connect" },
        });
    if (miss.length)
        return out({
            tone: "gray",
            head: act("report.fx.part.h"),
            acts: [act("report.fx.part.a1", { steps: miss })],
            cta: { key: "report.fx.part.cta", kind: "go", to: miss[0] },
        });
    if (advice.room === "keep-close-dont-back-off")
        return out({
            tone: "amber",
            head: act("report.fx.keep.h"),
            acts: [act("report.fx.keep.a1"), act("report.fx.keep.a2")],
            cta: COPY,
        });
    const dirDone = results(state).direction && statusOf(state, "direction") === "done";
    return out({
        tone: "green",
        head: act("report.fx.ok.h"),
        acts: [
            act("report.fx.ok.a1"),
            ...(dirDone || !visibleSteps(state).includes("direction") ? [] : [act("report.fx.ok.a2")]),
        ],
        cta: COPY,
    });
}

/**
 * 方向提示放进顶部卡：main-button 已在 fixFor 里换了主按钮；action-with-button 在动作最后加一条，再给次要按钮。
 * 顶部卡最多放 3 条动作（英文四条会挤出卡外），所以方向那条占第 3 条，原来的第 3 条（只是解释主按钮）让位。
 */
const ACTS_WITH_DIR = 3;
function withDirection(m, state, p) {
    if (p !== "action-with-button") return m;
    const key = m.dirAct || "report.fx.conf.a4";
    return {
        ...m,
        acts: [...m.acts.slice(0, ACTS_WITH_DIR - 1), { key, dir: true }],
        sec: { key: "report.dir.go", kind: "go", to: "direction", prep: true },
    };
}

/** 方向卡上的小按钮：card-button 是“测一下方向”，stale 是“重测”；其余不放。耳机麦没有方向卡。 */
export function directionSlot(state) {
    if (!visibleSteps(state).includes("direction")) return null;
    const p = (state.advice || {}).directionPrompt;
    return p === "card-button" ? "go" : p === "stale" ? "again" : null;
}

// ---------------------------------------------------------------------------------------------------------------------
// 复制结果

/** 一句话结果的组成：有结论的卡各一小段，最后接最该改的那句（灰色的不接）。 */
export function copyParts(state, cards = cardsOf(state), fix = fixModel(state)) {
    const num = (id) => {
        const c = cards.find((x) => x.id === id);
        return c && conclusive(c) ? c.nums[0].text : null;
    };
    const parts = [
        ["report.cp.level", "level"],
        ["report.cp.snr", "noise"],
        ["report.cp.t20", "room"],
        ["report.cp.dr", "dr"],
    ]
        .map(([key, id]) => ({ key, v: num(id) }))
        .filter((p) => p.v !== null);
    return { parts, fix: fix.tone === "gray" ? null : fix.head };
}

export { STEP_IDS };
