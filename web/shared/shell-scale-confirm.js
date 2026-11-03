// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from synchain-oss/rinne; see upstream/web.json
//
// 缩放确认窗：选档后窗口已经按新档位改过，10 秒内不点保存就回退。
// 状态机在 scale-ui.js（createScaleUi），本组件只画框、转发按钮，并显示剩余秒数：
// 页面把状态机的 onChange 接到 update()，把 onKeep、onCancel 接到 scaleUi.keep()、scaleUi.revert()。
//
// 关闭后焦点回到页脚的缩放按钮（returnTo）。页脚在确认期间保持可点，由页面用 data-keep-ftr 抬到遮罩之上；
// 窗口内的 Tab 在两个按钮之间循环，键盘进不了缩放控件，再选档只能用鼠标。
// Esc 等于取消（回退），不等于保存：不确认就当没改。

import { el } from "./dom.js";
import { createModal } from "./modal.js";
import { SCALE_KEYS } from "./scale-ui.js";

const WIDTH = 340;

/**
 * @param {{host:HTMLElement, inert?:HTMLElement[], returnTo?:()=>(HTMLElement|null),
 *   onKeep:()=>void, onCancel:()=>void}} o
 */
export function createScaleConfirm(o) {
    const title = el("h2", { class: "sc-modal__title", id: "scale-confirm-title" });
    const body = el("div", { class: "sc-modal__body", data: { part: "scale-body" } });
    const cancel = el("button", {
        type: "button",
        class: "sc-btn sc-btn--dark",
        data: { glow: "1", part: "confirm-cancel" },
        on: { click: () => o.onCancel() },
    });
    const keep = el("button", {
        type: "button",
        class: "sc-btn sc-btn--cta sc-btn--lg",
        data: { glow: "1", part: "confirm-keep" },
        on: { click: () => o.onKeep() },
    });
    const modal = createModal({
        host: o.host,
        part: "scale-confirm",
        width: WIDTH,
        inert: o.inert,
        onEsc: () => o.onCancel(),
        returnTo: o.returnTo,
        initialFocus: () => keep,
    });
    modal.panel.setAttribute("aria-labelledby", title.id);
    modal.panel.append(title, body, el("div", { class: "mc-modal__actions" }, cancel, keep));
    return {
        el: modal.panel,
        /** @param {{confirming:boolean, left:number, t:(k:string,v?:object)=>string}} p 状态机 onChange 的载荷加 t */
        update({ confirming, left, t }) {
            if (!confirming) {
                modal.hide();
                return;
            }
            modal.panel.setAttribute("data-state", `countdown-${left}`);
            title.textContent = t("common.scale.title");
            body.textContent = t(SCALE_KEYS.confirmBody, { s: left });
            cancel.textContent = t(SCALE_KEYS.revert);
            keep.textContent = t(SCALE_KEYS.keep);
            modal.show();
        },
        isShown: modal.isShown,
    };
}
