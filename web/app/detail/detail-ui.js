// SPDX-License-Identifier: GPL-3.0-or-later
//
// 详情面板：点结果卡打开的深色玻璃窗。表头（卡名、词、数字、已过期）、一句话解释、表格、说明、方向的雷达，
// 过期或要再测时底部有重测按钮。背景 inert、点遮罩与 Esc 关闭、焦点还回打开它的卡，由 shared/modal.js 负责。
// 只用 DOM API，文字走词条；内容全由 detailOf 给出，这里不读桥。
//
// 有证据的卡（增益、喷麦、齿音、口水音）在表格下面画证据画布：按这一步的 evidenceRev 变化才取一次数，取到之前、
// 取不到或这张卡不画，都整块不渲染，不留空框。画布节点在面板重画时原样搬进新结构，缩放状态不丢。
//
// ctx：{ t, retestSteps(steps), openPrep(step), getEvidence(stepId) → Promise<EvidencePack|null> }

import { el } from "../../shared/dom.js";
import { createModal } from "../../shared/modal.js";
import { ctaBtn, radar } from "../flow/flow-ui.js";
import { ic } from "../flow/icons.js";
import { cardsOf } from "../report/report-core.js";
import { detailOf } from "./detail-core.js";
import { createEvidence } from "./evidence-canvas.js";
import { normalize } from "./evidence-core.js";

const PANEL_W = 640;

export function createDetail(o) {
    const { ctx } = o;
    const t = (key, vars) => ctx.t(key, vars);
    let cur = null;
    const modal = createModal({
        host: o.host,
        part: "detail",
        width: PANEL_W,
        inert: o.inert,
        onEsc: () => close(),
        initialFocus: () => modal.panel.querySelector("[data-part='detail-close']"),
    });
    modal.panel.classList.add("dt");
    modal.scrim.addEventListener("click", (e) => {
        if (e.target === modal.scrim) close();
    });
    modal.panel.setAttribute("aria-labelledby", "dt-title");

    const label = (l) => (l.lit !== undefined ? l.lit : t(l.key, l.vars));
    const val = (c) => [
        el("b", { class: "sc-num", text: c.text }),
        c.unit ? el("i", { text: c.unit }) : c.unitKey ? el("i", { text: t(c.unitKey) }) : null,
    ];
    const td = (c) => el("td", {}, c ? val(c) : el("span", { text: "—" }));

    function table(d) {
        if (!d.rows.length) return null;
        return el(
            "table",
            { class: "dt__tbl", data: { part: "detail-table" } },
            el(
                "thead",
                {},
                el(
                    "tr",
                    {},
                    ["detail.col.item", ...d.cols].map((k) => el("th", { scope: "col", text: t(k) })),
                ),
            ),
            el(
                "tbody",
                {},
                d.rows.map((r) =>
                    el(
                        "tr",
                        { data: { hl: r.hl ? "1" : "0" } },
                        el("th", { scope: "row", text: label(r.label) }),
                        td(r.abs),
                        d.cols.length > 1 ? td(r.rel) : null,
                    ),
                ),
            ),
        );
    }

    /** 证据画布：没有就返回 null；要画的返回画布节点；rev 变了才重新取。 */
    function evidenceFor(spec) {
        if (!spec) return dropEvidence();
        let ev = cur.ev;
        if (!ev || ev.step !== spec.step) {
            dropEvidence();
            ev = cur.ev = { step: spec.step, comp: createEvidence(spec), rev: 0, ready: false };
        }
        if (ev.rev !== spec.rev) {
            // 这一步有了新的证据：新数据到之前不再画旧的，免得旧一版的波形和新结果并排
            ev.rev = spec.rev;
            ev.ready = false;
            fetchEvidence(ev, cur, spec.rev);
        }
        return ev.ready ? ev.comp : null;
    }

    function dropEvidence() {
        if (cur.ev) cur.ev.comp.destroy();
        cur.ev = null;
        return null;
    }

    function fetchEvidence(ev, owner, rev) {
        Promise.resolve()
            .then(() => ctx.getEvidence(ev.step))
            .catch(() => null)
            .then((pack) => {
                // 窗口关了、换了卡，或这期间又有新一版：这次的结果作废
                if (cur !== owner || cur.ev !== ev || ev.rev !== rev) return;
                const data = pack ? normalize(pack) : null;
                ev.ready = data !== null;
                ev.comp.setData(data);
                redraw(cur.state);
            });
    }

    function build(state) {
        cur.state = state;
        const d = detailOf(cur.id, state);
        const c = d.card;
        const ev = evidenceFor(d.evidence);
        const tone = c.stale || c.none ? "gray" : c.tone;
        const nums = (c.nums || []).map((n) =>
            el(
                "div",
                { class: "dt__num" },
                n.na ? el("b", { text: t(n.na) }) : val({ text: n.text, unit: n.unit, unitKey: n.unitKey }),
            ),
        );
        modal.panel.dataset.tone = tone;
        modal.panel.dataset.stale = c.stale ? "1" : "0";
        modal.panel.dataset.card = d.id;
        const parts = [
            el(
                "div",
                { class: "dt__hd" },
                el(
                    "div",
                    { class: "dt__ttl" },
                    el("span", { class: "sc-eyebrow", id: "dt-title", text: t(`flow.card.${d.id}`) }),
                    el(
                        "div",
                        { class: "dt__row" },
                        el("div", { class: "dt__word" }, el("i", { class: "sc-dot" }), el("span", { text: t(c.word) })),
                        c.stale ? el("span", { class: "rc__stale", text: t("flow.cf.stale") }) : null,
                        el("div", { class: "dt__nums" }, nums),
                    ),
                ),
                el("button", {
                    type: "button",
                    class: "sc-btn sc-btn--dark",
                    text: t("common.close"),
                    data: { glow: "1", part: "detail-close" },
                    on: { click: () => close() },
                }),
            ),
            c.stale
                ? el(
                      "p",
                      { class: "dt__stale", role: "status" },
                      ic("stale", 14),
                      el("span", { text: t("detail.stale") }),
                  )
                : null,
            el("p", { class: "dt__expl", text: t(d.expl.key, d.expl.vars) }),
            d.radar ? el("div", { class: "dt__radar" }, radar({ rel: d.radar.rel }, ctx)) : null,
            table(d),
            ev ? ev.el : null,
            d.notes.length
                ? el(
                      "div",
                      { class: "dt__notes" },
                      d.notes.map((n) =>
                          el("div", { class: "dt__note" }, ic("info", 14), el("span", { text: t(n.key, n.vars) })),
                      ),
                  )
                : null,
            d.cta
                ? el(
                      "div",
                      { class: "dt__cta" },
                      ctaBtn(ctx, d.cta.key, () => act(d.cta)),
                  )
                : null,
        ];
        modal.panel.replaceChildren(...parts.filter(Boolean));
        if (ev) ev.refresh(t);
    }

    /** 按最新数据重画，焦点还给同一个 data-part，找不到落到“关闭”。 */
    function redraw(state) {
        const at = modal.panel.contains(document.activeElement) ? document.activeElement.dataset.part : null;
        build(state);
        if (at !== null) {
            const back =
                modal.panel.querySelector(`[data-part="${at}"]`) ||
                modal.panel.querySelector("[data-part='detail-close']");
            back.focus({ preventScroll: true });
        }
    }

    function act(cta) {
        close();
        if (cta.prep) ctx.openPrep(cta.step);
        else ctx.retestSteps([cta.step]);
    }

    function close() {
        if (cur) dropEvidence();
        cur = null;
        modal.hide();
    }

    return {
        isOpen: () => cur !== null,
        open(id, state) {
            if (!state) return;
            if (cur) dropEvidence();
            cur = { id, state, ev: null };
            build(state);
            modal.show();
            if (cur.ev && cur.ev.ready) cur.ev.comp.refresh(t);
        },
        /** 快照更新或切换语言后按最新数据重画；打开着的那张卡不在了就关掉。 */
        update(state) {
            if (!cur || !state) return;
            if (!cardsOf(state).some((c) => c.id === cur.id)) return close();
            redraw(state);
        },
        close,
    };
}
