// SPDX-License-Identifier: GPL-3.0-or-later
//
// 外壳：设计盒里的页眉、左栏与主区的挂载点、页脚、模态层与提示条，以及窗口缩放和语言。
// 只做外壳和共享组件；步骤条、测试面板、叠卡和报告由后面的页面往挂载点里填。
//
// 数据流：原生推 miciri.state（完整快照，整体替换），外壳只读；用户操作经桥回到原生，
// 界面不自己改状态，只对语言做乐观更新（失败回退）。缩放档位以快照里的 uiScale 为准。
//
// 挂载点（createShell 的返回值 regions）：header、steps（页眉中间的空位）、left、main、footer、
// layers.{detail,modal,tour,toast}。状态订阅：onState(cb)，登记时若已有快照会立刻回调一次。
//
// 全程只用 DOM API，文字走 textContent 或词条，不拼 HTML。

import { BRIDGE_API_VERSION } from "../../shared/bridge-api.js";
import { DESIGN } from "../../shared/design-box.js";
import { el, makeT, svg } from "../../shared/dom.js";
import { installGlow } from "../../shared/glow.js";
import { applyI18n, installDicts, LANGS } from "../../shared/i18n.js";
import { createModal } from "../../shared/modal.js";
import { installMenu } from "../../shared/menu.js";
import { createToast } from "../../shared/toast.js";
import { createScaleUi } from "../../shared/scale-ui.js";
import { createScaleConfirm } from "../../shared/shell-scale-confirm.js";
import { installShellFit } from "../../shared/shell-fit.js";
import strings from "./shell-strings.js";
import commonStrings from "../../shared/strings-common.js";

/** 页脚显示的版本号；打包时由发布流程替换。 */
export const SHELL_VERSION = "0.1.0";
const CONTACT = "contact@synchain.ca";
const MARK =
    "M8 2.2a2.5 2.5 0 0 1 2.5 2.5v3.2a2.5 2.5 0 0 1-5 0V4.7A2.5 2.5 0 0 1 8 2.2z|M3.7 8.4a4.3 4.3 0 0 0 8.6 0|M8 12.7V16|M15.8 6.7L9.8 12.7";
const HELP = "M6.5 6.9a2.6 2.6 0 1 1 4 2.2c-.9.6-1.5 1.1-1.5 2.2|M9 13.7h.01";

/** 18×18 画布上的描边图标，路径用竖线分段。 */
function icon(paths, size) {
    return svg(
        "svg",
        {
            viewBox: "0 0 18 18",
            width: size,
            height: size,
            fill: "none",
            "stroke-width": "1.6",
            "stroke-linecap": "round",
            "stroke-linejoin": "round",
            "aria-hidden": "true",
        },
        paths.split("|").map((d) => svg("path", { d })),
    );
}

function defaultLang(win) {
    const nav = (win && win.navigator && win.navigator.language) || "";
    return nav.toLowerCase().startsWith("zh") ? "zh" : "en";
}

/**
 * @param {{bridge: object, doc?: Document, win?: Window}} o
 * @returns 外壳对象，见文件头
 */
export function createShell(o) {
    const { bridge } = o;
    const doc = o.doc || document;
    const win = o.win || doc.defaultView;
    installDicts(commonStrings, strings);

    let lang = defaultLang(win);
    let state = null;
    const listeners = new Set();
    let t = makeT(lang);

    // ---- 骨架 ----
    const root = doc.getElementById("shell");
    const stepsSlot = el("div", { id: "steps", class: "mc-steps", data: { part: "steps" } });
    const langButtons = LANGS.map((l) =>
        el("button", {
            type: "button",
            class: "sc-seg__item",
            data: { lang: l, glow: "1" },
            text: l === "zh" ? "中文" : "EN",
            on: { click: () => chooseLang(l) },
        }),
    );
    const header = el(
        "header",
        { id: "hdr", class: "hdr", data: { part: "header" } },
        el(
            "div",
            { class: "brand" },
            icon(MARK, 18),
            el("span", { class: "brand__name", text: "Miciri" }),
            el("span", { class: "brand__cn", text: "见切" }),
        ),
        stepsSlot,
        el("div", { class: "sc-seg lang", role: "group", data: { "t-aria": "shell.lang.aria" } }, langButtons),
    );
    const left = el("section", { id: "lcol", class: "lcol", data: { part: "left" } });
    const main = el("main", { id: "main", class: "main", data: { part: "main" } });

    const scaleBtn = el("button", {
        type: "button",
        class: "sc-select",
        id: "scale-btn",
        data: { glow: "1", part: "scale-button", "t-aria": "shell.scale.aria" },
    });
    const scaleWrap = el("div", { class: "scalesel" }, scaleBtn);
    const versionBtn = el("button", {
        type: "button",
        class: "ver",
        data: { glow: "1", part: "version", "t-aria": "shell.about.aria" },
        on: { click: () => openAbout() },
    });
    const helpBtn = el(
        "button",
        {
            type: "button",
            class: "help",
            data: { glow: "1", part: "help", "t-aria": "shell.help.aria" },
            on: { click: () => root.dispatchEvent(new CustomEvent("miciri:help", { bubbles: true })) },
        },
        icon(HELP, 13),
    );
    const footer = el(
        "footer",
        { id: "ftr", class: "ftr", data: { part: "footer" } },
        el("span", { class: "ftr__hint", data: { t: "shell.hint" } }),
        el("div", { class: "ftr__right" }, scaleWrap, versionBtn, helpBtn),
    );

    const layers = {
        detail: el("div", { id: "layer-detail", class: "layer" }),
        modal: el("div", { id: "layer-modal", class: "layer" }),
        tour: el("div", { id: "layer-tour", class: "layer" }),
        toast: el("div", { id: "layer-toast", class: "layer" }),
    };
    root.append(
        ...[1, 2, 3].map((n) => el("i", { class: `sc-halo sc-halo--${n}`, data: { decor: "1" } })),
        header,
        left,
        main,
        footer,
        ...Object.values(layers),
    );

    // ---- 提示条：一次一条，4 秒消失 ----
    const toast = createToast({ host: layers.toast, timers: win });
    const showToast = (key, tone = "red") => toast.show({ key, tone, t });

    // ---- 缩放：档位下拉、10 秒确认回退 ----
    const bgNodes = [header, left, main];
    const scaleBridge = {
        setUiScale: async (f) => {
            const r = await bridge.setUiScale(f);
            if (r && r.ok === false)
                showToast(r.reason === "scale-locked" ? "shell.err.scaleLocked" : "shell.err.generic");
            return r;
        },
        commitUiScale: () => bridge.commitUiScale(),
    };
    const confirm = createScaleConfirm({
        host: layers.modal,
        inert: bgNodes,
        returnTo: () => scaleBtn,
        onKeep: () => scaleUi.keep(),
        onCancel: () => scaleUi.revert(),
    });
    const currentScale = () => (state && Number.isFinite(state.uiScale) && state.uiScale > 0 ? state.uiScale : 1);
    const scaleUi = createScaleUi({
        role: "shell",
        bridge: scaleBridge,
        getScale: currentScale,
        getScreen: () => ({ availWidth: win.screen.availWidth, availHeight: win.screen.availHeight }),
        onChange: (s) => {
            // 确认期间页脚抬到遮罩之上，缩放钮仍可点；再选档只重置倒计时
            root.setAttribute("data-keep-ftr", s.confirming ? "1" : "0");
            confirm.update({ ...s, t });
        },
        onError: () => showToast("shell.err.generic"),
    });
    installMenu({
        btn: scaleBtn,
        wrap: scaleWrap,
        part: "scale-menu",
        align: "right",
        items: () =>
            scaleUi.options().map((opt) => ({
                id: String(opt.factor),
                label: `${opt.factor}×`,
                note: opt.titleKey ? t(opt.titleKey) : undefined,
                selected: opt.factor === currentScale(),
                disabled: opt.disabled,
            })),
        onPick: (id) => {
            scaleUi.preview(Number(id));
        },
    });
    win.addEventListener("pagehide", () => scaleUi.pagehide());

    // ---- 关于窗 ----
    const aboutTitle = el("h2", { class: "sc-modal__title", id: "about-title", data: { t: "about.title" } });
    const aboutClose = el("button", {
        type: "button",
        class: "sc-btn sc-btn--cta sc-btn--lg",
        data: { glow: "1", t: "common.close" },
        on: { click: () => about.hide() },
    });
    const aboutLicense = el("button", {
        type: "button",
        class: "sc-btn sc-btn--dark",
        data: { glow: "1", t: "about.license" },
        on: { click: () => bridge.showLicense && bridge.showLicense() },
    });
    const about = createModal({
        host: layers.modal,
        part: "about",
        width: 420,
        inert: bgNodes.concat([footer]),
        onEsc: () => about.hide(),
        returnTo: () => versionBtn,
        initialFocus: () => aboutClose,
    });
    about.panel.setAttribute("aria-labelledby", aboutTitle.id);
    about.panel.append(
        aboutTitle,
        el("p", { class: "sc-modal__body", data: { t: "about.body" } }),
        el("p", { class: "sc-modal__body", data: { t: "about.gpl" } }),
        el(
            "p",
            { class: "sc-modal__body" },
            el("span", { data: { t: "about.contact" } }),
            el("span", { text: `：${CONTACT}`, class: "sc-num" }),
        ),
        el("div", { class: "mc-modal__actions" }, aboutLicense, aboutClose),
    );
    function openAbout() {
        about.show();
    }

    // ---- 渲染 ----
    function render() {
        t = makeT(lang);
        applyI18n(root, lang);
        for (const b of langButtons) b.setAttribute("aria-pressed", String(b.getAttribute("data-lang") === lang));
        scaleBtn.replaceChildren(el("span", { class: "sc-num", text: `${currentScale()}×` }));
        versionBtn.textContent = t("shell.version", { v: SHELL_VERSION });
    }

    async function chooseLang(next) {
        if (next === lang) return;
        const prev = lang;
        lang = next;
        render();
        let failed = false;
        try {
            const r = await bridge.setLang(next);
            failed = !!(r && r.ok === false);
        } catch {
            failed = true;
        }
        if (failed) {
            lang = prev;
            render();
            showToast("shell.err.generic");
        }
    }

    function setState(next) {
        if (!next || typeof next !== "object") return;
        state = next;
        if (LANGS.includes(next.lang)) lang = next.lang;
        render();
        for (const cb of listeners) cb(state);
    }

    const fit = installShellFit({ el: root, box: DESIGN.shell });
    installGlow(doc);
    render();

    return {
        root,
        regions: { header, steps: stepsSlot, left, main, footer, layers },
        apiVersion: BRIDGE_API_VERSION,
        t: (k, v) => t(k, v),
        getLang: () => lang,
        getState: () => state,
        setState,
        onState(cb) {
            listeners.add(cb);
            if (state) cb(state);
            return () => listeners.delete(cb);
        },
        showToast,
        openAbout,
        onBridgeError(e) {
            showToast(e && e.reason === "scale-locked" ? "shell.err.scaleLocked" : "shell.err.generic");
        },
        fit,
    };
}
