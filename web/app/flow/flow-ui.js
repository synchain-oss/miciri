// SPDX-License-Identifier: GPL-3.0-or-later
//
// 流程页的组件：步骤条、叠卡（上下各露出一张摘要卡）、接通页、准备页、结果页，以及结果卡、区间条、拍手槽位、方向雷达。
// 只把 flow-core 算出的数据模型画成 DOM；不读快照、不碰桥，动作经 ctx 回调交给 flow-main。
// 全程只用 DOM API，文字走词条或 textContent。
//
// ctx：{ t, go(view), start(step), retest(step), recheck(), dismiss(), animate }

import { el, svg } from "../../shared/dom.js";
import { fmtDb } from "../meter/meter-core.js";
import {
    bannersFor,
    cardMode,
    connectModel,
    neighbour,
    peekSegments,
    prepModel,
    REPORT,
    resultModel,
    STEP_KEY,
    stepState,
    visibleSteps,
} from "./flow-core.js";
import { ic, STATE_ICON } from "./icons.js";

/** 词条变量里 { word: 键 } 的值先翻译成当前语言的词，再交给 t。 */
export const tv = (t, vars) =>
    vars &&
    Object.fromEntries(
        Object.entries(vars).map(([k, v]) => [k, v && typeof v === "object" && "word" in v ? t(v.word) : v]),
    );
const tt = (t, key, vars) => t(key, tv(t, vars));

/** 原生 append 会把 null 写成文字“null”：这里统一滤掉空值。 */
const add = (parent, ...kids) =>
    parent.append(...kids.flat(Infinity).filter((k) => k !== null && k !== undefined && k !== false));

const stepName = (t, id) => t(`flow.step.${STEP_KEY[id]}`);

// ---------------------------------------------------------------------------------------------------------------------
// 步骤条

/** 页眉中间的步骤条。当前页覆盖成 current；耳机麦时没有方向步。 */
export function renderSteps(host, state, view, ctx) {
    const { t } = ctx;
    const ids = [...visibleSteps(state), REPORT];
    const items = ids.map((id) => {
        const st = id === view ? "current" : id === REPORT ? "todo" : stepState(state, id);
        const [icon, tone] = STATE_ICON[st];
        const name = stepName(t, id);
        return el(
            "li",
            {},
            el(
                "button",
                {
                    type: "button",
                    class: "step",
                    data: { st, step: id, glow: "1" },
                    aria: { current: id === view ? "step" : null },
                    on: { click: () => ctx.go(id) },
                },
                el("span", { data: { tone }, class: "step__ic" }, ic(icon, 16)),
                el("span", { class: "step__name", text: name }),
                id === "direction" ? el("span", { class: "step__adv", text: t("flow.step.adv") }) : null,
                // 状态只靠图标会漏掉读屏用户：可见名字之外补一段只读给读屏的状态
                el("span", { class: "sr", text: t(`flow.st.${st}`) }),
            ),
        );
    });
    host.replaceChildren(el("ol", { class: "steps", aria: { label: t("flow.steps.aria") } }, items));
}

// ---------------------------------------------------------------------------------------------------------------------
// 叠卡

function peek(state, id, pos, ctx) {
    const { t } = ctx;
    const st = id === REPORT ? "todo" : stepState(state, id);
    const segs = peekSegments(state, id);
    const [icon, tone] = STATE_ICON[st];
    const kids = [];
    const summary = [];
    segs.forEach((g, n) => {
        const text = tt(t, g.key, g.vars);
        summary.push(text);
        if (n) kids.push(el("span", { text: "·" }));
        kids.push(el("span", { text }));
    });
    // 下面那张未做的卡写“下一步：X · 约 N 秒”
    if (pos === "down" && id !== REPORT && st !== "done" && st !== "stale" && st !== "fail") {
        const sec = state.steps && state.steps[id] ? Math.round(state.steps[id].durationSec) : 0;
        kids.length = 0;
        const text = t("flow.stk.next", { name: stepName(t, id), n: sec });
        summary.length = 0;
        summary.push(text);
        kids.push(el("span", { text }));
    }
    const arrow = ic("up", 14);
    arrow.setAttribute("class", "stk__go");
    return el(
        "button",
        {
            type: "button",
            class: "stk__peek",
            data: { pos, st, step: id, glow: "1" },
            on: { click: () => ctx.go(id) },
        },
        el("span", { class: "stk__ic", data: { tone } }, ic(icon, 16)),
        el("span", { class: "sc-eyebrow", text: stepName(t, id) }),
        el("span", { class: "stk__sum" }, kids),
        el("span", { class: "sr", text: t(`flow.stk.aria.${pos}`) }),
        arrow,
    );
}

function banner(b, ctx) {
    return el(
        "div",
        { class: "stk__banner" },
        el(
            "div",
            { class: "sc-banner", data: { tone: b.tone } },
            ic(b.icon, 16),
            el("span", { class: "sc-banner__text", text: tt(ctx.t, b.key, b.vars) }),
        ),
    );
}

/**
 * 叠卡：当前卡居中，上下各露出一张摘要卡；带横幅时横幅占上露那一格。
 * @param {number} fromPx 翻页动画的起点位移（px，0 表示不动画）
 */
export function renderStack(host, state, view, ctx, fromPx = 0) {
    const ids = [...visibleSteps(state), REPORT];
    const i = ids.indexOf(view);
    const banners = view === REPORT ? [] : bannersFor(state, view);
    const upId = i > 0 && banners.length === 0 ? ids[i - 1] : null;
    const downId = i >= 0 && i < ids.length - 1 ? ids[i + 1] : null;
    const cur = el(
        "div",
        { class: "stk__cur", tabindex: "-1", data: { part: "card", step: view } },
        page(state, view, ctx),
    );
    const track = el(
        "div",
        { class: "stk__track", style: fromPx ? { "--from": `${fromPx}px` } : null },
        banners.map((b) => banner(b, ctx)),
        upId ? peek(state, upId, "up", ctx) : null,
        cur,
        downId ? peek(state, downId, "down", ctx) : null,
    );
    host.replaceChildren(
        el(
            "div",
            {
                class: "stk",
                role: "group",
                aria: { label: ctx.t("flow.stk.aria") },
                data: { part: "stack", idx: String(i), step: view },
            },
            track,
        ),
    );
    return cur;
}

function page(state, view, ctx) {
    if (view === REPORT) return reportPlaceholder(state, ctx);
    const mode = cardMode(state, view, ctx.forcePrep.has(view));
    if (mode === "connect") return connectPage(state, ctx);
    if (mode === "result") {
        const m = resultModel(state, view);
        if (m) return resultPage(state, view, m, ctx);
    }
    return prepPage(state, view, ctx);
}

// ---------------------------------------------------------------------------------------------------------------------
// 共用小件

const eyebrow = (state, ctx, id) => {
    const ids = visibleSteps(state);
    return el("span", {
        class: "sc-eyebrow",
        text: ctx.t("flow.pn.step", { n: ids.indexOf(id) + 1, total: ids.length + 1 }),
    });
};
const ctaBtn = (ctx, key, on, o = {}) =>
    el("button", {
        type: "button",
        class: "sc-btn sc-btn--cta sc-btn--lg",
        text: ctx.t(key, o.vars),
        disabled: o.disabled,
        aria: { describedby: o.describedby },
        data: { glow: "1", part: "cta" },
        on: { click: on },
    });
const ghostBtn = (ctx, key, on, icon, vars) =>
    el(
        "button",
        { type: "button", class: "sc-btn", data: { glow: "1" }, on: { click: on } },
        icon ? ic(icon, 14) : null,
        el("span", { text: ctx.t(key, vars) }),
    );
const pill = (ctx, tone, key, o = {}) =>
    el(
        "span",
        { class: `sc-pill${o.lg ? " sc-pill--lg" : ""}`, data: { tone } },
        o.spin ? el("i", { class: "sc-spinner", style: { "--sz": "9px" } }) : el("i", { class: "sc-dot" }),
        el("span", { text: ctx.t(key) }),
    );
const ring = (icon) => el("div", { class: "pn__ring" }, ic(icon, 64));

// ---------------------------------------------------------------------------------------------------------------------
// 接通页与无信号诊断（只写“可能”，不声称读到监听或 arm 状态）

function connectPage(state, ctx) {
    const { t } = ctx;
    const m = connectModel(state);
    const col = el("div", { class: "pn__col" });
    const viz = el("div", { class: "pn__viz" });
    const panel = el("div", { class: "panel sc-glass", data: { part: "connect", mode: m.mode } }, col, viz);
    add(col, eyebrow(state, ctx, "connect"));
    const names = {
        ch: t(`flow.ch.${m.lane === "left" ? "left" : "right"}`),
        other: t(`flow.ch.${m.other}`),
        sec: m.sec,
    };
    const startGain = () => ctx.start("gain");
    if (m.diag) {
        const d = m.diag;
        add(
            col,
            el("div", { class: "dg__ic", data: { tone: d.tone } }, ic(d.icon, 28)),
            el(
                "div",
                { class: "dg__title" },
                el("h2", { class: "pn__title", text: t(d.t) }),
                d.host && state.device && state.device.hostName
                    ? el("span", { class: "sc-chip", text: state.device.hostName })
                    : null,
            ),
            el("p", { class: "pn__lead", role: "status", aria: { live: "polite" }, text: t(d.b, names) }),
            el(
                "div",
                { class: "pn__cta pn__cta--wrap" },
                ghostBtn(ctx, "flow.btn.retry", () => ctx.recheck(), "retry"),
                ctaBtn(ctx, "flow.cn.cta", startGain, {
                    disabled: m.blocked,
                    describedby: m.blocked ? "dg-why" : null,
                }),
                d.dismiss
                    ? el(
                          "button",
                          {
                              type: "button",
                              class: "sc-btn sc-btn--text",
                              data: { glow: "1", part: "dismiss" },
                              on: { click: () => ctx.dismiss() },
                          },
                          el("span", { text: t("flow.btn.nomore") }),
                      )
                    : null,
                m.blocked
                    ? el("p", {
                          class: "pn__sub pn__why",
                          id: "dg-why",
                          text: t(m.redBlock ? "flow.dg.blocked" : "flow.bn.offline"),
                      })
                    : null,
            ),
        );
        add(
            viz,
            el("span", { class: "sc-eyebrow", text: t("flow.dg.seen") }),
            el(
                "div",
                { class: "pn__list" },
                m.seen.map(([k, v]) =>
                    el(
                        "div",
                        { class: "pn__row" },
                        el("span", { class: "k", text: t(k) }),
                        el("span", { class: "v", text: t(v, names) }),
                    ),
                ),
            ),
        );
        return panel;
    }
    const ok = m.ok;
    add(
        col,
        el("h2", { class: "pn__title", text: t(ok ? "flow.cn.title.ok" : "flow.cn.title.first") }),
        el("p", {
            class: "pn__lead",
            text: t(ok ? (m.stereo ? "flow.cn.lead.ok.st" : "flow.cn.lead.ok") : "flow.cn.lead.first"),
        }),
        el(
            "div",
            { class: "sc-banner pn__phones", data: { tone: "gray" } },
            ic("ear", 16),
            el("span", { class: "sc-banner__text", text: t("flow.cn.phones") }),
        ),
        ok ? null : el("p", { class: "pn__sub", text: t("flow.cn.mic.hint") }),
        el(
            "div",
            { class: "pn__cta pn__cta--wrap" },
            ctaBtn(ctx, "flow.cn.cta", startGain, { disabled: m.blocked, describedby: m.blocked ? "cn-why" : null }),
            m.blocked
                ? el("p", {
                      class: "pn__sub pn__why",
                      id: "cn-why",
                      text: t(m.offline ? "flow.bn.offline" : "flow.cn.blocked"),
                  })
                : null,
        ),
    );
    if (!ok) {
        add(
            viz,
            ring("mic"),
            el("div", { class: "pn__wait" }, pill(ctx, "gray", "flow.cn.wait", { lg: true, spin: true })),
        );
    } else {
        const dev = state.device || {};
        const rows = m.stereo ? [["flow.dg.l.L"], ["flow.dg.l.R"]] : [["flow.dg.l.sig"]];
        add(
            viz,
            ring("mic"),
            el(
                "div",
                { class: "pn__list" },
                rows.map(([k]) =>
                    el(
                        "div",
                        { class: "pn__row" },
                        el("span", { class: "k", text: t(k) }),
                        pill(ctx, "green", "flow.dg.v.sig"),
                    ),
                ),
                el(
                    "div",
                    { class: "pn__row" },
                    el("span", { class: "k", text: t("flow.cn.rate") }),
                    el("span", { class: "v sc-num", text: `${+((dev.sampleRate || 0) / 1000).toFixed(1)} kHz` }),
                ),
                el(
                    "div",
                    { class: "pn__row" },
                    el("span", { class: "k", text: t("flow.cn.buf") }),
                    el("span", { class: "v", text: t("flow.cn.smp", { n: dev.bufferSize || 0 }) }),
                ),
            ),
        );
    }
    return panel;
}

// ---------------------------------------------------------------------------------------------------------------------
// 准备页

function prepPage(state, id, ctx) {
    const { t } = ctx;
    const p = prepModel(state, id);
    const k = STEP_KEY[id];
    const col = el("div", { class: "pn__col" });
    const viz = el("div", { class: "pn__viz" });
    const how = el(
        "ol",
        { class: "pn__steps" },
        p.how.map((h, i) =>
            el(
                "li",
                {},
                el("span", { class: "pn__n sc-num", text: String(i + 1) }),
                el(
                    "div",
                    {},
                    el("div", { text: t(h.key, p.vars && { ...p.vars, ...h.vars }) }),
                    h.list ? el("div", { class: "pn__words", text: t(h.list) }) : null,
                ),
            ),
        ),
    );
    const cta = ctaBtn(ctx, "flow.pn.start", () => ctx.start(id));
    add(
        col,
        el(
            "div",
            { class: "pn__head" },
            eyebrow(state, ctx, id),
            el("span", { class: "sc-chip" }, ic("stale", 12), el("span", { text: t("flow.pn.dur", { n: p.sec }) })),
        ),
        el("h2", { class: "pn__title", text: t(`flow.pp.${k}.t`, p.vars) }),
        el("p", { class: "pn__lead", text: t(`flow.pp.${k}.l`, p.vars) }),
        el("div", {}, el("span", { class: "sc-eyebrow", text: t("flow.pn.how") }), how),
        el(
            "div",
            { class: "pn__cta" },
            cta,
            id === "direction" ? ghostBtn(ctx, "flow.pn.skip", () => ctx.go(REPORT), "arrow-r") : null,
        ),
    );
    const bands = state.bands;
    const itemVars = { lo: bands && fmtDb(bands.tpQuietDbfs, 0), hi: bands && fmtDb(bands.tpLoudDbfs, 0) };
    add(
        viz,
        ring(p.icon),
        el("span", { class: "sc-eyebrow", text: t("flow.pn.measure") }),
        el(
            "div",
            { class: "pn__list" },
            p.items.map((key) => el("div", { class: "pn__row" }, el("span", { text: t(key, itemVars) }))),
        ),
    );
    return el("div", { class: "panel sc-glass", data: { part: "prep", step: id } }, col, viz);
}

// ---------------------------------------------------------------------------------------------------------------------
// 结果页

function resultCard(card, big, multi, ctx) {
    const { t } = ctx;
    const tone = card.stale || card.none ? "gray" : card.tone;
    const nums = (card.nums || []).map((n, i) =>
        el(
            "div",
            { class: "rc__num" },
            card.bandLabels ? el("span", { class: "rc__band", text: card.bandLabels[i] }) : null,
            n.na ? el("b", { class: "rc__na", text: t(n.na) }) : el("b", { class: "sc-num", text: n.text }),
            n.unit ? el("i", { text: n.unit }) : n.unitKey ? el("i", { text: t(n.unitKey) }) : null,
        ),
    );
    if (card.none) nums.push(el("div", { class: "rc__num" }, el("b", { text: "—" })));
    let confTone = "green";
    let confText = [t("flow.cf.ok")];
    if (card.stale) {
        confTone = "gray";
        confText = [t("flow.cf.stale")];
    } else if (card.retest || card.why) {
        confTone = "amber";
        confText = [t("flow.cf.retry"), card.why ? t(card.why, { need: card.need }) : null];
    }
    return el(
        "div",
        {
            class: `rc${multi ? " rc--multi" : ""}${big ? " rc--lg" : ""}`,
            data: { tone, st: card.stale ? "stale" : card.none ? "none" : "fresh", card: card.id },
        },
        el("div", { class: "rc__eb" }, el("span", { class: "sc-eyebrow", text: t(`flow.card.${card.id}`) })),
        el("div", { class: "rc__word" }, el("i", { class: "sc-dot" }), el("span", { text: t(card.word) })),
        el("div", { class: "rc__nums" }, nums),
        card.note ? el("div", { class: "rc__note", text: tt(t, card.note.key, card.note.vars) }) : null,
        el(
            "div",
            { class: "rc__conf", data: { ok: confTone === "amber" ? "0" : "1" } },
            el("i", { class: "sc-dot", data: { tone: confTone } }),
            el(
                "div",
                {},
                confText.filter(Boolean).map((s) => el("div", { text: s })),
            ),
        ),
    );
}

const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));

/** 区间条：竖条，上大下小，当前值是外侧的短刻度加读数。 */
function bandsViz(v, ctx) {
    const { t } = ctx;
    const span = v.dom[1] - v.dom[0];
    const col = el(
        "div",
        { class: "bands__col" },
        v.segs.map(([a, z, tone, key]) =>
            el(
                "div",
                {
                    class: "bands__seg",
                    data: { tone },
                    style: { "flex-grow": String((z - a) / span), "flex-basis": "0" },
                },
                el("span", { text: t(key) }),
            ),
        ),
    );
    const at = ((v.dom[1] - clamp(v.value, v.dom[0], v.dom[1])) / span) * 100;
    return el(
        "div",
        { class: "bands", data: { part: "bands" }, style: { "--t": `${at.toFixed(2)}%` } },
        col,
        el("i", { class: "bands__mk" }),
        el("span", { class: "bands__mkv" }, el("span", { class: "sc-num", text: v.text }), el("i", { text: v.unit })),
    );
}

const SLOT_ICON = { valid: "check", soft: "up", noisy: "wave", gate: "cut", wait: "circle" };
export const slotEl = (s, ctx) =>
    el("div", { class: "slot", data: { s } }, ic(SLOT_ICON[s], 26), el("span", { text: ctx.t(`flow.sl.${s}`) }));

/** 雷达的显示刻度（只决定怎么画，不是判据）：半径 = R × (1 + 相对 dB ÷ 满量程)，虚线圈画在这几档相对 dB 上。 */
const RADAR_FULL_DB = 20;
const RADAR_RINGS_DB = [0, -6, -12];

/** 四点雷达：最响的点实心，当前方向画虚线圈。rel 顺序：当前、转 90°、转 180°、转 270°。 */
function radar(v, ctx) {
    const { t } = ctx;
    const R = 68;
    const C = 100;
    const ANG = [-90, 0, 90, 180];
    const rad = (d) => Math.max(0.1, 1 + d / RADAR_FULL_DB) * R;
    const pt = (i, r) => [C + r * Math.cos((ANG[i] * Math.PI) / 180), C + r * Math.sin((ANG[i] * Math.PI) / 180)];
    const f = (n) => n.toFixed(1);
    const pts = v.rel.map((d, i) => pt(i, rad(d)));
    const top = v.rel.indexOf(Math.max(...v.rel));
    const chart = svg(
        "svg",
        { viewBox: "0 0 200 200", width: 200, height: 200, role: "img", "aria-label": t("flow.dir.radar.aria") },
        RADAR_RINGS_DB.map((d) => svg("circle", { class: "rd__ring", cx: C, cy: C, r: f(rad(d)) })),
        ANG.map((_, i) => svg("line", { class: "rd__axis", x1: C, y1: C, x2: f(pt(i, R)[0]), y2: f(pt(i, R)[1]) })),
        svg("polygon", { class: "rd__poly", points: pts.map((p) => `${f(p[0])},${f(p[1])}`).join(" ") }),
        svg("circle", { class: "rd__cur", cx: f(pts[0][0]), cy: f(pts[0][1]), r: 9 }),
        pts.map((p, i) =>
            svg("circle", { class: "rd__pt", "data-max": i === top ? "1" : "0", cx: f(p[0]), cy: f(p[1]), r: 4.2 }),
        ),
    );
    const POS = ["top", "rgt", "bot", "lft"];
    const KEYS = ["flow.dir.cur", "flow.dir.90", "flow.dir.180", "flow.dir.270"];
    const label = (i) =>
        el(
            "div",
            { class: "radar__lb", data: { [POS[i]]: "1" } },
            el("span", { text: t(KEYS[i]) }),
            el("b", { class: "sc-num", text: `${fmtDb(v.rel[i])} dB` }),
        );
    return [
        el("span", { class: "sc-eyebrow", text: t("flow.ms.dir") }),
        el("div", { class: "radar", data: { part: "radar" } }, chart, [0, 1, 2, 3].map(label)),
        el("div", { class: "radar__ring", text: t("flow.dir.ring") }),
    ];
}

function resultPage(state, id, m, ctx) {
    const { t } = ctx;
    const k = STEP_KEY[id];
    const col = el("div", { class: "pn__col" });
    const viz = el("div", { class: "pn__viz" });
    const multi = m.cards.length > 1;
    const panel = el(
        "div",
        { class: "panel sc-glass res", data: { part: "result", step: id, side: m.side ? "1" : "0" } },
        col,
        viz,
    );
    add(
        col,
        el(
            "div",
            { class: "res__head" },
            el("h2", { class: "pn__title", text: t("flow.rs.title", { name: stepName(t, id) }) }),
            eyebrow(state, ctx, id),
        ),
        el(
            "div",
            {
                class: "res__cards",
                style: { "--cols": String(m.cols), "--rows": String(Math.ceil(m.cards.length / m.cols)) },
            },
            m.cards.map((c) => resultCard(c, !multi, multi, ctx)),
        ),
    );
    const lead = el("p", {
        class: "pn__lead",
        role: "status",
        aria: { live: "polite" },
        text: tt(t, m.expl.key, m.expl.vars),
    });
    const acts = m.acts.length
        ? el(
              "ol",
              { class: "res__acts" },
              m.acts.map((a, i) =>
                  el(
                      "li",
                      {},
                      el("span", { class: "pn__n sc-num", text: String(i + 1) }),
                      el("span", { text: tt(t, a.key, a.vars) }),
                  ),
              ),
          )
        : null;
    const v = m.viz;
    if (v && v.kind === "bands")
        add(
            viz,
            el("span", {
                class: "sc-eyebrow sc-eyebrow--units",
                text: t(v.title, { lo: fmtDb(state.bands.tpQuietDbfs, 0), hi: fmtDb(state.bands.tpLoudDbfs, 0) }),
            }),
            bandsViz(v, ctx),
        );
    else if (v && v.kind === "slots")
        add(
            viz,
            el("span", { class: "sc-eyebrow", text: t(v.title) }),
            el(
                "div",
                { class: "slots-mini" },
                v.slots.map((s) => slotEl(s, ctx)),
            ),
        );
    else if (v && v.kind === "radar") add(viz, radar(v, ctx));
    if (m.bars)
        add(
            viz,
            el(
                "div",
                { class: "res__bars" },
                m.bars.map((b) =>
                    el(
                        "div",
                        { class: "res__bar" },
                        el("span", { text: b.label }),
                        el("i", { style: { "--w": `${b.width}%` } }),
                        el("span", { class: "sc-num", text: b.text }),
                    ),
                ),
                m.below ? el("div", { class: "res__note", text: tt(t, m.below.key, m.below.vars) }) : null,
            ),
        );
    add(m.side ? viz : col, lead, acts);
    add(
        col,
        el(
            "div",
            { class: "pn__cta" },
            m.ctas.map((b) => {
                const on = b.retest ? () => ctx.retest(b.retest) : () => ctx.go(b.to);
                return b.primary ? ctaBtn(ctx, b.key, on) : ghostBtn(ctx, b.key, on, b.icon);
            }),
        ),
    );
    panel.dataset.next = String(!!neighbour(state, id, 1));
    void k;
    return panel;
}

function reportPlaceholder(state, ctx) {
    const { t } = ctx;
    const last = visibleSteps(state).slice(-1)[0];
    return el(
        "div",
        { class: "panel sc-glass", data: { part: "report-slot" } },
        el(
            "div",
            { class: "pn__col" },
            el("h2", { class: "pn__title", text: t("flow.step.report") }),
            el(
                "div",
                { class: "pn__cta" },
                ghostBtn(ctx, "flow.step.report.back", () => ctx.go(last), "arrow-r"),
            ),
        ),
    );
}
