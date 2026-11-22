// SPDX-License-Identifier: GPL-3.0-or-later
//
// 报告页的组件：最该改的一件事、距离卡（前后对比）、九张结果卡加一个“做了改动？”格。
// 只把 report-core 算出的数据画成 DOM；不读桥，动作经 ctx 回调交给 flow-main。全程只用 DOM API，文字走词条。
//
// ctx：{ t, go(view), openPrep(step), retestSteps(steps), reportChange(kind), copy(text), openDetail(cardId) }

import { el } from "../../shared/dom.js";
import { ctaBtn, ghostBtn, recDistLine, resultCard } from "../flow/flow-ui.js";
import { STEP_KEY } from "../flow/flow-core.js";
import { ic } from "../flow/icons.js";
import { CHANGE_KINDS, cardsOf, copyParts, directionSlot, distanceModel, fixModel } from "./report-core.js";

const TONE_ICON = { red: "alert", amber: "info", green: "circle-check", gray: "stale" };

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

/** 前后对比的一项读数：更近、更远、没有明显变化，没比的写原因；比得出来时带上“现在减上次”的 dB。 */
function signChip(ctx, c) {
    const { t } = ctx;
    const on = c.sign === "closer" || c.sign === "farther";
    return el(
        "div",
        { class: "ev", role: "listitem", data: { has: on ? "1" : "0", k: c.key, sign: c.sign } },
        el("span", { class: "ev__k" }, el("i", { class: "sc-dot" }), el("span", { text: t(`report.ev.${c.key}`) })),
        el("b", { class: "ev__sign", text: t(`report.sign.${c.sign}`) }),
        c.delta ? el("i", { class: "ev__d sc-num", text: t("report.sign.db", { v: c.delta }) }) : null,
    );
}

/**
 * 距离卡：第一次只有“可能离远了”的弱提示；重测位置之后，一句话读法、一条示意的前后标尺（虚线是上次的位置，
 * 实线是现在，只示意方向，不是距离）、三项读数各自的变化，最后是推荐距离。
 */
function distanceCard(m, ctx) {
    const { t } = ctx;
    const fresh = m.st === "fresh";
    return el(
        "div",
        {
            class: "dist sc-glass",
            role: "group",
            aria: { label: t("report.dist.aria") },
            data: { part: "distance", st: m.st, kind: m.kind, tone: fresh && m.hint ? "amber" : "gray" },
        },
        el(
            "div",
            { class: "dist__eb" },
            el("span", { class: "sc-eyebrow", text: t("report.dist.t") }),
            m.hint
                ? el(
                      "span",
                      { class: "sc-pill", data: { tone: "amber", part: "hint" } },
                      el("i", { class: "sc-dot" }),
                      el("span", { text: t("report.dist.hint") }),
                  )
                : null,
            m.st === "stale" || m.st === "low"
                ? el("span", { class: "rc__stale", text: t(m.st === "stale" ? "flow.cf.stale" : "flow.cf.retry") })
                : null,
        ),
        el("p", { class: "dist__read", data: { part: "read" }, text: say(t, m.read) }),
        m.ghost !== null
            ? el(
                  "div",
                  { class: "dist__cmp", data: { part: "compare" } },
                  el(
                      "div",
                      { class: "dist__ax" },
                      el("span", { text: t("report.dist.near") }),
                      el("span", { text: t("report.dist.legend") }),
                      el("span", { text: t("report.dist.far") }),
                  ),
                  el(
                      "div",
                      { class: "dist__track", data: { decor: "1" } },
                      el("i", { class: "dist__ghost", style: { "--p": String(m.ghost) } }),
                      m.pos !== null ? el("i", { class: "dist__mk", style: { "--p": String(m.pos) } }) : null,
                  ),
              )
            : null,
        m.signs.length
            ? el(
                  "div",
                  { class: "dist__list", role: "list" },
                  m.signs.map((c) => signChip(ctx, c)),
              )
            : null,
        recDistLine(m.rec, ctx),
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
        el("div", { class: "rp__top" }, fixCard(fixModel(state), state, ctx), distanceCard(distanceModel(state), ctx)),
        el(
            "div",
            { class: "rp__grid", data: { n: String(cards.length) } },
            cards.map((c) => cardSlot(c, c.id === "dir" && slot ? dirGo(slot, state, ctx) : null, ctx)),
            changeCell(ctx, state),
        ),
    );
}
