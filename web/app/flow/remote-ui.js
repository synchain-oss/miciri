// SPDX-License-Identifier: GPL-3.0-or-later
//
// 远距离模式：测试进行中整窗换成一整块不透明的语义色（紫=进行中，绿=成功，玫瑰红=重来），
// 站在 1.5 米外也看得清：指令 ≥ 48 px、念的字 ≥ 64 px、全宽电平条。
//
// 结构只在“形状”变了（颜色、有没有字、点、槽位……）时重建，倒计时、指令文字、点亮的点、槽位判定都只改文字和属性，
// 不重建节点：指令行是 aria-live=assertive，重建会让读屏把同一句话再念一遍。
// 电平条的数据来自 miciri.meters；只有在显示时才跑 rAF，隐藏后零开销。

import { el } from "../../shared/dom.js";
import { advance, fmtDb, rest, ratio } from "../meter/meter-core.js";
import { slotEl, tv } from "./flow-ui.js";
import { ic } from "./icons.js";
import { STEP_KEY } from "./flow-core.js";

/** 全宽电平条的刻度下限（dBFS）；上限是 0。目标区取自快照里的 bands。 */
const BAR_FLOOR = -60;
const SILENT_MS = 250;

const setText = (node, text) => {
    if (node && node.textContent !== text) node.textContent = text;
};
const setAttr = (node, name, value) => {
    if (node.getAttribute(name) !== value) node.setAttribute(name, value);
};

/**
 * @param {{host: HTMLElement, t: Function, getLang: Function, onCancel: Function}} o
 *   t 是外壳的取词函数（切语言后自动取新语言）；onCancel 只在用户点“取消”时调用
 */
export function createRemote(o) {
    const { host, t, getLang, onCancel } = o;
    const tr = (m) => (m ? t(m.key, tv(t, m.vars)) : "");
    let shape = "";
    let refs = null;
    let lvl = rest();
    let target = { db: -80, peak: -80 };
    let lastEvent = 0;
    let raf = 0;
    let last = 0;
    let shown = false;

    function build(m, zone) {
        const r = {};
        r.liq = el("i", { class: "rm__liq" });
        r.pk = el("i", { class: "rm__pk" });
        const barAttrs = {
            class: "rm__bar",
            role: "img",
            aria: { label: t("flow.rm.bar.aria") },
            data: { part: "bar" },
        };
        const bar = m.bar
            ? el(
                  "div",
                  {
                      ...barAttrs,
                      style: zone
                          ? {
                                "--zlo": ratio(zone.lo, BAR_FLOOR).toFixed(4),
                                "--zhi": ratio(zone.hi, BAR_FLOOR).toFixed(4),
                            }
                          : null,
                  },
                  r.liq,
                  r.pk,
                  zone ? el("i", { class: "rm__zone" }) : null,
              )
            : null;
        r.icon = m.icon ? el("div", { class: "rm__icon" }, ic(m.icon, 56)) : null;
        r.cmd = el("h2", { class: "rm__cmd", role: "status", aria: { live: "assertive" }, data: { part: "cmd" } });
        r.words = m.words ? el("p", { class: "rm__words", data: { part: "words" } }) : null;
        r.dots = m.dots
            ? el(
                  "div",
                  { class: "rm__dots", data: { part: "dots" } },
                  Array.from({ length: m.dots.total }, () =>
                      el("span", { class: "rm__dot", data: { on: "0", cur: "0" } }),
                  ),
              )
            : null;
        r.slots = m.slots
            ? el(
                  "div",
                  { class: "rm__slots", data: { part: "slots" } },
                  m.slots.map((s) => slotEl(s, { t })),
              )
            : null;
        r.sub = m.sub ? el("p", { class: "rm__sub", data: { part: "sub" } }) : null;
        r.sub2 = m.sub2 ? el("p", { class: "rm__sub", data: { part: "sub2" } }) : null;
        r.num =
            m.count != null && m.struct !== "success"
                ? el("span", { class: "rm__num", data: { part: "count" } })
                : null;
        r.aux = m.aux ? el("span", { class: "rm__aux", data: { part: "aux" } }) : null;
        const cnt =
            r.num || r.aux
                ? el(
                      "div",
                      { class: "rm__cnt" },
                      r.num,
                      r.num ? el("span", { class: "rm__unit", text: t("flow.rm.sec") }) : null,
                      r.aux,
                  )
                : null;
        const labels = m.bar
            ? el(
                  "div",
                  { class: "rm__bl", aria: { hidden: "true" } },
                  el("span", { style: { left: "0" }, text: "−60 dB" }),
                  zone
                      ? el("span", {
                            style: { left: `${(ratio(zone.lo, BAR_FLOOR) * 100).toFixed(1)}%` },
                            text: t("flow.rm.zone", { lo: fmtDb(zone.lo, 0), hi: fmtDb(zone.hi, 0) }),
                        })
                      : null,
                  el("span", { style: { right: "0" }, text: "0 dB" }),
              )
            : null;
        const foot =
            m.struct === "success"
                ? null
                : el(
                      "div",
                      { class: "rm__foot" },
                      el("button", {
                          type: "button",
                          class: "sc-btn rm__cancel",
                          text: t("flow.rm.cancel"),
                          data: { glow: "1", part: "cancel" },
                          on: { click: () => onCancel() },
                      }),
                  );
        const root = el(
            "div",
            { class: "rm", role: "group", data: { rtone: m.tone, step: m.step, struct: m.struct } },
            el(
                "div",
                { class: "rm__top" },
                el("div", { class: "brand" }, ic("mark", 18), el("span", { class: "brand__name", text: "Miciri" })),
                (r.eb = m.stepNo ? el("span", { class: "rm__eb" }) : null),
            ),
            el("div", { class: "rm__mid" }, r.icon, r.cmd, r.words, r.dots, r.slots, r.sub, r.sub2),
            cnt,
            bar,
            labels,
            foot,
        );
        r.root = root;
        host.replaceChildren(root);
        return r;
    }

    function patch(m) {
        setText(refs.cmd, tr(m.cmd));
        if (refs.words) setText(refs.words, t(m.words));
        if (refs.sub) setText(refs.sub, tr(m.sub));
        if (refs.sub2) setText(refs.sub2, tr(m.sub2));
        if (refs.num) setText(refs.num, String(m.count));
        if (refs.aux) setText(refs.aux, tr(m.aux));
        if (refs.eb) setText(refs.eb, t("flow.rm.step", { name: t(`flow.step.${STEP_KEY[m.step]}`), n: m.stepNo }));
        if (refs.dots && m.dots) {
            [...refs.dots.children].forEach((d, i) => {
                const on = i < m.dots.on ? "1" : "0";
                const cur = i + 1 === m.dots.cur ? "1" : "0";
                if (d.getAttribute("data-on") !== on) {
                    setAttr(d, "data-on", on);
                    d.replaceChildren(...(on === "1" ? [ic("check", 30)] : []));
                }
                setAttr(d, "data-cur", cur);
            });
        }
        if (refs.slots && m.slots) {
            [...refs.slots.children].forEach((node, i) => {
                if (node.getAttribute("data-s") !== m.slots[i]) node.replaceWith(slotEl(m.slots[i], { t }));
            });
        }
    }

    // ---- 电平条 ----
    function paintBar() {
        if (!refs || !refs.liq.isConnected) return;
        refs.liq.style.setProperty("--lv", `${(ratio(lvl.db, BAR_FLOOR) * 100).toFixed(2)}%`);
        refs.pk.style.setProperty("--pk", `${(ratio(lvl.peakDb, BAR_FLOOR) * 100).toFixed(2)}%`);
    }
    const frame = (ts) => {
        raf = 0;
        if (!shown || !refs || !refs.liq.isConnected) return;
        const dt = last ? Math.min(100, ts - last) : 16;
        last = ts;
        if (ts - lastEvent > SILENT_MS) target = { db: -80, peak: -80 };
        lvl = advance(lvl, target.db, target.peak, dt);
        paintBar();
        // 显示期间一直跑：远距离模式下液面是主要反馈，且只在测试进行中显示
        raf = requestAnimationFrame(frame);
    };
    const kick = () => {
        if (shown && !raf && refs && refs.liq.isConnected) raf = requestAnimationFrame(frame);
    };

    return {
        /** @param {object|null} m remoteModel 的返回值；null 表示隐藏 */
        render(m, bands) {
            if (!m) {
                this.hide();
                return;
            }
            const zone =
                m.zone && bands && Number.isFinite(bands.tpQuietDbfs) && Number.isFinite(bands.tpLoudDbfs)
                    ? { lo: bands.tpQuietDbfs, hi: bands.tpLoudDbfs }
                    : null;
            const sig = JSON.stringify([
                getLang(),
                m.tone,
                m.step,
                m.struct,
                !!m.icon,
                !!m.words,
                m.dots && m.dots.total,
                m.slots && m.slots.length,
                !!m.sub,
                !!m.sub2,
                m.count != null,
                !!m.aux,
                !!m.bar,
                !!zone,
                zone && zone.lo,
                zone && zone.hi,
            ]);
            if (sig !== shape || !refs || !refs.root.isConnected) {
                refs = build(m, zone);
                shape = sig;
            }
            patch(m);
            host.hidden = false;
            if (!shown) {
                shown = true;
                lvl = rest();
                last = 0;
            }
            kick();
        },
        hide() {
            shown = false;
            shape = "";
            refs = null;
            host.hidden = true;
            host.replaceChildren();
            if (raf) cancelAnimationFrame(raf);
            raf = 0;
        },
        /** miciri.meters 的载荷：两路里取响的那路（远距离条只有一条） */
        setMeter(m) {
            if (!m) return;
            lastEvent = performance.now();
            const db = Math.max(
                Number.isFinite(m.levelDbfsL) ? m.levelDbfsL : -80,
                Number.isFinite(m.levelDbfsR) ? m.levelDbfsR : -80,
            );
            const peak = Math.max(
                Number.isFinite(m.peakDbfsL) ? m.peakDbfsL : -80,
                Number.isFinite(m.peakDbfsR) ? m.peakDbfsR : -80,
            );
            target = { db, peak };
            kick();
        },
        get shown() {
            return shown;
        },
    };
}
