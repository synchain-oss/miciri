// SPDX-License-Identifier: GPL-3.0-or-later
//
// 流程页用的描边图标（18×18 画布，1.6 描边）。路径用竖线分段，段首的“~”表示虚线段。
// 不用 emoji 或字符当图标。

import { svg } from "../../shared/dom.js";

const PATHS = {
    mark: "M8 2.2a2.5 2.5 0 0 1 2.5 2.5v3.2a2.5 2.5 0 0 1-5 0V4.7A2.5 2.5 0 0 1 8 2.2z|M3.7 8.4a4.3 4.3 0 0 0 8.6 0|M8 12.7V16|M15.8 6.7L9.8 12.7",
    check: "M4.2 9.4l3 3 6.6-6.8",
    circle: "M9 2.6a6.4 6.4 0 1 0 0 12.8A6.4 6.4 0 0 0 9 2.6z",
    "circle-dot": "M9 2.6a6.4 6.4 0 1 0 0 12.8A6.4 6.4 0 0 0 9 2.6z|M9 7.2a1.8 1.8 0 1 0 0 3.6 1.8 1.8 0 0 0 0-3.6z",
    "circle-check": "M9 2.6a6.4 6.4 0 1 0 0 12.8A6.4 6.4 0 0 0 9 2.6z|M6 9.2l2.2 2.2 3.9-4.2",
    "circle-x": "M9 2.6a6.4 6.4 0 1 0 0 12.8A6.4 6.4 0 0 0 9 2.6z|M6.6 6.6l4.8 4.8|M11.4 6.6l-4.8 4.8",
    stale: "~M9 2.6a6.4 6.4 0 1 0 0 12.8A6.4 6.4 0 0 0 9 2.6z|M9 5.6V9l2.3 1.5",
    alert: "M9 2.8L16 15H2z|M9 7.4v3.6|M9 13.1h.01",
    info: "M9 2.6a6.4 6.4 0 1 0 0 12.8A6.4 6.4 0 0 0 9 2.6z|M9 8.2v4|M9 5.7h.01",
    "arrow-r": "M3 9h11|M10 5l4 4-4 4",
    retry: "M14.6 8.2A5.8 5.8 0 1 0 13 12.8|M14.8 3.6v4.4h-4.4",
    gain: "M3 14.5V9M7 14.5V5.5M11 14.5v-7M15 14.5V3.5",
    quiet: "M2.5 9h13|M5 7v4|M9 6v6|M13 7.4v3.2",
    ripple: "M9 9h.01|M6.2 11.8a4 4 0 0 1 0-5.6|M11.8 6.2a4 4 0 0 1 0 5.6|M3.8 14.2a7.4 7.4 0 0 1 0-10.4|M14.2 3.8a7.4 7.4 0 0 1 0 10.4",
    pos: "M5 12.5a3.2 3.2 0 0 1 3.2-3.2h1.6A3.2 3.2 0 0 1 13 12.5|M9 2.8v2.4|M6.4 5.6h5.2|M2.8 15.4h12.4",
    dir: "M9 2.5v3|M9 12.5v3|M2.5 9h3|M12.5 9h3|M9 7.4a1.6 1.6 0 1 0 0 3.2 1.6 1.6 0 0 0 0-3.2z",
    mic: "M9 2.2a2.6 2.6 0 0 1 2.6 2.6v3.4a2.6 2.6 0 0 1-5.2 0V4.8A2.6 2.6 0 0 1 9 2.2z|M4.6 8.4a4.4 4.4 0 0 0 8.8 0|M9 12.8V16",
    ear: "M5 9a4 4 0 1 1 7.4 2.1c-.8 1.2-1.6 1.7-1.6 3.1a1.9 1.9 0 0 1-3.5.9",
    up: "M9 14.5V4|M4.8 8.2L9 4l4.2 4.2",
    wave: "M2 9h2.2l1.8-5 3 10 2.2-7 1.2 2H16",
    cut: "M3 12.5l12-7|M3 5.5l12 7",
    copy: "M6.4 6.4V4.6a1.2 1.2 0 0 1 1.2-1.2h6.2a1.2 1.2 0 0 1 1.2 1.2v6.2a1.2 1.2 0 0 1-1.2 1.2h-1.8|M4.6 6.4h6.2A1.2 1.2 0 0 1 12 7.6v6.2a1.2 1.2 0 0 1-1.2 1.2H4.6a1.2 1.2 0 0 1-1.2-1.2V7.6a1.2 1.2 0 0 1 1.2-1.2z",
};

/** 描边图标；颜色取 currentColor 或外层的描边色。 */
export function ic(name, size = 16) {
    const kids = (PATHS[name] || "").split("|").map((p) => {
        const dashed = p[0] === "~";
        return svg("path", { d: dashed ? p.slice(1) : p, "stroke-dasharray": dashed ? "2.3 2.6" : null });
    });
    return svg(
        "svg",
        {
            viewBox: "0 0 18 18",
            width: size,
            height: size,
            fill: "none",
            stroke: "currentColor",
            "stroke-width": "1.6",
            "stroke-linecap": "round",
            "stroke-linejoin": "round",
            "aria-hidden": "true",
            focusable: "false",
        },
        kids,
    );
}

/** 步骤条与摘要卡的五态：图标与语义色。 */
export const STATE_ICON = {
    todo: ["circle", "gray"],
    current: ["circle-dot", "violet"],
    done: ["circle-check", "green"],
    stale: ["stale", "gray"],
    fail: ["circle-x", "red"],
};
