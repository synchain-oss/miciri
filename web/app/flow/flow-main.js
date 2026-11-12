// SPDX-License-Identifier: GPL-3.0-or-later
//
// 流程页入口：外壳就绪后把左栏输入卡、页眉步骤条、主区叠卡和远距离模式挂上去，并接上滚轮与 PageUp/PageDown 翻页。
//
// 数据流：页面只由快照驱动。快照到了就重算“该显示哪一页”，用户的操作经桥回到原生，结果再随快照回来；
// 界面自己记的只有三样：当前翻到哪一页、哪些步骤被点了“重测”（临时显示准备页）、刚完成的步骤（成功屏的计时）。
// 测试进行中整窗换成远距离模式，主区不重绘，结束后一次补画。
//
// 本文件须排在 shell-main.js 之前加载：外壳就绪的事件只派发一次。

import { el } from "../../shared/dom.js";
import { installDicts } from "../../shared/i18n.js";
import { createModal } from "../../shared/modal.js";
import commonStrings from "../../shared/strings-common.js";
import { createMeterCard } from "../meter/meter-ui.js";
import meterStrings from "../meter/meter-strings.js";
import shellStrings from "../shell/shell-strings.js";
import { connectModel, initialView, REPORT, remoteModel, visibleSteps } from "./flow-core.js";
import { renderStack, renderSteps } from "./flow-ui.js";
import flowStrings from "./flow-strings.js";
import { createRemote } from "./remote-ui.js";

/** 成功屏停留的时间（ms）；叠卡一格的高度（设计 px，与 flow.css 里主区的分配一致）。 */
const SUCCESS_MS = 1600;
const STACK_SLOT = 492;
/** 滚轮累计到这个量才翻一页，翻页后锁一小会儿，免得触控板的惯性连翻。 */
const WHEEL_STEP = 100;
const WHEEL_LOCK_MS = 450;

document.addEventListener("miciri:shell-ready", (e) => start(e.detail));

function start(shell) {
    const { bridge } = shell;
    const { layers } = shell.regions;
    // installDicts 是整体替换，不是追加：外壳已装的两份要连同本页的一起再装一次
    installDicts(commonStrings, shellStrings, flowStrings, meterStrings);
    const t = (k, v) => shell.t(k, v);

    // ---- 左栏 ----
    const meter = createMeterCard({ host: shell.regions.left, bridge, shell });

    // ---- 页面状态 ----
    let state = null;
    let view = null;
    const forcePrep = new Set();
    let finished = null;
    let successTimer = 0;
    let prevRunning = null;
    let animFrom = 0;
    let focusCur = false;
    let lastSig = "";
    let pagePending = true;

    const bridgeFailed = () => shell.showToast("shell.err.generic");
    const receipt = (r) => {
        if (r && r.ok === false) bridgeFailed();
        return r;
    };

    // ---- 远距离模式 ----
    const remoteHost = el("div", { id: "remote", class: "remote", hidden: true, data: { part: "remote" } });
    shell.root.append(remoteHost);
    const remote = createRemote({
        host: remoteHost,
        t,
        getLang: () => shell.getLang(),
        onCancel: () =>
            Promise.resolve()
                .then(() => bridge.cancelStep())
                .then(receipt)
                .catch(bridgeFailed),
    });
    bridge.on("miciri.meters", (m) => remote.setMeter(m));

    // ---- 动作 ----
    const pages = () => [...visibleSteps(state), REPORT];
    function go(id, animate = true) {
        if (!state || id === view || !pages().includes(id)) return;
        const from = pages().indexOf(view);
        const to = pages().indexOf(id);
        forcePrep.delete(view);
        animFrom = animate && from >= 0 ? Math.sign(to - from) * STACK_SLOT : 0;
        focusCur = true;
        view = id;
        renderAll();
    }
    const ctx = {
        t,
        forcePrep,
        go,
        start(id) {
            forcePrep.delete(id);
            return Promise.resolve()
                .then(() => bridge.startStep(id))
                .then(receipt)
                .catch(bridgeFailed);
        },
        retest(id) {
            forcePrep.add(id);
            focusCur = true;
            pagePending = true;
            renderAll();
        },
        async recheck() {
            try {
                const s = await bridge.requestInitialState();
                if (s && s.ok !== false) shell.setState(s);
            } catch {
                bridgeFailed();
            }
        },
        dismiss() {
            return Promise.resolve()
                .then(() => bridge.dismissChannelPrompt())
                .then(receipt)
                .catch(bridgeFailed);
        },
    };

    // ---- 声道提示窗（左栏药丸可点时打开，正文与接通页的诊断是同一份） ----
    const bg = [shell.regions.header, shell.regions.left, shell.regions.main, shell.regions.footer];
    const chModal = createModal({
        host: layers.modal,
        part: "channel-prompt",
        width: 420,
        inert: bg,
        onEsc: () => chModal.hide(),
        returnTo: () => meter.pillButton,
        initialFocus: () => chModal.panel.querySelector("[data-part='chp-close']"),
    });
    function openChannelPrompt() {
        const m = state && connectModel(state);
        if (!m || !m.diag) return;
        const names = { ch: t(`flow.ch.${m.lane}`), other: t(`flow.ch.${m.other}`), sec: m.sec };
        const title = el("h2", { class: "sc-modal__title", id: "chp-title", text: t(m.diag.t) });
        chModal.panel.setAttribute("aria-labelledby", "chp-title");
        chModal.panel.replaceChildren(
            title,
            el("p", { class: "sc-modal__body", text: t(m.diag.b, names) }),
            el(
                "div",
                { class: "mc-modal__actions" },
                m.diag.dismiss
                    ? el("button", {
                          type: "button",
                          class: "sc-btn sc-btn--text",
                          text: t("flow.btn.nomore"),
                          data: { glow: "1", part: "chp-dismiss" },
                          on: {
                              click: () => {
                                  chModal.hide();
                                  ctx.dismiss();
                              },
                          },
                      })
                    : null,
                el("button", {
                    type: "button",
                    class: "sc-btn sc-btn--cta sc-btn--lg",
                    text: t("common.close"),
                    data: { glow: "1", part: "chp-close" },
                    on: { click: () => chModal.hide() },
                }),
            ),
        );
        chModal.show();
    }
    meter.onPillAction(openChannelPrompt);

    // ---- 渲染 ----
    const signature = () =>
        JSON.stringify([
            view,
            shell.getLang(),
            [...forcePrep],
            state.micType,
            state.device,
            state.connect,
            state.steps,
            state.results,
            state.bands,
            state.live && [state.live.claps, state.live.clapsNeeded, state.live.clapsMax],
        ]);

    function renderAll() {
        if (!state) return;
        const ids = pages();
        if (!view || !ids.includes(view)) view = initialView(state);
        const model = remoteModel(state, { justFinished: finished });
        shell.root.setAttribute("data-remote", model ? "1" : "0");
        remote.render(model, state.bands);
        if (model) {
            pagePending = true;
            return;
        }
        const sig = signature();
        if (!pagePending && sig === lastSig) return;
        pagePending = false;
        lastSig = sig;
        renderSteps(shell.regions.steps, state, view, ctx);
        const cur = renderStack(shell.regions.main, state, view, ctx, animFrom);
        animFrom = 0;
        if (focusCur) {
            focusCur = false;
            cur.focus({ preventScroll: true });
        }
    }

    shell.onState((s) => {
        state = s;
        meter.setState(s);
        const ids = visibleSteps(s);
        const running = ids.find((id) => s.steps && s.steps[id] && s.steps[id].status === "running") || null;
        if (running) {
            // 开始测试：翻到那一页，旧的成功屏作废
            clearTimeout(successTimer);
            finished = null;
            view = running;
            forcePrep.delete(running);
        } else if (prevRunning) {
            // 刚结束：成功了先显示成功屏，然后落在这一步的结果页；取消或作废就直接落回这一页
            view = prevRunning;
            focusCur = true;
            if (s.steps[prevRunning] && s.steps[prevRunning].status === "done") {
                finished = prevRunning;
                clearTimeout(successTimer);
                successTimer = setTimeout(() => {
                    finished = null;
                    pagePending = true;
                    renderAll();
                }, SUCCESS_MS);
            }
        }
        prevRunning = running;
        renderAll();
    });

    // ---- 翻页：滚轮与 PageUp/PageDown ----
    const modalOpen = () => !!layers.modal.querySelector(".sc-scrim:not([hidden])");
    const idle = () => state && !remote.shown && !modalOpen();
    const step = (dir) => {
        const ids = pages();
        const j = ids.indexOf(view) + dir;
        if (j >= 0 && j < ids.length) go(ids[j]);
        return j >= 0 && j < ids.length;
    };
    let acc = 0;
    let lockUntil = 0;
    let lastWheel = 0;
    shell.regions.main.addEventListener(
        "wheel",
        (e) => {
            if (!idle() || e.ctrlKey) return;
            const now = performance.now();
            if (now < lockUntil) {
                acc = 0;
                return;
            }
            if (now - lastWheel > 300) acc = 0;
            lastWheel = now;
            acc += e.deltaMode === 1 ? e.deltaY * 16 : e.deltaY;
            if (Math.abs(acc) < WHEEL_STEP) return;
            const dir = acc > 0 ? 1 : -1;
            acc = 0;
            lockUntil = now + WHEEL_LOCK_MS;
            step(dir);
        },
        { passive: true },
    );
    document.addEventListener("keydown", (e) => {
        if (e.key !== "PageDown" && e.key !== "PageUp") return;
        if (!idle() || /^(INPUT|SELECT|TEXTAREA)$/.test((e.target && e.target.tagName) || "")) return;
        if (step(e.key === "PageDown" ? 1 : -1)) e.preventDefault();
    });
}
