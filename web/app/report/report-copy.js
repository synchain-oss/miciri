// SPDX-License-Identifier: GPL-3.0-or-later
//
// 复制结果：先试剪贴板接口，再试选中文字加复制命令；宿主的网页容器两样都不给时，弹一个窗，
// 里面是已选中的只读文字，让用户手动复制。成功时回调一次，由页面给提示条。全程只用 DOM API。

import { el } from "../../shared/dom.js";
import { createModal } from "../../shared/modal.js";

async function writeClipboard(text, doc) {
    const nav = doc.defaultView && doc.defaultView.navigator;
    try {
        if (nav && nav.clipboard && typeof nav.clipboard.writeText === "function") {
            await nav.clipboard.writeText(text);
            return true;
        }
    } catch {
        // 没有权限或不是安全上下文：接着试下一种
    }
    try {
        const box = el("textarea", {
            readonly: "",
            tabindex: "-1",
            aria: { hidden: "true" },
            style: { position: "fixed", left: "-9999px", top: "0" },
        });
        box.value = text;
        doc.body.append(box);
        box.select();
        const ok = doc.execCommand("copy");
        box.remove();
        return ok === true;
    } catch {
        return false;
    }
}

/**
 * @param {{host: HTMLElement, inert: HTMLElement[], t: Function, onCopied: Function, doc?: Document}} o
 * @returns {{copy: (text: string) => Promise<boolean>}}
 */
export function createCopier(o) {
    const doc = o.doc || document;
    const modal = createModal({
        host: o.host,
        part: "copy-manual",
        width: 420,
        inert: o.inert,
        onEsc: () => modal.hide(),
        initialFocus: () => modal.panel.querySelector("textarea"),
    });
    return {
        async copy(text) {
            if (await writeClipboard(text, doc)) {
                o.onCopied();
                return true;
            }
            const title = el("h2", {
                class: "sc-modal__title",
                id: "copy-title",
                text: o.t("report.copy.manual.title"),
            });
            const box = el("textarea", {
                class: "mc-copy__text",
                readonly: "",
                rows: "3",
                aria: { label: o.t("report.copy.manual.title") },
            });
            box.value = text;
            modal.panel.setAttribute("aria-labelledby", "copy-title");
            modal.panel.replaceChildren(
                title,
                el("p", { class: "sc-modal__body", text: o.t("report.copy.manual.body") }),
                box,
                el(
                    "div",
                    { class: "mc-modal__actions" },
                    el("button", {
                        type: "button",
                        class: "sc-btn sc-btn--cta sc-btn--lg",
                        text: o.t("common.close"),
                        data: { glow: "1" },
                        on: { click: () => modal.hide() },
                    }),
                ),
            );
            modal.show();
            box.select();
            return false;
        },
    };
}
