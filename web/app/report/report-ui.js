// SPDX-License-Identifier: GPL-3.0-or-later
//
// 报告页的组件：最该改的一件事、距离标尺、九张结果卡加一个“做了改动？”格。
// 只把 report-core 算出的数据画成 DOM；不读桥，动作经 ctx 回调交给 flow-main。全程只用 DOM API，文字走词条。
//
// ctx：{ t, go(view), openPrep(step), retestSteps(steps), reportChange(kind), copy(text), openDetail(cardId) }

import { el } from "../../shared/dom.js";
import { ctaBtn, ghostBtn, resultCard } from "../flow/flow-ui.js";
import { STEP_KEY } from "../flow/flow-core.js";
import { ic } from "../flow/icons.js";
import { CHANGE_KINDS, cardsOf, copyParts, directionSlot, fixModel, rulerModel } from "./report-core.js";

const TONE_ICON = { red: "alert", amber: "info", green: "circle-check", gray: "stale" };
/** 标尺底色的五段：太近（红）到太远（红），中间是合适。 */
const RULER_SEGS = ["red", "amber", "green", "amber", "red"];

/** 词条变量里的步骤列表（steps）换成当前语言的步骤名。 */
function say(t, a) {
    const vars =
        a.vars && a.vars.steps
            ? { ...a.vars, steps: a.vars.steps.map((id) => t(`flow.step.${STEP_KEY[id]}`)).join(t("flow.list.sep")) }
            : a.vars;
    return t(a.key, vars);
}

/** 一句话结果：各卡的数字加最该改的那句。 */
export function copyText(state, t) {
    const c = copyParts(state);
    const body = c.parts.map((p) => t(p.key, { v: p.v })).join(t("report.cp.sep"));
    return t("report.cp.head") + body + (c.fix ? t("report.cp.fix", { t: say(t, c.fix) }) : "");
}

function runCta(c, state, ctx) {
    if (c.kind === "retest") return ctx.retestSteps(c.steps);
    if (c.kind === "copy") return ctx.copy(copyText(state, ctx.t));
    return c.prep ? ctx.openPrep(c.to) : ctx.go(c.to);
}

function fixCard(m, state, ctx) {
    const { t } = ctx;
    const acts = m.acts.map((a, i) =>
        el(
            "li",
            { data: a.dir ? { dirhint: "act" } : null },
            el("span", { class: "pn__n sc-num", text: String(i + 1) }),
            el("span", { text: say(t, a) }),
        ),
    );
    return el(
        "section",
        { class: "fix", aria: { label: t(m.eyebrow) }, data: { part: "fix", tone: m.tone } },
        el(
            "div",
            { class: "fix__eb" },
            ic(TONE_ICON[m.tone], 16),
            el("span", { class: "sc-eyebrow", text: t(m.eyebrow) }),
        ),
        el("h2", { class: "fix__head", role: "status", aria: { live: "polite" }, text: say(t, m.head) }),
        el(
            "div",
            { class: "fix__row" },
            el("ol", { class: "fix__acts" }, acts),
            el(
                "div",
                { class: "fix__btns" },
                ctaBtn(ctx, m.cta.key, () => runCta(m.cta, state, ctx)),
                m.sec ? ghostBtn(ctx, m.sec.key, () => runCta(m.sec, state, ctx), "dir") : null,
            ),
        ),
    );
}

function chip(ctx, c) {
    const { t } = ctx;
    return el(
        "div",
        { class: "ev", role: "listitem", data: { has: c.on ? "1" : "0", k: c.key } },
        el("i", { class: "sc-dot" }),
        el("span", { text: t(`report.ev.${c.key}`) }),
        c.minor ? el("span", { class: "ev__minor", text: t("report.ev.minor") }) : null,
        el("span", { class: "sr-only", text: t(c.on ? "report.ev.has" : c.off ? "report.ev.off" : "report.ev.none") }),
    );
}

function ruler(m, ctx) {
    const { t } = ctx;
    const fresh = m.st === "fresh";
    const group = (label, chips) =>
        el(
            "div",
            { class: "ruler__grp" },
            el("span", { class: "ruler__gl", text: t(label) }),
            el(
                "div",
                { class: "ruler__list", role: "list" },
                chips.map((c) => chip(ctx, c)),
            ),
        );
    return el(
        "div",
        {
            class: "ruler ruler--card sc-glass",
            role: "group",
            aria: { label: t("report.ruler.aria") },
            data: { part: "ruler", st: m.st, tone: fresh ? "amber" : "gray" },
        },
        el(
            "div",
            { class: "ruler__eb" },
            el("span", { class: "sc-eyebrow", text: t("report.ruler.t") }),
            fresh
                ? m.conflict
                    ? el(
                          "span",
                          { class: "sc-pill", data: { tone: "amber" } },
                          el("i", { class: "sc-dot" }),
                          el("span", { text: t("report.ruler.conf") }),
                      )
                    : null
                : el("span", { class: "rc__stale", text: t(m.st === "stale" ? "flow.cf.stale" : "flow.cf.retry") }),
        ),
        el(
            "div",
            { class: "ruler__ax" },
            el("span", { text: t("report.ruler.near") }),
            el("span", { text: t("report.ruler.ok") }),
            el("span", { text: t("report.ruler.far") }),
        ),
        el(
            "div",
            { class: "ruler__track", data: { decor: "1" } },
            RULER_SEGS.map((tone) => el("i", { class: "ruler__seg", data: { tone } })),
            m.prev !== null ? el("i", { class: "ruler__ghost", style: { "--p": String(m.prev) } }) : null,
            m.pos !== null ? el("i", { class: "ruler__mk", style: { "--p": String(m.pos) } }) : null,
        ),
        m.pos === null ? el("span", { class: "sr-only", text: t("report.ruler.none") }) : null,
        el("div", { class: "ruler__ev" }, group("report.ruler.near.ev", m.near), group("report.ruler.far.ev", m.far)),
    );
}

function changeCell(ctx, state) {
    const { t } = ctx;
    const label = { gain: "report.chg.gain", position: "report.chg.pos", room: "report.chg.room" };
    return el(
        "div",
        { class: "chg sc-glass", data: { part: "changes" } },
        el("span", { class: "sc-eyebrow", text: t("report.acts") }),
        CHANGE_KINDS.map((k) => ghostBtn(ctx, label[k], () => ctx.reportChange(k))),
        ghostBtn(ctx, "report.copy", () => ctx.copy(copyText(state, t)), "copy"),
    );
}

/** 一张卡加它的“查看详情”按钮：按钮是卡的兄弟节点，铺满整张卡，叠在卡上；方向卡的小按钮再叠在它上面。 */
function cardSlot(card, extra, ctx) {
    const { t } = ctx;
    const face = resultCard(card, false, true, ctx);
    // 首次测量：写在可信度的上面，和“上次 → 现在”同一个位置、同一个样式
    if (card.first) {
        const note = el("div", { class: "rc__prev", data: { part: "first" }, text: t("report.first") });
        const conf = face.querySelector(".rc__conf");
        if (conf) conf.before(note);
        else face.append(note);
    }
    return el(
        "div",
        { class: "rc-slot", data: { dirhint: extra ? "card" : null } },
        face,
        el("button", {
            type: "button",
            class: "rc-slot__open",
            aria: { label: t("detail.open", { name: t(`flow.card.${card.id}`) }) },
            data: { part: "card-open", card: card.id },
            on: { click: () => ctx.openDetail(card.id) },
        }),
        extra,
    );
}

/** 方向卡上的小按钮：叠在数字和可信度之间。 */
function dirGo(kind, state, ctx) {
    const sec = state.steps && state.steps.direction ? Math.round(state.steps.direction.durationSec || 0) : 0;
    const again = kind === "again";
    const key = again ? "report.dir.again" : sec > 0 ? "report.dir.card" : "report.dir.go";
    return el(
        "button",
        {
            type: "button",
            class: "sc-btn rc-slot__go",
            data: { glow: "1", part: "dir-go" },
            on: { click: () => ctx.openPrep("direction") },
        },
        again ? ic("retry", 14) : null,
        el("span", { text: ctx.t(key, { n: sec }) }),
    );
}

/** 整页报告。返回根节点（tabindex -1，翻页后把焦点交给它）。 */
export function renderReport(state, ctx) {
    const cards = cardsOf(state);
    const slot = directionSlot(state);
    return el(
        "div",
        {
            class: "rp",
            tabindex: "-1",
            role: "region",
            aria: { label: ctx.t("report.aria") },
            data: { part: "report-page", step: "report" },
        },
        el("div", { class: "rp__top" }, fixCard(fixModel(state), state, ctx), ruler(rulerModel(state), ctx)),
        el(
            "div",
            { class: "rp__grid", data: { n: String(cards.length) } },
            cards.map((c) => cardSlot(c, c.id === "dir" && slot ? dirGo(slot, state, ctx) : null, ctx)),
            changeCell(ctx, state),
        ),
    );
}
