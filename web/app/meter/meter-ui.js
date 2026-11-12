// SPDX-License-Identifier: GPL-3.0-or-later
//
// 左栏输入卡：玻璃管电平表（单声道一根、立体声两根）、削波锁存灯、真峰值读数、信号状态药丸、
// 采样率与缓冲与声道小标签、话筒类型菜单，以及测完后管内的结果标记和悬停提示。
//
// 数据：状态来自快照（setState），电平来自 miciri.meters 事件。电平事件只更新目标值，
// 画面由 requestAnimationFrame 逐帧推进弹道；液面和峰值线都回到底之后停掉循环（空闲时零开销）。
// 全程只用 DOM API，文字走词条。

import { el } from "../../shared/dom.js";
import { installMenu } from "../../shared/menu.js";
import {
    advance,
    analysedLane,
    atRest,
    fmtDb,
    layoutLabels,
    markList,
    ratio,
    rest,
    signalPill,
    targetZone,
    TICKS,
    TUBE_PAD,
} from "./meter-core.js";

const MICS = [
    ["unknown", "meter.mic.unknown"],
    ["dynamic", "meter.mic.dyn"],
    ["condenser", "meter.mic.cond"],
    ["usb", "meter.mic.usb"],
    ["headset", "meter.mic.headset"],
];
const LANES = ["L", "R"];

/**
 * @param {{host: HTMLElement, bridge: object, shell: object}} o shell 提供 t、onState、showToast
 * @returns {{setState: (s: object) => void, onPillAction: (cb: Function) => void, pillButton: HTMLElement}}
 */
export function createMeterCard(o) {
    const { host, bridge, shell } = o;
    const t = (k, v) => shell.t(k, v);
    let state = null;
    let pillAction = () => {};
    let signature = "";

    // ---- 骨架 ----
    const lanes = {};
    const tube = (id) => {
        const lamp = el("button", {
            type: "button",
            class: "lt__lamp",
            data: { ch: id, on: "0", glow: "1" },
            aria: { pressed: "false" },
            on: { click: () => clearLatch() },
        });
        const liquid = el("i", { class: "lt__liquid" });
        const peak = el("i", { class: "lt__peak" });
        const bands = el("div", { class: "lt__bands", aria: { hidden: "true" } });
        const zone = el("i", { class: "lt__zone" });
        const slot = el("div", { class: "lt__slot" }, liquid, bands, peak, zone);
        const lbl = el("span", { class: "lt__lbl", text: id, aria: { hidden: "true" } });
        const node = el(
            "div",
            { class: "lt", data: { ch: id } },
            lamp,
            el("div", { class: "lt__tube", data: { ch: id } }, slot, el("i", { class: "lt__gloss" })),
            lbl,
        );
        lanes[id] = { node, lamp, liquid, peak, bands, zone, lvl: rest(), target: -80, targetPeak: -80, clip: false };
        return node;
    };
    const scale = el(
        "div",
        { class: "lt-scale", aria: { hidden: "true" } },
        TICKS.map((db) => el("span", { class: "lt-tick", text: fmtDb(db, 0), style: { "--r": String(ratio(db)) } })),
    );
    const zoneLabel = el("div", { class: "lt-zonelbl", aria: { hidden: "true" } });
    const zoneText = el("span", { data: { k: "zone" } });
    const marks = el("div", { class: "lt-marks" });
    zoneLabel.append(zoneText, marks);
    const tip = el("div", { class: "lt-tip", role: "tooltip", hidden: true });
    // 电平表里有可点的削波灯（button）。role=img 的子节点按规范是展示性的，里面不能再放可聚焦控件（axe nested-interactive），
    // 所以外层用 group：名字照旧带电平表与结果标记，削波灯作为独立的按钮被读到。
    const meter = el(
        "div",
        { class: "meter", role: "group", data: { ch: "mono", part: "meter" } },
        scale,
        tube("L"),
        tube("R"),
        zoneLabel,
        tip,
    );

    const pillDot = el("i", { class: "sc-dot" });
    const pillText = el("span");
    const pillAct = el("button", {
        type: "button",
        class: "sc-pill__act",
        hidden: true,
        data: { glow: "1", part: "pill-action" },
        on: { click: () => pillAction(pill_ref()) },
    });
    const pill = el(
        "span",
        { class: "sc-pill", role: "status", aria: { live: "polite" }, data: { part: "signal-pill", tone: "gray" } },
        pillDot,
        pillText,
        pillAct,
    );
    let pillInfo = null;
    const pill_ref = () => pillInfo;

    const readout = el("b", { class: "readout__val", data: { part: "true-peak" } });
    const chipRate = el("span", { class: "sc-chip", data: { part: "chip-rate" } });
    const chipBuf = el("span", { class: "sc-chip", data: { part: "chip-buffer" } });
    const chipCh = el("span", { class: "sc-chip", data: { part: "chip-channels" } });
    const micBtn = el("button", {
        type: "button",
        class: "sc-select",
        id: "mic-sel",
        data: { glow: "1", part: "mic-button" },
    });
    const micWrap = el("div", { class: "lcard__sel" }, micBtn);
    const headLabel = el("span", { class: "sc-eyebrow" });
    const tpLabel = el("span", { class: "sc-eyebrow" });
    const unit = el("span", { class: "readout__unit", text: "dBTP" });
    const micLabel = el("label", { class: "sc-eyebrow", for: "mic-sel" });
    const card = el(
        "div",
        { class: "lcard sc-glass", data: { part: "input-card" } },
        el("div", { class: "lcard__head" }, headLabel, pill),
        el("div", {}, meter),
        el("div", { class: "readout" }, tpLabel, el("span", {}, readout, unit)),
        el("div", { class: "lcard__chips" }, chipRate, chipBuf, chipCh),
        el("div", { class: "lcard__field" }, micLabel, micWrap),
    );
    host.append(card);

    installMenu({
        btn: micBtn,
        wrap: micWrap,
        part: "mic-menu",
        align: "left",
        items: () => MICS.map(([id, key]) => ({ id, label: t(key), selected: !!state && state.micType === id })),
        onPick: (id) => {
            Promise.resolve()
                .then(() => bridge.setMicType(id))
                .then((r) => {
                    if (r && r.ok === false) shell.showToast("shell.err.generic");
                })
                .catch(() => shell.showToast("shell.err.generic"));
        },
    });

    function clearLatch() {
        Promise.resolve()
            .then(() => bridge.clearClipLatch())
            .catch(() => shell.showToast("shell.err.generic"));
    }

    // ---- 逐帧推进 ----
    let raf = 0;
    let last = 0;
    let lastEvent = 0;
    // 电平事件停了（编辑器还开着、宿主不走带）就当作回到底，让液面落下去并停掉循环
    const SILENT_MS = 250;
    const frame = (ts) => {
        raf = 0;
        const dt = last ? Math.min(100, ts - last) : 16;
        last = ts;
        let moving = false;
        const silent = ts - lastEvent > SILENT_MS;
        for (const id of LANES) {
            const ln = lanes[id];
            if (silent) {
                ln.target = -80;
                ln.targetPeak = -80;
            }
            ln.lvl = advance(ln.lvl, ln.target, ln.targetPeak, dt);
            moving = moving || !atRest(ln.lvl) || ln.target > -80;
        }
        paint();
        if (moving) raf = requestAnimationFrame(frame);
        else last = 0;
    };
    const kick = () => {
        if (!raf) raf = requestAnimationFrame(frame);
    };

    function paint() {
        const bands = state && state.bands;
        let peak = -80;
        for (const id of LANES) {
            const ln = lanes[id];
            ln.liquid.style.setProperty("--lv", `${(ratio(ln.lvl.db) * 100).toFixed(2)}%`);
            ln.peak.style.setProperty("--pk", `${(ratio(ln.lvl.peakDb) * 100).toFixed(2)}%`);
            const alert = !!bands && ln.lvl.peakDb > bands.tpTooLoudDbfs;
            if (alert) ln.peak.setAttribute("data-alert", "1");
            else ln.peak.removeAttribute("data-alert");
            if (id === "L" || meter.dataset.ch === "st") peak = Math.max(peak, ln.lvl.peakDb);
        }
        readout.textContent = (peak > 0 ? "+" : "") + fmtDb(peak);
    }

    bridge.on("miciri.meters", (m) => {
        if (!m) return;
        lastEvent = performance.now();
        const mono = !state || !state.device || state.device.layout !== "stereo";
        lanes.L.target = m.levelDbfsL;
        lanes.L.targetPeak = m.peakDbfsL;
        lanes.R.target = mono ? -80 : m.levelDbfsR;
        lanes.R.targetPeak = mono ? -80 : m.peakDbfsR;
        lanes.L.clip = !!m.clipLatchL;
        lanes.R.clip = !mono && !!m.clipLatchR;
        for (const id of LANES) {
            lanes[id].lamp.setAttribute("data-on", lanes[id].clip ? "1" : "0");
            lanes[id].lamp.setAttribute("aria-pressed", String(lanes[id].clip));
        }
        kick();
    });

    // ---- 悬停提示 ----
    function hideTip() {
        tip.hidden = true;
        for (const b of meter.querySelectorAll(".lt-bd[data-hover]")) b.removeAttribute("data-hover");
    }
    function showTip(id) {
        hideTip();
        const zone = targetZone(state && state.bands);
        const mk = currentMarks.find((m) => m.id === id);
        if (id === "zone" ? !zone : !mk) return;
        const key = (k) => el("div", {}, el("i", { text: t(k) }));
        const val = (k, v) => el("div", {}, el("i", { text: t(k) }), el("b", { text: v }));
        const db = (k, v) => val(k, `${fmtDb(v)} dBFS`);
        let rows;
        if (id === "zone") {
            rows = [
                key("meter.in.target"),
                el("div", {}, el("b", { text: `${fmtDb(zone.lo, 0)}..${fmtDb(zone.hi, 0)} dBTP` })),
                key("meter.mk.tip.zone"),
            ];
        } else if (id === "mouth") {
            rows = [key("meter.mk.mouth"), db("meter.mk.tip.loudest", mk.mean)];
        } else {
            rows = [
                mk.f ? val(`meter.mk.${id}`, `${Math.round(mk.f)} Hz`) : key(`meter.mk.${id}`),
                db("meter.mk.tip.mean", mk.mean),
                db("meter.mk.tip.peak", mk.peak),
            ];
        }
        if (mk && mk.stale) rows.push(key("meter.rc.stale"));
        tip.replaceChildren(...rows);
        tip.hidden = false;
        const band = meter.querySelector(`.lt-bd[data-k="${id}"]`);
        if (band) band.setAttribute("data-hover", "1");
        // 提示挂在电平区右上：y 从管底算，加上管底到电平区底边的距离，再限制在电平区顶部以内
        const zl = marks.parentNode;
        const gap = meter.offsetHeight - zl.offsetTop - zl.offsetHeight;
        const mid = zone ? (zone.lo + zone.hi) / 2 : 0;
        const y = id === "zone" ? TUBE_PAD + ratio(mid) * (marks.offsetHeight - 2 * TUBE_PAD) : mk.y;
        tip.style.setProperty("--y", `${Math.min(gap + y + 10, meter.offsetHeight - tip.offsetHeight).toFixed(1)}px`);
    }
    meter.addEventListener("pointerover", (e) => {
        const hit = e.target.closest(".lt-bd, .lt-mk, .lt-mk__lb, .lt__zone, .lt-zonelbl > span");
        if (hit) showTip(hit.dataset.k || "zone");
        else hideTip();
    });
    meter.addEventListener("pointerleave", hideTip);

    // ---- 结果标记 ----
    let currentMarks = [];
    function renderMarks() {
        const tubeH = lanes.L.node.querySelector(".lt__tube").offsetHeight || 356;
        const list = markList(state);
        const sig = JSON.stringify([list, analysedLane(state), tubeH]);
        if (sig === signature) return;
        signature = sig;
        hideTip();
        const ys = layoutLabels(list, tubeH - 8);
        currentMarks = list.map((m, i) => ({ ...m, y: ys[i] }));
        marks.replaceChildren();
        for (const id of LANES) lanes[id].bands.replaceChildren();
        const lane = analysedLane(state);
        currentMarks.forEach((m) => {
            const tone = m.stale ? "gray" : m.tone;
            const st = m.stale ? "stale" : "fresh";
            if (m.kind === "dot")
                marks.append(
                    el("i", { class: "lt-mk", data: { tone, st, k: m.id }, style: { "--r": String(ratio(m.mean)) } }),
                );
            marks.append(
                el(
                    "div",
                    { class: "lt-mk__lb", data: { tone, st, k: m.id }, style: { "--y": `${m.y.toFixed(1)}px` } },
                    el("i", { text: t(`meter.mk.${m.id}`) }),
                    el("b", { text: fmtDb(m.mean) }),
                ),
            );
        });
        currentMarks
            .filter((m) => m.kind === "band")
            .sort((a, b) => b.mean - a.mean)
            .forEach((m) => {
                const mean = ratio(m.mean);
                lanes[lane].bands.append(
                    el("i", {
                        class: `lt-bd lt-bd--${m.id}`,
                        data: { tone: m.stale ? "gray" : m.tone, st: m.stale ? "stale" : "fresh", k: m.id, ch: lane },
                        style: {
                            "--m": String(mean),
                            "--p": `${((ratio(m.peak) / Math.max(mean, 1e-6)) * 100).toFixed(2)}%`,
                        },
                    }),
                );
            });
    }

    // ---- 渲染 ----
    function setState(next) {
        if (!next) return;
        state = next;
        const stereo = !!next.device && next.device.layout === "stereo";
        const zone = targetZone(next.bands);
        meter.dataset.ch = stereo ? "st" : "mono";
        headLabel.textContent = t("meter.in.title");
        tpLabel.textContent = t("meter.in.tp");
        micLabel.textContent = t("meter.in.mic");
        const baseAria = t(stereo ? "meter.in.meter.aria.st" : "meter.in.meter.aria");
        lanes.L.lamp.setAttribute("aria-label", t(stereo ? "meter.in.clip.aria.L" : "meter.in.clip.aria"));
        lanes.R.lamp.setAttribute("aria-label", t("meter.in.clip.aria.R"));
        // 目标区
        for (const id of LANES) {
            if (zone) {
                lanes[id].zone.style.setProperty("--zlo", String(ratio(zone.lo)));
                lanes[id].zone.style.setProperty("--zhi", String(ratio(zone.hi)));
            }
            lanes[id].zone.hidden = !zone;
        }
        zoneText.hidden = !zone;
        if (zone) {
            zoneText.style.setProperty("--zmid", String(ratio((zone.lo + zone.hi) / 2)));
            zoneText.replaceChildren(
                el("div", { text: t("meter.in.target") }),
                el("div", { class: "sc-num", text: `${fmtDb(zone.lo, 0)}..${fmtDb(zone.hi, 0)}` }),
            );
        }
        // 药丸
        pillInfo = signalPill(next);
        const chName = pillInfo.ch ? t(`meter.ch.${pillInfo.ch}`) : "";
        pill.dataset.tone = pillInfo.tone;
        pillDot.dataset.pulse = pillInfo.pulse ? "1" : "0";
        pill.dataset.act = pillInfo.act ? "1" : "0";
        pillText.textContent = t(`meter.${pillInfo.key}`, { ch: chName });
        pillAct.hidden = !pillInfo.act;
        // 隐藏时按钮也要有名字（读屏和冒烟都按“按钮必有名字”核对）：不可点时取药丸自己的文字
        const ariaKey = { "sig.one": "meter.sig.one.aria", "sig.anti": "meter.sig.anti.aria" }[pillInfo.key];
        pillAct.setAttribute("aria-label", ariaKey ? t(ariaKey, { ch: chName }) : pillText.textContent);
        // 小标签
        const dev = next.device || {};
        chipRate.textContent = dev.sampleRate ? `${+(dev.sampleRate / 1000).toFixed(1)} kHz` : "";
        chipBuf.textContent = dev.bufferSize ? t("meter.in.buf", { n: dev.bufferSize }) : "";
        chipCh.textContent = t(stereo ? "meter.in.ch.st" : "meter.in.ch.mono");
        // 话筒类型
        const mic = MICS.find(([id]) => id === next.micType) || MICS[0];
        micBtn.replaceChildren(el("span", { text: t(mic[1]) }));
        micBtn.setAttribute("aria-label", `${t("meter.in.mic")} ${t(mic[1])}`);
        // 标记与 aria
        signature = "";
        renderMarks();
        const marksAria = currentMarks
            .map((m) => `${t(`meter.mk.${m.id}`)} ${fmtDb(m.mean)}${m.stale ? ` ${t("meter.rc.stale")}` : ""}`)
            .join(t("meter.mk.sep"));
        meter.setAttribute(
            "aria-label",
            currentMarks.length ? t("meter.in.meter.aria.m", { base: baseAria, marks: marksAria }) : baseAria,
        );
        paint();
    }

    return {
        setState,
        onPillAction: (cb) => {
            pillAction = cb;
        },
        pillButton: pillAct,
    };
}
