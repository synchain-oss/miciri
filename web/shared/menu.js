// SPDX-License-Identifier: GPL-3.0-or-later
//
// 展开菜单：缩放下拉和话筒类型下拉共用的深色玻璃菜单，替代原生下拉。
// 原生下拉的弹层是系统自绘的，页面样式改不到，也不跟窗口缩放；所以一律自绘。
//
// 交互（listbox 语义）：
//   - 触发钮 aria-haspopup=listbox、aria-expanded；菜单一律向上展开（两个宿主的页脚都贴着窗口底部）；
//   - 展开后焦点落在当前项；↑ ↓ 在可选项里循环，Enter 或点击选中，Esc 关闭并把焦点还给触发钮，
//     Tab 关闭并把焦点还给触发钮；点菜单外面关闭并还焦点；
//   - 灰掉的项（aria-disabled）不能选，但仍显示，右边带注记说明原因；
//   - 带 Ctrl、Cmd、Alt 的组合键一律放行：那是宿主与系统的地盘；
//   - 选中当前项只关闭，不触发 onPick（与原生下拉同值不派发 change 一致）。
// 全程只用 DOM API，文字走 textContent。

import { el, svg } from "./dom.js";

const CHECK_PATH = "M4.2 9.4l3 3 6.6-6.8";

function checkIcon() {
    return svg(
        "svg",
        {
            viewBox: "0 0 18 18",
            width: 14,
            height: 14,
            fill: "none",
            "stroke-width": "1.6",
            "stroke-linecap": "round",
            "stroke-linejoin": "round",
            "aria-hidden": "true",
        },
        svg("path", { d: CHECK_PATH }),
    );
}

/**
 * 装一个菜单按钮。菜单节点在每次展开时现建、关闭时移除，不常驻 DOM。
 * @param {object} o
 * @param {HTMLElement} o.btn 触发钮，必须有 id（菜单用它做 aria-labelledby）
 * @param {HTMLElement} o.wrap 触发钮的定位容器（position: relative），菜单挂在它下面
 * @param {string} o.part 菜单的 data-part
 * @param {()=>Array<{id:string, label:string, note?:string, selected?:boolean, disabled?:boolean}>} o.items
 *   每次展开时取一遍，所以超屏判据、当前项总是最新的；label 与 note 已是当前语言的文字
 * @param {(id:string)=>void} o.onPick
 * @param {"left"|"right"} [o.align] 菜单贴齐触发钮的哪一侧，缺省 right
 */
export function installMenu(o) {
    const { btn, wrap } = o;
    let list = null;
    btn.setAttribute("aria-haspopup", "listbox");
    btn.setAttribute("aria-expanded", "false");

    const isOpen = () => list !== null;

    function close(focusBtn) {
        if (!list) return;
        list.remove();
        list = null;
        btn.setAttribute("aria-expanded", "false");
        document.removeEventListener("pointerdown", onOutside, true);
        if (focusBtn) btn.focus({ preventScroll: true });
    }

    function onOutside(e) {
        if (!list || list.contains(e.target) || btn.contains(e.target)) return;
        close(true);
    }

    function pick(item) {
        if (item.disabled) return;
        close(true);
        if (!item.selected) o.onPick(item.id);
    }

    function open() {
        const items = o.items();
        list = el("ul", {
            class: "menu dk",
            role: "listbox",
            tabindex: "-1",
            aria: { labelledby: btn.id },
            data: { part: o.part, align: o.align || "right" },
        });
        const rows = items.map((item) => {
            const row = el(
                "li",
                {
                    role: "option",
                    tabindex: "-1",
                    aria: { selected: String(!!item.selected), disabled: String(!!item.disabled) },
                    data: { id: item.id },
                    on: { click: () => pick(item) },
                },
                el("span", { text: item.label }),
                item.note ? el("small", { text: item.note }) : null,
                item.selected ? checkIcon() : null,
            );
            return row;
        });
        list.append(...rows);
        wrap.append(list);
        btn.setAttribute("aria-expanded", "true");
        document.addEventListener("pointerdown", onOutside, true);
        (rows.find((r) => r.getAttribute("aria-selected") === "true") || list).focus({ preventScroll: true });
        list.addEventListener("keydown", (e) => {
            if (e.ctrlKey || e.metaKey || e.altKey) return;
            const live = rows.filter((r) => r.getAttribute("aria-disabled") !== "true");
            const at = live.indexOf(document.activeElement);
            if (e.key === "ArrowUp" || e.key === "ArrowDown") {
                e.preventDefault();
                if (live.length === 0) return;
                const step = e.key === "ArrowDown" ? 1 : -1;
                const next = at < 0 ? (step > 0 ? 0 : live.length - 1) : (at + step + live.length) % live.length;
                live[next].focus({ preventScroll: true });
            } else if (e.key === "Enter" || e.key === " ") {
                e.preventDefault();
                if (at >= 0) live[at].click();
            } else if (e.key === "Escape") {
                // 这一下 Esc 只归菜单，不冒泡到页面级的处理器
                e.preventDefault();
                e.stopPropagation();
                close(true);
            } else if (e.key === "Tab") {
                e.preventDefault();
                close(true);
            }
        });
    }

    btn.addEventListener("click", () => (isOpen() ? close(true) : open()));
    btn.addEventListener("keydown", (e) => {
        if (e.ctrlKey || e.metaKey || e.altKey) return;
        // 方向键只展开，不直接换档：换档会立刻改窗口尺寸并起倒计时，不该由一次方向键顺手触发
        if ((e.key === "ArrowUp" || e.key === "ArrowDown") && !isOpen()) {
            e.preventDefault();
            open();
        }
    });

    return { open, close: () => close(false), isOpen };
}
