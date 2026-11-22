// SPDX-License-Identifier: GPL-3.0-or-later
//
// 报告页的纯逻辑：最该改的一件事、距离卡（前后对比）、九张结果卡、方向提示放在哪。不碰 DOM，Node 里可以直接 import 做测试。
// 页面全部由快照推出：排序在原生侧（fixes、advice），这里只把名字翻成词条键和动作，不重新排序、不写阈值。
//
// 取舍顺序（从上到下第一个命中的就是顶部那张卡）：
//   1. fixes 的第一项是“没信号”或“数字削波”：照 fixes 来（这两项只看是否过期，不看置信度，也不看有没有别的要再测；
//      fixes 只收没过期的结果，所以它们出现时就是现在的状态，先处理它们再谈重测过期的几步）；
//   2. 有步骤过期：灰，写哪些过期、主按钮重测它们（过期的结果本来就不进 fixes）；
//   3. 有读数要再测：灰，先重测它们（要再测的结果没有结论，也不进 fixes）；
//   4. fixes 有第一项：照 fixes 与 advice 来；
//   5. fixes 为空：四步都做过是绿的“可以录”，否则灰的“还没测完”。
// 距离与喷麦都不进 fixes：距离只有第一次的弱提示和重测后的前后对比，放在距离卡上。

import {
    cardExtras,
    distanceReading,
    recDistance,
    resultCards,
    signed,
    STEP_IDS,
    visibleSteps,
} from "../flow/flow-core.js";

/** 九张卡的固定顺序，以及每张卡对应的结果区段与步骤。 */
export const CARD_ORDER = ["level", "noise", "hum", "room", "dr", "plosive", "sib", "mouth", "dir"];
export const STEP_OF = {
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
    // 参考行与轻提示只属于当前结果：上次（previous）的卡不带建议
    const x = cardExtras(state, from === state.results);
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
        plosive: () => r.plosive && resultCards.plosive(r.plosive, x),
        sib: () => r.sibilance && resultCards.sib(r.sibilance, x),
        mouth: () => r.click && resultCards.mouth(r.click, x),
        dir: () => r.direction && resultCards.dir(r.direction),
    }[id]();
    return built || { id, word: "flow.w.untested", nums: [], none: true, missing: true, tone: "gray", stale: false };
}

/** 卡是否有结论：未测、过期、要再测的都没有。 */
const conclusive = (c) => !c.missing && !c.stale && !c.retest && !c.none;

/** 每张卡在 previous 里对应的区段（previous 只含被重测换掉的区段）；电流声跟着噪声一起测，两个都算。 */
const PREVIOUS_OF = {
    level: ["gain"],
    noise: ["noise"],
    hum: ["hum", "noise"],
    room: ["reverb"],
    dr: ["directRatio"],
    plosive: ["plosive"],
    sib: ["sibilance"],
    mouth: ["click"],
    dir: ["direction"],
};
/** 这一步做完过几次才算“重测过”。 */
const RETESTED_RUNS = 2;

/** 首次测量：这一步已经做完不止一次，而这张卡所在的区段没有上次的读数（上次没有这一项，或那次不可用）。 */
export function isFirstMeasure(id, state) {
    const step = state.steps && state.steps[STEP_OF[id]];
    const runs = step && Number.isInteger(step.runs) ? step.runs : 0;
    const prev = state.previous || {};
    return runs >= RETESTED_RUNS && PREVIOUS_OF[id].every((s) => !prev[s]);
}

/** “上次 → 现在”写哪两个数：一般是第一个数；齿音按 cmp 取（同一频段才可比）。 */
function compared(before, now) {
    if (before.cmp && now.cmp)
        return before.cmp.high && now.cmp.high ? [before.cmp.high, now.cmp.high] : [before.cmp.mid, now.cmp.mid];
    return [before.nums[0] && before.nums[0].text, now.nums[0] && now.nums[0].text];
}

export function cardsOf(state) {
    const prevAll = state.previous || {};
    const hideDir = !visibleSteps(state).includes("direction");
    return CARD_ORDER.filter((id) => !(hideDir && id === "dir")).map((id) => {
        const card = cardOf(id, state);
        if (!conclusive(card)) return card;
        const before = cardOf(id, state, prevAll);
        const [a, b] = compared(before, card);
        if (conclusive(before) && a && b) return { ...card, prev: { a, b } };
        return isFirstMeasure(id, state) ? { ...card, first: true } : card;
    });
}

// ---------------------------------------------------------------------------------------------------------------------
// 距离卡

/**
 * 标尺上的位置是示意（0 到 1，左近右远），不是距离：上次的位置固定在中间（虚影），现在的标记按指向更近或更远的项数
 * 往左或往右挪一格（最多三格）；不显示任何距离数值。判据在原生侧（distance.kind 与各项的 sign）。
 */
const POS_MID = 0.5;
const POS_STEP = 0.13;
const POS_MAX_STEPS = 3;
/** 做了前后对比的情形：有上次的位置可画。 */
const COMPARED_KINDS = ["closer", "farther", "no-change", "conflicting"];
const SIGN_KINDS = ["not-compared", "closer", "farther", "no-change", "gain-changed", "room-changed"];
/** 前后对比用的三项读数：卡片 id、结果里的 sign 与 delta 字段。口水音只作提示，不进对比。 */
const SIGN_FIELDS = [
    ["dr", "drSign", "drDeltaDb"],
    ["plosive", "plosiveSign", "plosiveDeltaDb"],
    ["sib", "sibilanceSign", "sibilanceDeltaDb"],
];

/**
 * 距离卡：第一次只有“可能离远了”这条弱提示；重测位置之后，按 D/R、喷麦、齿音的变化说更近、更远、没有明显变化或说不准。
 * 状态 st：none（位置还没测）、fresh、stale（位置挪过）、low（原生要再测）。read 是一句话，sign 的 delta 是现在减上次（dB）。
 */
export function distanceModel(state) {
    const d = results(state).distance;
    const { shown, n } = distanceReading(d);
    const st = !d ? "none" : d.stale ? "stale" : d.reliability === "retest" ? "low" : "fresh";
    const fresh = st === "fresh";
    const compared = fresh && COMPARED_KINDS.includes(shown);
    const steps = Math.min(Math.max(1, n || 1), POS_MAX_STEPS);
    const pos = !compared
        ? null
        : shown === "closer"
          ? POS_MID - POS_STEP * steps
          : shown === "farther"
            ? POS_MID + POS_STEP * steps
            : shown === "no-change"
              ? POS_MID
              : null;
    const signs = SIGN_FIELDS.map(([key, sk, dk]) => {
        const sign = d && SIGN_KINDS.includes(d[sk]) ? d[sk] : "not-compared";
        const directional = sign === "closer" || sign === "farther" || sign === "no-change";
        return { key, sign, delta: directional && Number.isFinite(d[dk]) ? signed(d[dk]) : null };
    });
    return {
        st,
        kind: shown,
        read: d ? { key: `flow.pr.e.${shown}`, vars: { n } } : { key: "report.dist.none" },
        hint: fresh && shown === "maybe-too-far",
        ghost: compared ? POS_MID : null,
        pos,
        // 三项读数都没比过（第一次）时不列
        signs: fresh && signs.some((x) => x.sign !== "not-compared") ? signs : [],
        rec: recDistance(state),
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

/** fixes 的一项到顶部卡的内容；不认识的项返回 null。 */
function fixFor(item, state, p) {
    const r = results(state);
    const advice = state.advice || { room: "unknown", distance: "none", directionPrompt: "none" };
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
        // 房间建议只看 T20：很混先换地方或加吸声，偏混先靠近；别的取值（旧的原生端）按偏混写
        return advice.room === "change-room-and-move-closer"
            ? {
                  tone: "red",
                  head: act("report.fx.room1.h"),
                  acts: [act("flow.rr.a1"), close, act("report.fx.room1.a3")],
                  cta: retestCta(["gain", "quiet", "room", "position"]),
              }
            : {
                  tone: "amber",
                  head: act("report.fx.room3.h"),
                  acts: [close, act("report.fx.room3.a2")],
                  cta: retestCta(["gain", "quiet", "position"]),
              };
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
        // 底噪偏大又可能离远了（T20 不长，所以不是房间的事）：先靠近。方向检查没做过时放在主按钮上
        if (advice.distance === "move-closer")
            return p === "main-button"
                ? {
                      tone: tone(r.noise),
                      head: act("report.fx.far.h"),
                      acts: [close, act("report.fx.far.a2")],
                      cta: { key: "report.dir.opt", kind: "go", to: "direction", prep: true },
                  }
                : {
                      tone: tone(r.noise),
                      head: act("report.fx.farnoise.h"),
                      acts: [close, act("report.fx.farnoise.a2")],
                      cta: retestCta(["position"]),
                  };
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
    return null;
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

/** 界面认识的“最该改”的项（桥契约的 FixItem）；别的（旧的原生端留下的 distance、plosive 之类）跳过，不照它写。 */
const KNOWN_FIXES = ["no-signal", "digital-clip", "direction", "room", "hum", "gain", "noise"];

export function fixModel(state) {
    const fixes = (state.fixes || []).filter((f) => KNOWN_FIXES.includes(f));
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
    const dirDone = results(state).direction && statusOf(state, "direction") === "done";
    const dirOpen = !dirDone && visibleSteps(state).includes("direction");
    return out({
        tone: "green",
        head: act("report.fx.ok.h"),
        acts: [act("report.fx.ok.a1"), ...(dirOpen ? [act("report.fx.ok.a2")] : [])],
        cta: COPY,
        // 几项读数指向不同的方向（说不准）时，方向提示是“动作加按钮”：绿的结论下面给一个次要按钮，不改结论
        sec:
            dirOpen && p === "action-with-button"
                ? { key: "report.dir.go", kind: "go", to: "direction", prep: true }
                : null,
    });
}

/**
 * 方向提示放进顶部卡：main-button 已在 fixFor 里换了主按钮；action-with-button 在动作最后加一条，再给次要按钮。
 * 顶部卡最多放 3 条动作（英文四条会挤出卡外），所以方向那条占第 3 条，原来的第 3 条（只是解释主按钮）让位。
 */
const ACTS_WITH_DIR = 3;
function withDirection(m, state, p) {
    if (p !== "action-with-button") return m;
    const key = m.dirAct || "report.fx.dirhint";
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
