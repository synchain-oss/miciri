// SPDX-License-Identifier: GPL-3.0-or-later
//
// 流程页的纯逻辑：步骤表、各页面的数据模型。不碰 DOM，Node 里可以直接 import 做测试。
// 页面全部由快照推出，数值、档位边界、计数一律取自快照（bands、live、steps、results），这里没有阈值。
//
// 步骤表（STEPS、POSITION_PHASES）是流程的唯一编排数据：增减一个子步骤只改这里，组件不动。
// 位置步的“轻拍 3 次”就是 POSITION_PHASES 里独立的一行，删掉那一行，准备页、测试中视图和部分计数都跟着变。

import { fmtDb } from "../meter/meter-core.js";

/** 叠卡里的六张卡，按先后；第七页是报告。 */
export const STEP_IDS = ["connect", "gain", "quiet", "room", "position", "direction"];
export const REPORT = "report";
/** 词条里步骤名的键（词条沿用 pos、dir 的短名）。 */
export const STEP_KEY = {
    connect: "connect",
    gain: "gain",
    quiet: "quiet",
    room: "room",
    position: "pos",
    direction: "dir",
    report: "report",
};
/** 每步的准备页：图标、会测哪些项（词条键）、怎么做的几条（词条键）。位置步的“怎么做”由 POSITION_PHASES 生成。 */
export const STEPS = {
    gain: {
        icon: "gain",
        items: ["flow.ms.tp", "flow.ms.clip", "flow.ms.target"],
        how: ["flow.pp.gain.1", "flow.pp.gain.2", "flow.pp.gain.3"],
    },
    quiet: {
        icon: "quiet",
        items: ["flow.ms.snr", "flow.ms.hum", "flow.ms.dev"],
        how: ["flow.pp.quiet.1", "flow.pp.quiet.2", "flow.pp.quiet.3"],
    },
    room: {
        icon: "ripple",
        items: ["flow.ms.t20"],
        how: ["flow.pp.room.1", "flow.pp.room.2", "flow.pp.room.3"],
    },
    position: {
        icon: "pos",
        items: ["flow.ms.dr", "flow.ms.plosive", "flow.ms.sib", "flow.ms.mouth"],
        how: [],
    },
    direction: {
        icon: "dir",
        items: ["flow.ms.dir"],
        how: ["flow.pp.dir.1", "flow.pp.dir.2", "flow.pp.dir.3"],
    },
};
/** 方向步的方向数与转动提示；方向步在 live.rounds 里也会给出，这里只用来写准备页。 */
export const DIRECTION_COUNT = 4;
const DIRECTION_HINT = ["flow.rm.dir.d1", "flow.rm.dir.d2", "flow.rm.dir.d3", "flow.rm.dir.d4"];

/**
 * 位置步的子步骤，按先后。`id` 对应桥契约的 ENUM_STEP_PHASE；`n` 是准备页写的次数或遍数（测试中以 live.rounds 为准）；
 * `aux` 决定测试中右下角写“第几部分”还是“第几遍”；`words` 是念的字表（词条键）；
 * `taps`：逐下数的轻拍。自动推进时按遍画点；手动分段时一直听、逐下给判定（live.taps），凑够 live.tapsNeeded 下有效就自己往下走。
 * 想删掉“轻拍 3 次”：删掉第一行即可，Session 不再发 soft-clap。
 */
export const POSITION_PHASES = [
    { id: "soft-clap", n: 3, how: "flow.pp.pos.soft-clap", cmd: "flow.rm.pos.clap", aux: "part", taps: true },
    {
        id: "plosive",
        n: 2,
        how: "flow.pp.pos.plosive",
        cmd: "flow.rm.pos.p",
        aux: "round",
        words: "flow.rm.words.p",
        list: "flow.pos.words.p",
    },
    {
        id: "sibilant",
        n: 2,
        how: "flow.pp.pos.sibilant",
        cmd: "flow.rm.pos.p",
        aux: "round",
        words: "flow.rm.words.s",
        list: "flow.pos.words.s",
    },
    { id: "natural", n: 1, how: "flow.pp.pos.natural", cmd: "flow.rm.pos.n", aux: "part", words: "flow.rm.natural" },
];

// ---------------------------------------------------------------------------------------------------------------------
// 步骤状态

const statusOf = (state, id) => (state && state.steps && state.steps[id] ? state.steps[id].status : "idle");

/** 耳机麦没有方向步。 */
export function visibleSteps(state) {
    const headset = !!state && state.micType === "headset";
    return STEP_IDS.filter((id) => !(headset && id === "direction"));
}

/**
 * 步骤条与摘要卡用的五态：todo、current、done、stale、fail。current 由调用方按当前页覆盖。
 * 接通只看此刻的信号判断，不看步骤状态：接通观察每做完一轮都记 done，不管听到的是什么。
 * 信号正常或只是极低（琥珀，不挡“开始”）算通过；没收到音频、全零是 fail；还没听到声音是 todo。
 */
export function stepState(state, id) {
    if (id === "connect") {
        const m = connectModel(state);
        return m.redBlock ? "fail" : m.mode === "first" ? "todo" : "done";
    }
    const s = statusOf(state, id);
    if (s === "stale") return "stale";
    if (s === "invalid") return "fail";
    if (s === "done") return "done";
    return "todo";
}

/** 一张卡此刻显示什么：接通页、准备页、测试中、结果页。 */
export function cardMode(state, id, forcePrep) {
    if (id === "connect") return "connect";
    const s = statusOf(state, id);
    if (s === "running") return "running";
    if (forcePrep) return "prep";
    return s === "idle" ? "prep" : "result";
}

/** 打开窗口时落在哪一页：接通没通过、或者后面的步骤一个都没做过，是接通页；否则第一个没做完的步骤。 */
export function initialView(state) {
    const ids = visibleSteps(state);
    const running = ids.find((id) => statusOf(state, id) === "running");
    if (running) return running;
    if (stepState(state, "connect") !== "done") return "connect";
    if (ids.every((id) => id === "connect" || stepState(state, id) === "todo")) return "connect";
    return ids.find((id) => id !== "connect" && stepState(state, id) !== "done") || ids[ids.length - 1];
}

/** 相邻的页：dir 为 1 或 −1；越界返回 null。最后一页的下一页是报告。 */
export function neighbour(state, view, dir) {
    const pages = [...visibleSteps(state), REPORT];
    const i = pages.indexOf(view);
    const j = i + dir;
    return i < 0 || j < 0 || j >= pages.length ? null : pages[j];
}

// ---------------------------------------------------------------------------------------------------------------------
// 档位词（词条键）

const GAIN_WORD = {
    "too-quiet": "flow.w.toolow",
    quiet: "flow.w.low",
    good: "flow.w.fit",
    loud: "flow.w.hot",
    "too-loud": "flow.w.over",
};
const NOISE_WORD = { bad: "flow.w.snr.bad", fair: "flow.w.snr.mid", good: "flow.w.snr.good" };
const REVERB_WORD = {
    dry: "flow.w.dry",
    normal: "flow.w.normal",
    boomy: "flow.w.mixed",
    "very-boomy": "flow.w.verymixed",
};
const DIR_WORD = {
    best: "flow.w.d.ok",
    "maybe-reversed": "flow.w.d.flipped",
    "small-difference": "flow.w.d.flat",
    uncertain: "flow.w.d.unsure",
};
const HUM_WORD = { hum: "flow.w.hum", buzz: "flow.w.buzz", whistle: "flow.w.whine" };

export const gainWord = (g) =>
    g.digitalClip ? "flow.w.clipped" : g.maybeFlatTop ? "flow.w.clip.maybe" : GAIN_WORD[g.band] || "flow.w.untested";
export const noiseWord = (n) => NOISE_WORD[n.band] || "flow.w.untested";
export const humWord = (h) =>
    h.kind === "none"
        ? "flow.w.none"
        : h.slight && h.kind === "hum"
          ? "flow.w.hum.weak"
          : HUM_WORD[h.kind] || "flow.w.none";
export const reverbWord = (r) => (r.gated ? "flow.w.unk" : REVERB_WORD[r.band] || "flow.w.untested");
/** D/R、喷麦、齿音、口水音都不分档：没有“多、少、轻、重”一类的词，卡上的词一律是“未分档”，内容是数字和参考值。 */
export const UNGRADED_WORD = "flow.w.ungraded";
export const dirWord = (d) => DIR_WORD[d.kind] || "flow.w.untested";

/** 被分析的那一路：单声道取 L。 */
export const laneOf = (state) =>
    state.device && state.device.layout === "stereo" && state.connect && state.connect.analysedChannel === "right"
        ? "R"
        : "L";
export const truePeak = (state, g) => g[`truePeakDbfs${laneOf(state)}`];

// ---------------------------------------------------------------------------------------------------------------------
// 摘要卡（叠卡上下露出的那一张）

/** @returns {Array<{key: string, vars?: object}>} 一张摘要卡里依次排的词条（用“·”隔开） */
export function peekSegments(state, id) {
    if (id === REPORT) return [{ key: "flow.stk.report" }];
    if (id === "connect") {
        // 接通的摘要跟着信号判断：有诊断写诊断的标题，还没听到声音写“等你出声”，只有正常时写“信号正常”
        const m = connectModel(state);
        if (m.diag && !m.ok) return [{ key: m.diag.t }];
        if (m.mode === "first") return [{ key: "flow.cn.wait" }];
        const dev = state.device || {};
        return [
            {
                key: "flow.sum.connect",
                vars: {
                    ch: { word: dev.layout === "stereo" ? "meter.in.ch.st" : "meter.in.ch.mono" },
                    sr: `${+((dev.sampleRate || 0) / 1000).toFixed(1)} kHz`,
                },
            },
        ];
    }
    const st = stepState(state, id);
    if (st === "stale") return [{ key: "flow.cf.stale" }];
    if (st === "fail") return [{ key: "flow.sum.fail" }];
    const r = (state && state.results) || {};
    if (st === "done") {
        if (id === "gain" && r.gain)
            return [
                { key: "flow.sum.gain", vars: { w: { word: gainWord(r.gain) }, v: fmtDb(truePeak(state, r.gain)) } },
            ];
        if (id === "quiet" && r.noise)
            return [
                {
                    key: "flow.sum.quiet",
                    vars: {
                        w: { word: noiseWord(r.noise) },
                        v: fmtDb(r.noise.snrDb),
                        h: fmtDb(r.hum ? r.hum.overFloorDb : 0),
                    },
                },
            ];
        if (id === "room" && r.reverb)
            return [
                Number.isFinite(r.reverb.tMidSec)
                    ? {
                          key: "flow.sum.room",
                          vars: { w: { word: reverbWord(r.reverb) }, v: r.reverb.tMidSec.toFixed(2) },
                      }
                    : { key: "flow.w.unk" },
            ];
        if (id === "position" && r.directRatio) {
            // 四项都不分档，摘要里只写读数；齿音写卡上的主数字（比元音，比不了时是一个频段的电平）
            const sb = r.sibilance;
            return [
                { key: "flow.sum.dr", vars: { n: signed(r.directRatio.drDb) } },
                r.plosive && { key: "flow.sum.plosive", vars: { n: signed(r.plosive.worstRatioDb) } },
                sb &&
                    (sibVowel(sb)
                        ? { key: "flow.sum.sib", vars: { n: signed(sb.highOverVowelDb) } }
                        : {
                              key: "flow.sum.sib.lv",
                              vars: { n: signed(sb.highBandDisabled ? sb.midHighDb : sb.highDb) },
                          }),
                r.click && { key: "flow.sum.mouth", vars: { n: String(Math.round(r.click.perMinute)) } },
            ].filter(Boolean);
        }
        if (id === "direction" && r.direction)
            return [{ key: "flow.sum.kv", vars: { k: { word: "flow.card.dir" }, w: { word: dirWord(r.direction) } } }];
    }
    return [{ key: "flow.w.untested" }];
}

/** 数字格式：带正负号，负号用 U+2212。 */
export function signed(v, digits = 1) {
    const s = Math.abs(v).toFixed(digits);
    return (v < 0 && Number(s) !== 0 ? "−" : "+") + s;
}

// ---------------------------------------------------------------------------------------------------------------------
// 接通与无信号诊断

/** 诊断表：tone、图标、标题与正文词条、“插件看到的”两行（标签词条、值词条）。 */
const DIAG = {
    nocb: {
        tone: "red",
        icon: "alert",
        t: "flow.dg.nocb.t",
        b: "flow.dg.nocb.b",
        seen: [["flow.dg.l.cb", "flow.dg.v.none2s"]],
    },
    zero: {
        tone: "red",
        icon: "alert",
        t: "flow.dg.zero.t",
        b: "flow.dg.zero.b",
        seen: [
            ["flow.dg.l.cb", "flow.dg.v.normal"],
            ["flow.dg.l.smp", "flow.dg.v.zero"],
        ],
    },
    zero2: {
        tone: "red",
        icon: "alert",
        t: "flow.dg.zero2.t",
        b: "flow.dg.zero2.b",
        seen: [
            ["flow.dg.l.cb", "flow.dg.v.normal"],
            ["flow.dg.l.smp", "flow.dg.v.zero2"],
        ],
    },
    lownoise: {
        tone: "amber",
        icon: "info",
        t: "flow.dg.low.t",
        b: "flow.dg.low.b",
        seen: [
            ["flow.dg.l.cb", "flow.dg.v.normal"],
            ["flow.dg.l.lvl", "flow.dg.v.low"],
        ],
    },
    st1: { tone: "amber", icon: "info", t: "flow.dg.st1.t", b: "flow.dg.st1.b", seen: "channels" },
    st1fl: { tone: "amber", icon: "info", t: "flow.dg.st1.t", b: "flow.dg.st1fl.b", seen: "channels", host: true },
    dom: { tone: "amber", icon: "info", t: "flow.dg.dom.t", b: "flow.dg.dom.b", seen: "channels" },
    anti: { tone: "amber", icon: "info", t: "flow.dg.anti.t", b: "flow.dg.anti.b", seen: "channels" },
    same: { tone: "amber", icon: "info", t: "flow.dg.same.t", b: "flow.dg.same.b", seen: "channels", dismiss: true },
};
const PROMPT_MODE = {
    "one-silent": "st1",
    "one-silent-host": "st1fl",
    "one-dominant": "dom",
    antiphase: "anti",
    identical: "same",
};
const SIGNAL_MODE = {
    "no-callback": "nocb",
    "all-zero-from-start": "zero",
    "all-zero-after-signal": "zero2",
    "very-low-noise": "lownoise",
};
/** 声道行的值词条：按每一路的判定。 */
const CHANNEL_VALUE = {
    signal: "flow.dg.v.sig",
    "floor-only": "flow.dg.v.floor",
    silent: "flow.dg.v.nosig",
    "same-as-left": "flow.dg.v.same",
    "inverted-from-left": "flow.dg.v.anti",
    unknown: "flow.dg.v.nosig",
};

/**
 * 接通页的数据。
 * @returns {{mode: string, diag: object|null, ok: boolean, stereo: boolean, blocked: boolean, offline: boolean, seen: Array, lane: string, other: string}}
 */
export function connectModel(state) {
    const c = state.connect || {};
    const stereo = !!state.device && state.device.layout === "stereo";
    let mode;
    if (SIGNAL_MODE[c.signalState]) mode = SIGNAL_MODE[c.signalState];
    else if (c.signalState !== "ok") mode = "first";
    else {
        mode = (stereo && PROMPT_MODE[c.channelPrompt]) || "ok";
        if (mode === "same" && c.channelPromptDismissed) mode = "ok";
    }
    const diag = DIAG[mode] || null;
    const offline = !!(state.device && state.device.offlineRender);
    const lane = laneOf(state);
    const rows = !diag
        ? []
        : diag.seen === "channels"
          ? [
                ["flow.dg.l.L", CHANNEL_VALUE[c.channelStatusL] || "flow.dg.v.nosig"],
                ["flow.dg.l.R", CHANNEL_VALUE[c.channelStatusR] || "flow.dg.v.nosig"],
            ]
          : diag.seen;
    return {
        mode,
        diag,
        ok: mode === "ok" || mode === "same",
        stereo,
        blocked: !!diag && diag.tone === "red" ? true : mode === "first" || offline,
        redBlock: !!diag && diag.tone === "red",
        offline,
        seen: rows,
        // 说明正文里的声道名：有声音（或较响）的那一路，和另一路
        lane: lane === "R" ? "right" : "left",
        other: lane === "R" ? "left" : "right",
        sec: Math.max(1, Math.round(c.callbackAgeSec || 0)),
    };
}

/** 横幅：宿主离线渲染、采样率变了；设备处理迹象只在安静步的结果页出现。 */
export function bannersFor(state, view) {
    const out = [];
    if (state.device && state.device.offlineRender) out.push({ tone: "amber", icon: "alert", key: "flow.bn.offline" });
    if (STEP_IDS.some((id) => state.steps && state.steps[id] && state.steps[id].invalidReason === "rate-changed"))
        out.push({ tone: "amber", icon: "alert", key: "flow.bn.sr" });
    const dev = state.results && state.results.deviceProcessing;
    if (view === "quiet" && dev && dev.detected)
        out.push({ tone: "amber", icon: "alert", key: "flow.bn.device", vars: { ms: Math.round(dev.longestZeroMs) } });
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// 准备页

const NEEDS_BANDS = new Set(["flow.ms.target"]);

/** @returns {{steps: object[], items: string[], icon: string, sec: number, words: object, vars: object}} */
export function prepModel(state, id) {
    const def = STEPS[id];
    const live = state.live || {};
    const step = (state.steps && state.steps[id]) || {};
    const how =
        id === "position"
            ? POSITION_PHASES.map((p) => ({ key: p.how, vars: { n: p.n }, list: p.list || null }))
            : def.how.map((key) => ({ key }));
    return {
        icon: def.icon,
        // 要用 bands 里的数才写得出来的条目：旧的原生端没有 bands 时整条不显示，不留“undefined”
        items: def.items.filter((key) => state.bands || !NEEDS_BANDS.has(key)),
        how,
        // 位置步写“推荐距离”（静态建议）；耳机麦没有，其余步骤也没有
        rec: id === "position" ? recDistance(state) : null,
        sec: Math.round(step.durationSec || 0),
        vars: {
            sec: Math.round(step.durationSec || 0),
            need: live.clapsNeeded,
            max: live.clapsMax,
            parts: POSITION_PHASES.length,
            dirs: DIRECTION_COUNT,
            others: DIRECTION_COUNT - 1,
            lo: state.bands && state.bands.tpQuietDbfs,
            hi: state.bands && state.bands.tpLoudDbfs,
        },
    };
}

// ---------------------------------------------------------------------------------------------------------------------
// 测试中（远距离模式）

const CLAP_SLOT = { valid: "valid", "too-soft": "soft", noisy: "noisy", truncated: "gate" };
const TAP_SLOT = { valid: "valid", clipped: "clip", unmeasured: "miss" };
/** 一行最多几个槽位（和房间步的 clapsMax 一样多）；轻拍多了只留最近的几下。 */
const SLOTS_MAX = 8;
const RESTART = "flow.rm.restart";
/** 还差什么才能点“结束”：live.need 到词条键（数字是 needLeft 向上取整）。 */
const NEED_KEY = {
    seconds: "flow.rm.need.seconds",
    "voice-seconds": "flow.rm.need.voice",
    syllables: "flow.rm.need.syllables",
};

/**
 * 这一段是不是由用户开始与结束（手动分段）：待开始，或者这一段有最低量。
 * 自动推进的原生端（宿主测试的探针、旧版本）need 是 none，界面照旧只有倒计时和“取消”。
 */
export const manualPhase = (live) => !!live && (live.waiting === true || (!!live.need && live.need !== "none"));

/**
 * 测试中视图的数据；没有步骤在跑且没有刚完成的步骤时返回 null。
 * 手动分段时多两样：待开始（live.waiting）给 go（大的“开始”），不倒数；进行中给 end（“结束”，收够最低量 canEnd 才能按）
 * 和 need（还差什么，或者“够了”）。
 * @param {object} state
 * @param {{justFinished?: string|null}} [o] 刚完成的步骤（界面短暂显示成功屏）
 */
export function remoteModel(state, o = {}) {
    const ids = visibleSteps(state);
    // 接通观察不进测试中视图，也不打断别的步骤做完后的成功屏：信号还不正常时它在任何一页都会自己开
    const running = ids.find((id) => id !== "connect" && statusOf(state, id) === "running");
    if (o.justFinished === "connect") return null;
    // 契约：快照里没有 bands 或 live（旧的原生端）就不进测试中视图，由准备页兜底
    if (!state.bands || !state.live) return null;
    const live = state.live;
    if (!running) {
        if (!o.justFinished) return null;
        const next = neighbour(state, o.justFinished, 1);
        return {
            struct: "success",
            tone: "green",
            step: o.justFinished,
            icon: "check",
            cmd: { key: "flow.rm.ok" },
            sub: { key: "flow.rm.next", vars: { name: { word: `flow.step.${STEP_KEY[next || REPORT]}` } } },
            bar: false,
        };
    }
    const step = state.steps[running];
    const stepNo = ids.indexOf(running) + 1;
    const total = live.phaseDurationSec > 0 ? live.phaseDurationSec : step.durationSec;
    const elapsed = live.phaseDurationSec > 0 ? live.phaseElapsedSec : step.progressSec;
    const count = Math.max(0, Math.ceil(total - elapsed));
    const m = { step: running, stepNo, count, tone: "violet", bar: true, zone: running === "gain" };
    const manual = manualPhase(live);
    const notice = live.notice;
    if (notice && notice !== "none" && notice !== "success") {
        m.tone = "red";
        m.count = Math.max(0, Math.round(live.noticeSec || 0));
        const redo = { key: RESTART, vars: { n: m.count } };
        if (live.waiting) {
            // 手动分段：这一段已退回待开始，不倒数，提示留到用户再点“开始”
            m.count = null;
            redo.key = "flow.rm.wait.redo";
            m.go = true;
        }
        const BY = {
            spoiled: { cmd: "flow.rm.redo", sub: "flow.rm.quiet.spoiled" },
            "clap-clipped": { cmd: "flow.rm.clap.soft", sub: "flow.rm.clap.clipped" },
            "too-quiet": { cmd: "flow.rm.again", sub: "flow.rm.plosive.quiet" },
            gated: { cmd: "flow.rm.room.void", sub: "flow.rm.room.gated" },
            redo: { cmd: "flow.rm.redo.gen", sub: null },
            // 轻拍到上限有效的还不够、又不是削波刷掉的（手动分段才有）：原生给的是通用的 redo
            fewTaps: { cmd: "flow.rm.clap.again", sub: "flow.rm.clap.few" },
        };
        const by =
            (notice === "redo" && running === "position" && tapsPhase(live) ? BY.fewTaps : BY[notice]) || BY.redo;
        m.struct = `notice:${notice}${live.waiting ? ":wait" : ""}`;
        m.cmd = { key: by.cmd };
        m.sub = by.sub ? { key: by.sub } : null;
        m.sub2 = redo;
        if (notice === "gated") m.slots = clapSlots(live);
        return m;
    }
    m.struct = `${running}:${live.phase}`;
    if (running === "gain") Object.assign(m, { cmd: { key: "flow.rm.gain.cmd" }, sub: { key: "flow.rm.gain.sub" } });
    else if (running === "quiet")
        Object.assign(m, { cmd: { key: "flow.rm.quiet.cmd" }, sub: { key: "flow.rm.quiet.sub" } });
    else if (running === "room") {
        const valid = (live.claps || []).filter((c) => c === "valid").length;
        Object.assign(m, {
            cmd: { key: "flow.rm.room.cmd" },
            sub: { key: "flow.rm.room.sub" },
            slots: clapSlots(live),
            // 手动分段时由 need 写“有效几下 / clapsNeeded”（见 countedPart），凑够就自动收尾
            aux: manual ? null : { key: "flow.rm.need", vars: { n: Math.max(0, live.clapsNeeded - valid) } },
        });
    } else if (running === "position") {
        const at = POSITION_PHASES.findIndex((p) => p.id === live.phase);
        const p = POSITION_PHASES[at < 0 ? 0 : at];
        // 手动分段时每个部分是一段（字表的几遍一口气念完、几下轻拍一起拍），只写第几部分，不画遍数的点
        const byPart = manual || p.aux === "part";
        const auxVars = byPart ? { n: at + 1, total: POSITION_PHASES.length } : { n: live.round, total: live.rounds };
        const taps = manual && !!p.taps && Array.isArray(live.taps);
        Object.assign(m, {
            cmd: { key: p.cmd, vars: { n: manual ? (taps && live.tapsNeeded) || p.n : live.rounds } },
            words: p.words || null,
            aux: { key: byPart ? "flow.rm.part" : "flow.rm.round", vars: auxVars },
        });
        if (p.taps && !manual) m.dots = { total: live.rounds, on: Math.max(0, live.round - 1), cur: live.round };
        // 手动分段的轻拍：每一下都有反馈（有效、拍轻一点、没量出），待开始时先画出要凑的几下
        if (taps) Object.assign(m, { slots: tapSlots(live), slotsKind: "taps" });
    } else if (running === "direction") {
        if (live.phase === "confirm") {
            const pair = confirmPair(state);
            Object.assign(m, {
                cmd: {
                    key: pair.b ? "flow.rm.dir.confirm" : "flow.rm.dir.confirm.gen",
                    vars: { sec: Math.round(live.phaseDurationSec), ...pair },
                },
                words: pair.b ? "flow.rm.dir.pair" : "flow.rm.dir.pair.gen",
                wordsVars: pair,
                sub: { key: "flow.rm.dir.sub" },
            });
        } else {
            const i = Math.min(DIRECTION_HINT.length - 1, Math.max(0, live.directionIndex));
            Object.assign(m, {
                cmd: {
                    key: "flow.rm.dir.cmd",
                    vars: { d: { word: DIRECTION_HINT[i] }, sec: Math.round(live.phaseDurationSec) },
                },
                words: "flow.rm.dir.words",
                dots: { total: live.rounds, on: i, cur: i + 1 },
                aux: { key: "flow.rm.dir.n", vars: { n: i + 1, total: live.rounds } },
            });
        }
    }
    if (live.waiting) {
        // 待开始：先说下一段要做什么，等用户点“开始”；不倒数，等多久都行
        m.struct += ":wait";
        m.count = null;
        m.sub2 = { key: "flow.rm.wait" };
        m.go = true;
    } else if (manual) {
        m.end = { can: live.canEnd === true };
        const counted = countedPart(running, live);
        m.need = counted
            ? { key: "flow.rm.count", vars: counted }
            : live.canEnd
              ? { key: "flow.rm.need.ready" }
              : {
                    key: NEED_KEY[live.need] || "flow.rm.need.more",
                    vars: { n: Math.max(1, Math.ceil(Number(live.needLeft) || 0)) },
                };
    }
    return m;
}

/**
 * 凑够数就自己结束的部分（房间的拍手、位置步的轻拍）：进行中只写“有效 n / 总数”，总数是自动结束要的下数。
 * 收够最低量后“结束”照样能按，但不另说“够了”：再拍一下就自动结束了，说“随时可以点结束”反而让人以为要回来点。
 * 其余按最低量结束的部分（按秒、按字）没有自动结束，仍然写“够了”。
 */
function countedPart(running, live) {
    const valid = (list) => (list || []).filter((c) => c === "valid").length;
    if (running === "room" && Number.isFinite(live.clapsNeeded))
        return { n: valid(live.claps), total: live.clapsNeeded };
    if (running === "position" && tapsPhase(live) && Number.isFinite(live.tapsNeeded))
        return { n: valid(live.taps), total: live.tapsNeeded };
    return null;
}

/**
 * 方向确认要重唱的两个方向，用“方向 n / 总数”里的编号 n 指出：a 是开始时面对的方向，b 是更响的那个。
 * b 取自第二遍的 directionIndex，或结果里的 repeatIndex；都还不知道时为 null，由通用文案兜底。
 */
export function confirmPair(state) {
    const live = state.live || {};
    const d = (state.results || {}).direction;
    const a = d && Number.isInteger(d.currentIndex) && d.currentIndex >= 0 ? d.currentIndex + 1 : 1;
    let b = null;
    if (live.round >= 2 && live.directionIndex > 0) b = live.directionIndex + 1;
    else if (d && Number.isInteger(d.repeatIndex) && d.repeatIndex >= 0) b = d.repeatIndex + 1;
    return { a, b };
}

/** 位置步当前这一部分是不是逐下判定的那一部分（步骤表里的 taps）。 */
const tapsPhase = (live) => !!(POSITION_PHASES.find((p) => p.id === live.phase) || {}).taps;

/** 轻拍槽位：听到的每一下按判定，再补上还差几下有效的“待拍”；一行放不下时只留最近的几下。 */
export function tapSlots(live) {
    const heard = (live.taps || []).map((t) => TAP_SLOT[t] || "miss");
    const valid = heard.filter((s) => s === "valid").length;
    const pending = Math.max(0, Math.min(SLOTS_MAX, (live.tapsNeeded || 0) - valid));
    return [...heard.slice(Math.max(0, heard.length - (SLOTS_MAX - pending))), ...Array(pending).fill("wait")];
}

/** 拍手槽位：已拍的按判定，没拍的补成“待拍”，总数是 clapsMax。 */
export function clapSlots(live) {
    const heard = (live.claps || []).map((c) => CLAP_SLOT[c] || "wait");
    const pending = Math.max(0, (live.clapsMax || heard.length) - heard.length);
    return [...heard, ...Array.from({ length: pending }, () => "wait")];
}

// ---------------------------------------------------------------------------------------------------------------------
// 结果页

/** 为什么要再测一次：重测原因到词条键。 */
const WHY = {
    "few-claps": "flow.cf.why.fewclaps",
    "low-pnr": "flow.cf.why.tail",
    "gate-detected": "flow.cf.why.device",
    "device-processing": "flow.cf.why.device",
    "voice-detected": "flow.cf.why.voice",
};
const sec = (r) => ({
    tone: r.tone,
    stale: !!r.stale,
    retest: r.reliability === "retest",
    why: WHY[r.retestReason] || null,
});
const num = (text, unit, unitKey) => ({ text, unit, unitKey });
/** 房间读不出来时没有原因的兜底：设备处理，否则是拍手不够。 */
const roomWhy = (rv) => (rv.gated ? "flow.cf.why.device" : "flow.cf.why.fewclaps");

/**
 * 齿音两个频段的显示范围（Hz），与 thresholds.json 的 sibilance.mid_high_band_hz、high_band_hz 同值（冒烟逐项核对）。
 * 只用来写卡片上的频段标签，判据在原生侧。
 */
export const SIB_BANDS_HZ = [
    [3000, 6000],
    [6000, 12000],
];
export const kHz = (v) => `${+(v / 1000).toFixed(1)}`;
export const hzRange = (b) => `${kHz(b[0])}–${kHz(b[1])} kHz`;
/** 口水音检测前的高通频率（Hz），与 thresholds.json 的 click.highpass_hz 同值（冒烟核对）；“最响一次”是它之后的峰值。 */
export const CLICK_HIGHPASS_HZ = 2000;
/** 口水音按多少秒说话算的（取整后的文字）；没有、或取整后是 0 时为 null，不写秒数。 */
export const speechSeconds = (c) => {
    const s = Number.isFinite(c.analysedSeconds) ? Math.round(c.analysedSeconds) : 0;
    return s > 0 ? String(s) : null;
};
/** 齿音能和元音比：高频项没关、有带元音参考的擦音、比值是个数。卡上的主数字与参考值比的都是这个比值。 */
export const sibVowel = (sb) => !sb.highBandDisabled && sb.vowelEvents > 0 && Number.isFinite(sb.highOverVowelDb);

// ---------------------------------------------------------------------------------------------------------------------
// 参考值、轻提示与推荐距离

const cm = (v) => `${+v.toFixed(1)}`;
const refNum = (v) => (v < 0 ? "−" : "") + +Math.abs(v).toFixed(1);

/**
 * 推荐距离：静态的常见做法，不是测量值。取自快照根上的 recommendedDistance，数值一律来自它，这里不写死。
 * 耳机麦（null）或字段不全时返回 null，不显示。两端相等写一个数，不等写一段；范围与“约”的两端相同时不重复写范围。
 */
export function recDistance(state) {
    const d = state && state.recommendedDistance;
    if (!d || !["aroundLowCm", "aroundHighCm", "minCm", "maxCm"].every((k) => Number.isFinite(d[k]))) return null;
    const a = d.aroundLowCm === d.aroundHighCm ? cm(d.aroundLowCm) : `${cm(d.aroundLowCm)}–${cm(d.aroundHighCm)}`;
    return d.minCm === d.aroundLowCm && d.maxCm === d.aroundHighCm
        ? { key: "flow.rd", vars: { a } }
        : { key: "flow.rd.r", vars: { a, min: cm(d.minCm), max: cm(d.maxCm) } };
}

/**
 * 参考值一行（经验值，不是档位）：数值取自 bands，没有（旧的原生端，或参考还没定的 null）就不显示。
 * 齿音比的是 6-12 kHz 比元音，就是卡上的主数字，参考行只写参考值；高频项关闭或没有元音可比时不比，也不显示参考。
 */
const plosiveRef = (p, bands) =>
    bands && Number.isFinite(bands.plosiveReferenceDb)
        ? { key: "flow.ref.plosive", vars: { v: refNum(bands.plosiveReferenceDb) }, over: !!p.overReference }
        : null;
const sibRef = (sb, bands) =>
    bands && Number.isFinite(bands.sibilanceReferenceDb) && sibVowel(sb)
        ? { key: "flow.ref.sib", vars: { v: refNum(bands.sibilanceReferenceDb) }, over: !!sb.overReference }
        : null;
const clickRef = (c, bands) =>
    bands && Number.isFinite(bands.clickReferencePerMin)
        ? { key: "flow.ref.mouth", vars: { v: refNum(bands.clickReferencePerMin) }, over: !!c.overReference }
        : null;

/** 超过参考值的轻提示：建议键到词条后缀。没有推荐距离（耳机麦）时，带“退后一点”的建议只说不涉及距离的部分。 */
const TIP_ID = {
    "pop-filter-or-back-off": (noDist) => (noDist ? "pop-filter" : "pop-filter-or-back-off"),
    "angle-soften-de-ess": () => "angle-soften-de-ess",
    "back-off-a-little": (noDist) => (noDist ? "back-off-a-little.nodist" : "back-off-a-little"),
    "drink-water": () => "drink-water",
};

/** 带“稍离远”的完整提示：详情里要把推荐距离范围一起写出来。 */
const TIP_NEEDS_REC = new Set(["pop-filter-or-back-off", "back-off-a-little"]);

/**
 * 这一张卡的参考行与轻提示的来源：bands、各项建议（只有当前的结果才带，上次的不带）、有没有推荐距离。
 * @param {boolean} [current] 画的是当前结果（不是 previous）
 */
export const cardExtras = (state, current = true) => ({
    bands: state && state.bands,
    advice: (current && state && state.advice) || {},
    noDistance: !recDistance(state),
});

/** 参考行与轻提示：过期或要再测的结果不比、不提示（建议本来就只看当前、可信的结果，这里再挡一道）。 */
function voiceExtras(r, ref, advice, x) {
    const live = !r.stale && r.reliability !== "retest";
    // advice 来自原生端：只认自己的键（不认 constructor、__proto__ 这类原型上的名字）
    const mapTip = live && advice && Object.hasOwn(TIP_ID, advice) ? TIP_ID[advice] : null;
    const id = mapTip ? mapTip(x.noDistance) : null;
    return { ref: ref && { ...ref, over: live && ref.over }, tip: id ? { id, needsRec: TIP_NEEDS_REC.has(id) } : null };
}

/** 四张卡都不分档：卡片无色，不管原生给了什么色调。 */
const plain = (r) => ({ ...sec(r), tone: "none" });

/** 区间条：竖条从上到下，segs 是 [下界, 上界, 色, 词条键]，value 是当前值。 */
function bandsViz(dom, segs, value, text, unit, titleKey) {
    return { kind: "bands", dom, segs, value, text, unit, title: titleKey };
}
const CLIP_BAND_DB = 3; // 增益区间条在 0 dBTP 之上多画一段“削波”，只是示意的高度

export function gainViz(bands, tp) {
    const { tpTooQuietDbfs: a, tpQuietDbfs: b, tpLoudDbfs: c, tpTooLoudDbfs: d } = bands;
    const lo = a - (b - a);
    const segs = [
        [0, CLIP_BAND_DB, "red", "flow.w.clipped"],
        [d, 0, "red", "flow.w.over"],
        [c, d, "amber", "flow.w.hot"],
        [b, c, "green", "flow.w.fit"],
        [a, b, "amber", "flow.w.low"],
        [lo, a, "red", "flow.w.toolow"],
    ];
    return bandsViz([lo, CLIP_BAND_DB], segs, tp, (tp > 0 ? "+" : "") + fmtDb(tp), "dBTP", "flow.ms.target");
}
export function snrViz(bands, snr) {
    const { snrBadBelowDb: lo, snrGoodAboveDb: hi } = bands;
    const pad = (hi - lo) * 1.5;
    const dom = [lo - pad, hi + pad];
    const segs = [
        [hi, dom[1], "green", "flow.w.snr.good"],
        [lo, hi, "amber", "flow.w.snr.mid"],
        [dom[0], lo, "red", "flow.w.snr.bad"],
    ];
    return bandsViz(dom, segs, snr, fmtDb(snr), "dB(A)", "flow.ms.snr");
}
export function t20Viz(bands, t) {
    const { t20DryBelowS: a, t20NormalBelowS: b, t20BoomyBelowS: c } = bands;
    const top = c + (c - b) * 1.2;
    const segs = [
        [c, top, "red", "flow.w.verymixed"],
        [b, c, "amber", "flow.w.mixed"],
        [a, b, "green", "flow.w.normal"],
        [0, a, "green", "flow.w.dry"],
    ];
    return bandsViz([0, top], segs, t, t.toFixed(2), "s", "flow.ms.t20");
}

const hz = (v) => (v >= 1000 ? `${+(v / 1000).toFixed(1)} kHz` : `${v} Hz`);
const act = (key, vars) => ({ key, vars });

/** 重唱确认成立时（confirmedAdvantageDb 有值）用两次里较小的优势；否则是第一遍里最响的其他方向的优势。 */
const directionAdvantage = (d) =>
    Number.isFinite(d.confirmedAdvantageDb)
        ? d.confirmedAdvantageDb
        : Number.isFinite(d.bestOtherAdvantageDb)
          ? d.bestOtherAdvantageDb
          : 0;

/** 没有电流声结果（噪声步做了、电流声分析没给）时按“没检出”画一张卡。 */
const NO_HUM = {
    kind: "none",
    slight: false,
    overFloorDb: 0,
    energySharePct: 0,
    fundamentalHz: 0,
    tone: "green",
    reliability: "trusted",
    retestReason: "none",
};

/**
 * 结果卡的构造：结果页与报告共用，所以“上次”（snapshot.previous）也能用同一套画出来。
 * 参数 r 是一个区段（results 或 previous 里的）；state 只用来取被分析的那一路与 live。
 * 齿音、口水音不分档：tone 为 none 时卡片无色，词是“未分档”，不是“正常”或“少”。
 */
export const resultCards = {
    level(state, g) {
        const tp = truePeak(state, g);
        const clipN = (g.digitalClipRunsL || 0) + (g.digitalClipRunsR || 0);
        const flatN = (g.flatPlateauPeaksL || 0) + (g.flatPlateauPeaksR || 0);
        return {
            id: "level",
            word: gainWord(g),
            nums: [num((tp > 0 ? "+" : "") + fmtDb(tp), "dBTP")],
            note: g.digitalClip
                ? act("flow.cd.clip", { n: clipN })
                : g.maybeFlatTop
                  ? act("flow.cd.flat", { n: flatN })
                  : null,
            ...sec(g),
        };
    },
    noise: (nz) => ({
        id: "noise",
        word: noiseWord(nz),
        nums: [num(fmtDb(nz.snrDb), "dB(A)")],
        note: null,
        ...sec(nz),
    }),
    hum(hm) {
        const h = hm || NO_HUM;
        const hv = { f: Math.round(h.fundamentalHz), p: Math.round(h.energySharePct) };
        return {
            id: "hum",
            word: humWord(h),
            nums: [num(h.kind === "none" ? "0.0" : signed(h.overFloorDb), "dB")],
            note: h.kind === "none" ? null : act("flow.cd.hum.note", hv),
            ...sec(h),
        };
    },
    /**
     * 作废太多、给不出 T20，或原生要再测（confidence 为 retest，这时 band 仍可能写着 normal）：无色、不写档位、写原因。
     * 设备的降噪或噪声门作废时即使原生没给原因，也写设备原因。
     */
    room(state, rv, failed = rv.gated || rv.tMidSec == null || rv.reliability === "retest") {
        const need = (state.live || {}).clapsNeeded;
        if (failed)
            return {
                id: "room",
                word: "flow.w.unk",
                nums: [],
                none: true,
                tone: "gray",
                stale: false,
                retest: true,
                why:
                    WHY[rv.retestReason] ||
                    (rv.reliability === "retest" && !rv.gated && rv.tMidSec != null
                        ? "flow.cf.why.unsure"
                        : roomWhy(rv)),
                need,
            };
        return {
            id: "room",
            word: reverbWord(rv),
            nums: [num(rv.tMidSec.toFixed(2), "s")],
            note: null,
            need,
            ...sec(rv),
        };
    },
    dr: (dr) => ({
        id: "dr",
        word: UNGRADED_WORD,
        nums: [num(signed(dr.drDb), "dB")],
        note: act(dr.lowerBoundOnly ? "flow.cd.dr.lower" : "flow.cd.dr.note"),
        ...plain(dr),
    }),
    plosive: (p, x = cardExtras()) => ({
        id: "plosive",
        word: UNGRADED_WORD,
        nums: [num(signed(p.worstRatioDb), "dB")],
        note: null,
        ...voiceExtras(p, plosiveRef(p, x.bands), x.advice.plosive, x),
        ...plain(p),
    }),
    /**
     * 主数字是 6-12 kHz 比元音，和参考值比的就是它；两个频段的绝对电平随增益和距离变，单看没有好坏，只放在详情里。
     * 比不了（高频项关闭，或没有带元音参考的擦音）时退回一个频段的电平，带上频段标签，说明写为什么比不了。
     */
    sib(sb, x = cardExtras()) {
        const vowel = sibVowel(sb);
        const hi = !sb.highBandDisabled;
        const main = vowel
            ? {
                  ...num(signed(sb.highOverVowelDb), "dB"),
                  label: act("flow.cd.sib.vowel", { b: hzRange(SIB_BANDS_HZ[1]) }),
              }
            : {
                  ...num(signed(hi ? sb.highDb : sb.midHighDb), "dBFS"),
                  label: act("flow.cd.sib.level", { b: hzRange(SIB_BANDS_HZ[hi ? 1 : 0]) }),
              };
        return {
            id: "sib",
            word: UNGRADED_WORD,
            nums: [main],
            // “上次 → 现在”比主数字那个量：比值对比值、同一频段的电平对电平；两次的主数字不是同一个量就不比
            cmp: vowel ? { by: "vowel", v: main.text } : { by: hi ? "high" : "mid", v: main.text },
            note: !hi
                ? act("flow.cd.sib.bw", { bw: kHz(sb.effectiveBandwidthHz) })
                : vowel
                  ? null
                  : act("flow.cd.sib.novowel"),
            ...voiceExtras(sb, sibRef(sb, x.bands), x.advice.sibilance, x),
            ...plain(sb),
        };
    },
    /** 说明写口水音按多少秒说话算的（两张字表加自然念的总长）；“最响一次”只在详情里。 */
    mouth: (c, x = cardExtras()) => ({
        id: "mouth",
        word: UNGRADED_WORD,
        nums: [num(String(Math.round(c.perMinute)), null, "flow.u.perMin")],
        note: speechSeconds(c) ? act("flow.cd.mouth.note", { s: speechSeconds(c) }) : null,
        ...voiceExtras(c, clickRef(c, x.bands), x.advice.click, x),
        ...plain(c),
    }),
    dir(d) {
        const advantage = directionAdvantage(d);
        return {
            id: "dir",
            word: dirWord(d),
            nums: [num(signed(advantage), "dB")],
            note: act("flow.cd.dir.note"),
            ...sec(d),
        };
    },
};

/** 距离的几种情形（桥契约的 DistanceCase）：只有第一次的弱提示与重测后的前后对比。 */
const DISTANCE_KINDS = ["unknown", "conflicting", "maybe-too-far", "closer", "farther", "no-change"];

/**
 * 距离的读法：kind 不认识（旧的原生端）按“还不够判断”写。更近、更远的句子要写“几项读数指向它”：
 * 项数缺失、不是数或不到 1 项时也按“还不够判断”写，免得露出占位符，或写成“有迹象，但 0 项读数指向它”。
 * shown 是句子的词条后缀（flow.pr.e.<shown>），n 是项数。
 */
export function distanceReading(d) {
    const kind = d && DISTANCE_KINDS.includes(d.kind) ? d.kind : "unknown";
    const n = kind === "closer" ? d.closeEvidence : kind === "farther" ? d.farEvidence : null;
    const shown = (kind === "closer" || kind === "farther") && !(n >= 1) ? "unknown" : kind;
    return { kind, shown, n };
}

/**
 * 一页结果的数据。没有对应结果时返回 null（调用方回退到准备页）。
 * 缺 bands（旧的原生端）时结果页照常画读数，只是没有区间条和依赖档位边界的建议，不退回准备页。
 * @returns {{cards: object[], cols: number, side: boolean, expl: object, acts: object[], viz: object|null, ctas: object[], bars?: Array}}
 */
export function resultModel(state, id) {
    const r = state.results || {};
    const bands = state.bands;
    const next = neighbour(state, id, 1);
    const nextKey =
        next === REPORT
            ? "flow.btn.next.report"
            : next === "direction"
              ? "flow.btn.next.dir"
              : `flow.btn.next.${STEP_KEY[next]}`;
    const goNext = { key: nextKey, to: next, icon: "arrow-r" };
    const retest = (k) => ({ key: `flow.btn.retest.${k}`, retest: id, icon: "retry" });
    // 依赖档位边界的说明和建议：没有 bands 时整条不写
    const needB = (a) => (bands ? [a] : []);
    const noBands = act("flow.rs.nobands");
    if (id === "gain" && r.gain) {
        const g = r.gain;
        const tp = truePeak(state, g);
        const target = bands && bands.targetPeakDbfs;
        const n = Math.round(Math.abs(target - tp));
        const clipA = (key) => needB(act(key, { n: bands && bands.clipAdviceDropDb }));
        let expl;
        let acts;
        if (g.digitalClip) {
            const usb = state.micType === "usb";
            expl = act(usb ? "flow.gr.clip.usb.e" : "flow.gr.clip.e");
            acts = [...clipA(usb ? "flow.gr.clip.usb.a" : "flow.gr.clip.a"), act("flow.gr.retest")];
        } else if (g.maybeFlatTop) {
            expl = act("flow.gr.flat.e");
            acts = [...clipA("flow.gr.clip.a"), act("flow.gr.retest")];
        } else if (g.band === "good") {
            expl = bands
                ? act("flow.gr.green.e", { lo: fmtDb(bands.tpQuietDbfs, 0), hi: fmtDb(bands.tpLoudDbfs, 0) })
                : noBands;
            acts = [act("flow.gr.green.a")];
        } else if (g.band === "quiet" || g.band === "too-quiet") {
            expl = bands
                ? act(g.band === "quiet" ? "flow.gr.low.e" : "flow.gr.toolow.e", { target: fmtDb(target, 0) })
                : noBands;
            acts = [...needB(act("flow.gr.low.a", { n })), act("flow.gr.retest")];
        } else {
            expl = bands ? act("flow.gr.hot.e", { target: fmtDb(target, 0) }) : noBands;
            acts = [...needB(act("flow.gr.hot.a", { n })), act("flow.gr.retest")];
        }
        const ok = g.band === "good" && !g.digitalClip && !g.maybeFlatTop;
        return {
            cards: [resultCards.level(state, g)],
            cols: 1,
            side: false,
            expl,
            acts,
            viz: bands ? gainViz(bands, tp) : null,
            ctas: ok ? [{ ...goNext, primary: true }] : [{ ...retest("gain"), primary: true }, goNext],
        };
    }
    if (id === "quiet" && r.noise) {
        const nz = r.noise;
        const hm = r.hum || NO_HUM;
        const hv = { f: Math.round(hm.fundamentalHz), v: fmtDb(hm.overFloorDb), p: Math.round(hm.energySharePct) };
        const dev = r.deviceProcessing && r.deviceProcessing.detected;
        let expl;
        let acts;
        if (dev) {
            expl = act("flow.qr.dev.e");
            acts = [act("flow.qr.dev.a1")];
        } else if (hm.kind === "hum" && hm.slight) {
            expl = act("flow.qr.humweak.e", hv);
            acts = [act("flow.qr.humweak.a1")];
        } else if (hm.kind === "hum") {
            expl = act("flow.qr.hum.e", hv);
            acts = [act("flow.qr.hum.a1"), act("flow.qr.hum.a2")];
        } else if (hm.kind === "buzz") {
            expl = act("flow.qr.buzz.e");
            acts = [act("flow.qr.buzz.a1"), act("flow.qr.buzz.a2")];
        } else if (hm.kind === "whistle") {
            expl = act("flow.qr.whine.e");
            acts = [act("flow.qr.whine.a1"), act("flow.qr.whine.a2")];
        } else {
            expl = act("flow.qr.none.e", { w: { word: noiseWord(nz) } });
            acts = nz.band === "bad" ? [act("flow.qr.bad.a1")] : [];
        }
        const noiseCard = resultCards.noise(nz);
        return {
            cards: [
                dev ? { ...noiseCard, retest: true, why: "flow.cf.why.device" } : noiseCard,
                resultCards.hum(r.hum || { ...NO_HUM, stale: nz.stale }),
            ],
            cols: 2,
            side: false,
            expl,
            acts,
            viz: bands ? snrViz(bands, nz.snrDb) : null,
            ctas: [{ ...goNext, primary: true }, retest("quiet")],
        };
    }
    if (id === "room" && r.reverb) {
        const rv = r.reverb;
        const unreadable = statusOf(state, "room") === "invalid" || rv.gated || rv.tMidSec == null;
        // 有读数但原生要再测：不下结论，不写档位和房间建议；原因不一定是拍手不够，说明句只说“不够确定”
        const failed = unreadable || rv.reliability === "retest";
        const live = state.live || {};
        if (failed) {
            const claps = clapSlots({ claps: live.claps, clapsMax: (live.claps || []).length });
            const valid = (live.claps || []).filter((c) => c === "valid").length;
            return {
                cards: [resultCards.room(state, rv, true)],
                cols: 1,
                side: false,
                expl: unreadable
                    ? act("flow.rf.e", { total: (live.claps || []).length, valid, need: live.clapsNeeded })
                    : act("flow.rf.e.retest"),
                acts: [act("flow.rf.a1"), act("flow.rf.a2"), act("flow.rf.a3")],
                viz: { kind: "slots", slots: claps, title: "flow.ms.t20" },
                ctas: [{ ...retest("room"), primary: true }, goNext],
            };
        }
        const labels = ((bands && bands.t20BandsHz) || []).map(hz);
        const tmax = Math.max(...rv.t20Sec, 0.001);
        const boomy = rv.band === "boomy" || rv.band === "very-boomy";
        return {
            cards: [resultCards.room(state, rv, false)],
            cols: 1,
            side: false,
            expl: act(`flow.rr.e.${rv.band}`, { t: rv.tMidSec.toFixed(2), b1: labels[0] || "", b2: labels[1] || "" }),
            acts: boomy ? [act("flow.rr.a1"), act("flow.rr.a2")] : [act("flow.rr.a.ok")],
            viz: bands ? t20Viz(bands, rv.tMidSec) : null,
            bars: rv.t20Sec.map((v, i) => ({
                label: labels[i] || "",
                text: v.toFixed(2),
                width: Math.round((v / tmax) * 100),
            })),
            below: labels[0] ? act("flow.rr.below", { b: labels[0] }) : null,
            ctas: [{ ...goNext, primary: true }, retest("room")],
        };
    }
    if (id === "position" && r.directRatio) {
        const dr = r.directRatio;
        // 距离只有前后对比与一条弱提示，不给绝对判断
        const { kind, shown, n } = distanceReading(r.distance);
        const x = cardExtras(state);
        const cards = [
            resultCards.dr(dr),
            r.plosive && resultCards.plosive(r.plosive, x),
            r.sibilance && resultCards.sib(r.sibilance, x),
            r.click && resultCards.mouth(r.click, x),
        ].filter(Boolean);
        return {
            cards,
            cols: 2,
            side: true,
            expl: act(`flow.pr.e.${shown}`, { n }),
            // 往哪边挪由原生侧的建议决定：只有“可能离远了”会给 move-closer；超过参考值的轻提示按卡的顺序接在后面
            acts: [
                ...(kind === "maybe-too-far" && (state.advice || {}).distance === "move-closer"
                    ? [act("flow.pr.a.closer")]
                    : []),
                ...cards.filter((c) => c.tip).map((c) => act(`flow.tip.${c.tip.id}`)),
            ],
            rec: recDistance(state),
            viz: null,
            ctas: [{ ...goNext, primary: true }, retest("pos")],
        };
    }
    if (id === "direction" && r.direction) {
        const d = r.direction;
        const advantage = directionAdvantage(d);
        const loudest = d.levelDb.indexOf(Math.max(...d.levelDb)) + 1;
        const by = {
            "maybe-reversed": {
                e: act("flow.dr1.e", { v: fmtDb(advantage), n: loudest }),
                a: [act("flow.dr1.a1"), act("flow.dr1.a2")],
            },
            "small-difference": { e: act("flow.dr2.e"), a: [act("flow.dr2.a1")] },
            best: { e: act("flow.dr.best.e"), a: [] },
            uncertain: { e: act("flow.dr.unsure.e"), a: [] },
        }[d.kind] || { e: act("flow.dr.unsure.e"), a: [] };
        // 雷达的顺序是“当前方向、转 90°、转 180°、转 270°”：把当前方向转到第一位
        const cur = Number.isInteger(d.currentIndex) && d.currentIndex >= 0 ? d.currentIndex : 0;
        const level = d.levelDb.map((_, k) => d.levelDb[(cur + k) % d.levelDb.length]);
        const top = Math.max(...level);
        return {
            cards: [resultCards.dir(d)],
            cols: 1,
            side: false,
            expl: by.e,
            acts: by.a,
            viz: { kind: "radar", rel: level.map((v) => v - top) },
            ctas: [{ ...goNext, primary: true }, retest("dir")],
        };
    }
    return null;
}
