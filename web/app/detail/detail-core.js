// SPDX-License-Identifier: GPL-3.0-or-later
//
// 详情面板的纯逻辑：一张结果卡点开后，面板里写什么。不碰 DOM，Node 里可以直接 import 做测试。
// 数字都来自快照里这张卡对应的结果区段；阈值只用快照的 bands，缺 bands（旧的原生端）时对应的句子省略，不写死。
//
// detailOf(id, state) 返回：
//   card    这张卡本身（与报告页的卡同一个对象，表头直接用它的词、数字、过期）
//   expl    一句话解释 {key, vars}
//   cols    表格的数值列标题（第一列固定是“项目”）；rows 为空时不画表
//   rows    [{label: {key, vars} | {lit}, abs, rel, hl}]，abs 与 rel 是格子 {text, unit?, unitKey?} 或 null，hl 是高亮行
//   notes   说明句 [{key, vars}]
//   radar   方向详情的四点雷达 {rel}
//   evidence 要画证据画布时 {step, kinds, focus, rev}，不画（这张卡不画，或这一步没有证据）是 null
//   cta     过期、要再测或没测过时的按钮 {key, step, prep}

import { fmtDb } from "../meter/meter-core.js";
import {
    CLICK_HIGHPASS_HZ,
    SIB_BANDS_HZ,
    hzRange,
    kHz,
    laneOf,
    resultModel,
    signed,
    truePeak,
} from "../flow/flow-core.js";
import { STEP_OF, cardOf } from "../report/report-core.js";
import { evidenceSpec } from "./evidence-core.js";

const A = (key, vars) => ({ key, vars });
const cell = (text, unit, unitKey) => ({ text, unit, unitKey });
const dB = (x) => cell(signed(x), "dB");
const level = (x, unit = "dBFS") => cell(fmtDb(x), unit);
const times = (n) => cell(String(Math.round(n)), null, "detail.u.times");
const row = (key, abs, rel = null, hl = false, vars) => ({ label: A(key, vars), abs, rel, hl });
const fin = Number.isFinite;
const hzText = (f) => (f >= 1000 ? `${+(f / 1000).toFixed(1)} kHz` : `${Math.round(f)} Hz`);
const SIB_LABELS = SIB_BANDS_HZ.map(hzRange);

const ABS = "detail.col.abs";
const BODY = {
    level(state, r) {
        const g = r.gain;
        const b = state.bands;
        const tp = truePeak(state, g);
        const clip = (g.digitalClipRunsL || 0) + (g.digitalClipRunsR || 0);
        const flat = (g.flatPlateauPeaksL || 0) + (g.flatPlateauPeaksR || 0);
        return {
            expl: b
                ? A("detail.level.e", {
                      v: fmtDb(tp),
                      lo: fmtDb(b.tpQuietDbfs),
                      hi: fmtDb(b.tpLoudDbfs),
                      c: fmtDb(b.targetPeakDbfs),
                  })
                : A("detail.level.e0", { v: fmtDb(tp) }),
            cols: [ABS, "detail.col.target"],
            rows: [
                row("detail.r.tp", level(tp, "dBTP"), b ? dB(tp - b.targetPeakDbfs) : null, true),
                row("detail.r.sp", level(g[`samplePeakDbfs${laneOf(state)}`])),
                row("detail.r.clip", times(clip)),
                ...(g.maybeFlatTop ? [row("detail.r.flat", times(flat))] : []),
            ],
        };
    },
    noise(state, r) {
        const n = r.noise;
        const b = state.bands;
        return {
            expl: A("detail.noise.e", { v: fmtDb(n.snrDb) }),
            cols: [ABS, "detail.col.speech"],
            rows: [
                row("detail.r.nA", level(n.noiseDbfsA, "dBFS(A)"), cell(fmtDb(n.snrDb), "dB"), true),
                row("detail.r.nU", level(n.noiseDbfs)),
            ],
            notes: [
                A("detail.n.dbfs"),
                ...(b ? [A("detail.n.noise", { lo: fmtDb(b.snrBadBelowDb), hi: fmtDb(b.snrGoodAboveDb) })] : []),
            ],
        };
    },
    hum(state, r) {
        const h = r.hum;
        if (!h || h.kind === "none") return { expl: A("detail.hum.none") };
        return {
            expl: A("detail.hum.e", {
                f: Math.round(h.fundamentalHz),
                v: fmtDb(h.overFloorDb),
                p: Math.round(h.energySharePct),
            }),
            cols: [ABS, "detail.col.floor"],
            rows: [
                row("detail.r.f1", cell(String(Math.round(h.fundamentalHz)), "Hz"), dB(h.overFloorDb), true),
                row("detail.r.fh", cell(String(Math.round(h.highestHarmonicHz)), "Hz")),
                row("detail.r.share", cell(String(Math.round(h.energySharePct)), "%")),
            ],
            notes: [A("detail.n.hum")],
        };
    },
    room(state, r) {
        const v = r.reverb;
        const hzs = (state.bands && state.bands.t20BandsHz) || [];
        if (v.gated || !fin(v.tMidSec)) return { expl: A("detail.room.none") };
        const per = (v.t20Sec || []).flatMap((t, i) =>
            t > 0 && fin(hzs[i])
                ? [
                      row("detail.r.band", cell(t.toFixed(2), "s"), cell(signed(t - v.tMidSec, 2), "s"), false, {
                          hz: hzText(hzs[i]),
                      }),
                  ]
                : [],
        );
        return {
            expl: A("detail.room.e", { v: v.tMidSec.toFixed(2) }),
            cols: [ABS, "detail.col.mean"],
            rows: [row("detail.r.avg", cell(v.tMidSec.toFixed(2), "s"), null, true), ...per],
            notes: [A("detail.n.t20")],
        };
    },
    dr(state, r) {
        const d = r.directRatio;
        const b = state.bands;
        return {
            expl: A("detail.dr.e", { v: signed(d.drDb) }),
            cols: ["detail.col.val"],
            rows: [row("detail.r.dr", cell((d.lowerBoundOnly ? "≥ " : "") + signed(d.drDb), "dB"), null, true)],
            notes: [
                ...(d.lowerBoundOnly ? [A("flow.cd.dr.lower")] : []),
                ...(b ? [A("detail.n.dr", { lo: fmtDb(b.drLowBelowDb), hi: fmtDb(b.drHighAboveDb) })] : []),
            ],
        };
    },
    plosive(state, r) {
        const p = r.plosive;
        return {
            expl: A("detail.plosive.e", { v: fmtDb(p.worstRatioDb) }),
            cols: ["detail.col.val"],
            rows: [
                row("detail.r.pmax", dB(p.worstRatioDb), null, true),
                ...(fin(p.events) ? [row("detail.r.count", times(p.events))] : []),
            ],
            notes: p.lowSensitivity ? [A("detail.n.sens")] : [],
        };
    },
    sib(state, r, card) {
        const s = r.sibilance;
        const [a, z] = SIB_LABELS;
        const na = cell("—");
        return {
            expl: s.highBandDisabled
                ? A("detail.sib.n", { a, b: z, bw: kHz(s.effectiveBandwidthHz) })
                : A("detail.sib.e", { a, b: z }),
            cols: ["detail.col.val"],
            rows: [
                { label: { lit: a }, abs: level(s.midHighDb), rel: null, hl: true },
                {
                    label: { lit: z },
                    abs: s.highBandDisabled ? na : level(s.highDb),
                    rel: null,
                    hl: !s.highBandDisabled,
                },
                row("detail.r.bw", cell(String(Math.round(s.effectiveBandwidthHz)), "Hz")),
                ...(fin(s.events) ? [row("detail.r.count", times(s.events))] : []),
            ],
            notes: card.word === "flow.w.ungraded" ? [A("detail.n.ungraded")] : [],
        };
    },
    mouth(state, r, card) {
        const c = r.click;
        const f = kHz(CLICK_HIGHPASS_HZ);
        const hit = c.count > 0;
        return {
            expl: hit
                ? A("detail.mouth.e", { n: Math.round(c.perMinute), f, v: signed(c.loudestRelDb) })
                : A("detail.mouth.0"),
            cols: [ABS, "detail.col.peak"],
            rows: [
                row("detail.r.rate", cell(String(Math.round(c.perMinute)), null, "flow.u.perMin"), null, true),
                row("detail.r.count", times(c.count)),
                ...(hit ? [row("detail.r.loud", level(c.loudestDbfs), dB(c.loudestRelDb), false, { f })] : []),
            ],
            notes: card.word === "flow.w.ungraded" ? [A("detail.n.ungraded")] : [],
        };
    },
    dir(state, r) {
        const d = r.direction;
        const four = (a) => Array.isArray(a) && a.length === 4 && a.every(fin);
        if (!four(d.levelDb) || !four(d.brightnessDb)) return { expl: A("detail.dir.none") };
        const cur = Number.isInteger(d.currentIndex) && d.currentIndex >= 0 ? d.currentIndex : 0;
        const turn = d.levelDb.map((_, k) => (cur + k) % d.levelDb.length);
        // 并列最响时只亮第一个；turn 把当前方向转到了第一位，所以这里比较的是原下标
        const top = d.levelDb.indexOf(Math.max(...d.levelDb));
        const names = ["flow.dir.cur", "flow.dir.90", "flow.dir.180", "flow.dir.270"];
        const m = resultModel(state, "direction");
        return {
            expl: m ? m.expl : A("detail.dir.none"),
            cols: ["detail.col.loud", "detail.col.bright"],
            rows: turn.map((i, k) =>
                row(names[k], level(d.levelDb[i]), cell(fmtDb(d.brightnessDb[i]), "dB"), i === top),
            ),
            radar: m && m.viz && m.viz.kind === "radar" ? { rel: m.viz.rel } : null,
        };
    },
};

/** 这张卡点开后的内容。没有结果区段的卡（没测过）只给一句话和“去测”。 */
export function detailOf(id, state) {
    const card = cardOf(id, state);
    const step = STEP_OF[id];
    const need = card.stale || card.retest || card.missing;
    // 方向只开准备页（要用户先转话筒），按钮写“去测方向”，不写“重测”
    const key = card.missing ? "detail.go" : step === "direction" ? "detail.dir.go" : "detail.retest";
    const cta = need ? { key, step, prep: card.missing || step === "direction" } : null;
    if (card.missing)
        return {
            id,
            card,
            expl: A(id === "dir" ? "detail.dir.none" : "detail.none"),
            notes: [],
            rows: [],
            evidence: null,
            cta,
        };
    const body = BODY[id](state, state.results || {}, card);
    const why = card.retest && card.why ? [A(card.why, { need: card.need })] : [];
    return {
        id,
        card,
        notes: [],
        rows: [],
        radar: null,
        ...body,
        ...(why.length ? { notes: [...why, ...(body.notes || [])] } : {}),
        evidence: evidenceSpec(id, state),
        cta,
    };
}
