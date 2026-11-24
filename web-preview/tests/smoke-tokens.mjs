// SPDX-License-Identifier: GPL-3.0-or-later
//
// 开窗占位渐变：各页面 index.html 的 head 里内联的 linear-gradient，必须与 tokens.css 的 --page-gradient
// 逐字同色标（样式表到位之前铺的底，换成成品渐变时不能跳色）；外圈色 --page-backdrop 是另一个角色，
// 不能与占位渐变同值。
// 原生侧的开局画面（src/plugin/PlatformWebView.h）同样要与页面同形：色标数组与角度对 --page-gradient，
// 外圈色对 --page-backdrop，三个柔光球的位置对 shell-glass.css 的 .sc-halo--N、颜色与尺寸对 --halo-*。
// 再用坏样例证明这些检查会变红。
//
// 用法：node web-preview/tests/smoke-tokens.mjs [仓库根]

import { createRun, repoRoot } from "./lib/harness.mjs";
import { listFiles, readText } from "./lib/files.mjs";

const ROOT = repoRoot();
const run = createRun("开窗占位渐变");

/** 归一化空白和大小写，色标逐字比较。 */
const norm = (s) =>
    String(s)
        .replace(/\s+/g, " ")
        .replace(/\s*,\s*/g, ", ")
        .replace(/\(\s+/g, "(")
        .replace(/\s+\)/g, ")")
        .trim()
        .toLowerCase();

/** tokens.css 里某个自定义属性的值；没有返回 null。 */
export function tokenValue(css, prop) {
    const m = new RegExp(`${prop}\\s*:\\s*([^;]+);`).exec(css);
    return m ? norm(m[1]) : null;
}

/** index.html 的 head 内联样式里 html/body 的渐变背景；没有返回 null。 */
export function placeholderGradient(html) {
    const style = /<style>([\s\S]*?)<\/style>/.exec(html);
    const m = style ? /background\s*:\s*(linear-gradient\([^;]+\))\s*;/.exec(style[1]) : null;
    return m ? norm(m[1]) : null;
}

const css = readText(ROOT, "web/shared/tokens.css");
const gradient = tokenValue(css, "--page-gradient");
const backdrop = tokenValue(css, "--page-backdrop");
run.check(!!gradient && gradient.startsWith("linear-gradient("), "tokens.css 应有 --page-gradient 渐变");
run.check(!!backdrop, "tokens.css 应有 --page-backdrop");
run.check(gradient !== backdrop, "外圈色与占位渐变是两个角色，取值不能相同");

const pages = listFiles(ROOT, ["web/app"], [".html"]);
run.check(pages.length > 0, "应能找到页面入口 index.html");
for (const f of pages) {
    run.eq(placeholderGradient(readText(ROOT, f)), gradient, `${f}：开窗占位渐变应与 --page-gradient 逐字同色标`);
}

// ---- 原生侧开局画面 ----
/** "#rrggbb" 或 "0xrrggbb" 归一成小写六位十六进制。 */
const hex6 = (v) =>
    String(v)
        .replace(/^#|^0x/i, "")
        .toLowerCase();

/** PlatformWebView.h 里的色标数组与角度，拼成与 tokens.css 同写法的渐变字面量；读不到返回 null。 */
export function nativeGradient(header) {
    const angle = /kShellBackdropAngleDeg\s*=\s*([\d.]+)f/.exec(header);
    const block = /kShellBackdropStops\[\]\s*=\s*\{([\s\S]*?)\};/.exec(header);
    if (!angle || !block) return null;
    const stops = [...block[1].matchAll(/\{\s*0x([0-9a-fA-F]{6})\s*,\s*([\d.]+)f\s*\}/g)];
    if (stops.length < 2) return null;
    const parts = stops.map((m) => `#${hex6(m[1])} ${Math.round(Number(m[2]) * 100)}%`);
    return norm(`linear-gradient(${Number(angle[1])}deg, ${parts.join(", ")})`);
}

/** PlatformWebView.h 里某个 0x 色值常量；读不到返回 null。 */
export function nativeRgb(header, name) {
    const m = new RegExp(`${name}\\s*=\\s*0x([0-9a-fA-F]{6})`).exec(header);
    return m ? hex6(m[1]) : null;
}

/** PlatformWebView.h 里某个浮点常量；读不到返回 null。 */
export function nativeFloat(header, name) {
    const m = new RegExp(`${name}\\s*=\\s*(-?[\\d.]+)f`).exec(header);
    return m ? Number(m[1]) : null;
}

/** 柔光球数组：[{x, y, right, bottom, rgb, alpha}]，按声明顺序。 */
export function nativeHalos(header) {
    const block = /kShellHalos\[\]\s*=\s*\{([\s\S]*?)\};/.exec(header);
    if (!block) return [];
    const re =
        /\{\s*(-?[\d.]+)f\s*,\s*(-?[\d.]+)f\s*,\s*(true|false)\s*,\s*(true|false)\s*,\s*0x([0-9a-fA-F]{6})\s*,\s*([\d.]+)f\s*\}/g;
    return [...block[1].matchAll(re)].map((m) => ({
        x: Number(m[1]),
        y: Number(m[2]),
        right: m[3] === "true",
        bottom: m[4] === "true",
        rgb: hex6(m[5]),
        alpha: Number(m[6]),
    }));
}

/** shell-glass.css 里 .sc-halo--N 的位置：{x, y, right, bottom}（px）；读不到返回 null。 */
export function cssHaloInset(glassCss, n) {
    const block = new RegExp(`\\.sc-halo--${n}\\s*\\{([^}]*)\\}`).exec(glassCss);
    if (!block) return null;
    const px = (prop) => {
        const m = new RegExp(`(?:^|[;\\s])${prop}\\s*:\\s*(-?[\\d.]+)px`).exec(block[1]);
        return m ? Number(m[1]) : null;
    };
    const left = px("left");
    const right = px("right");
    const top = px("top");
    const bottom = px("bottom");
    if ((left === null) === (right === null) || (top === null) === (bottom === null)) return null;
    return { x: left ?? right, y: top ?? bottom, right: right !== null, bottom: bottom !== null };
}

/** "rgba(r, g, b, a)" → {rgb, alpha}；不是这个写法返回 null。 */
export function rgbaParts(value) {
    const m = /rgba\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*([\d.]+)\s*\)/.exec(String(value));
    if (!m) return null;
    const h = (n) => Number(n).toString(16).padStart(2, "0");
    return { rgb: `${h(m[1])}${h(m[2])}${h(m[3])}`, alpha: Number(m[4]) };
}

const header = readText(ROOT, "src/plugin/PlatformWebView.h");
const glass = readText(ROOT, "web/app/shell/shell-glass.css");
run.eq(nativeGradient(header), gradient, "原生开局画面的色标与角度应与 --page-gradient 相同");
run.eq(`#${nativeRgb(header, "kShellRimRgb")}`, backdrop, "原生开局画面的外圈色应与 --page-backdrop 相同");
run.eq(
    `${nativeFloat(header, "kShellHaloDiameter")}px`,
    tokenValue(css, "--halo-size"),
    "柔光球直径应与 --halo-size 相同",
);
run.eq(`${nativeFloat(header, "kShellHaloBlur")}px`, tokenValue(css, "--halo-blur"), "柔光球模糊应与 --halo-blur 相同");
const halos = nativeHalos(header);
run.eq(halos.length, 3, "原生开局画面应有三个柔光球");
halos.forEach((h, i) => {
    const n = i + 1;
    run.eq(
        { x: h.x, y: h.y, right: h.right, bottom: h.bottom },
        cssHaloInset(glass, n),
        `柔光球 ${n} 的位置应与 .sc-halo--${n} 相同`,
    );
    run.eq(
        { rgb: h.rgb, alpha: h.alpha },
        rgbaParts(tokenValue(css, `--halo-${n}`)),
        `柔光球 ${n} 的颜色应与 --halo-${n} 相同`,
    );
});

// 反例
const twoStop = "<style>html{background: linear-gradient(157deg, #b5acc9 0%, #fde8ed 100%);}</style>";
run.check(placeholderGradient(twoStop) !== gradient, "反例：两色标的占位应当与四色标的渐变对不上");
run.check(placeholderGradient("<style>html{margin:0}</style>") === null, "反例：没有渐变背景应当读成 null");
run.check(
    tokenValue("--a: linear-gradient(1deg,#fff 0%,#000 100%);", "--a") ===
        tokenValue("--a:  linear-gradient( 1deg , #FFF 0%, #000 100% ) ;", "--a"),
    "正例：空白与大小写差异不算不同",
);

const grayHeader =
    "kShellBackdropAngleDeg = 157.0f;\nkShellBackdropStops[] = {\n    {0xdfe3ea, 0.00f},\n    {0xf1f3f7, 1.00f},\n};";
run.check(nativeGradient(grayHeader) !== gradient, "反例：原生侧还是两色标灰底时应当对不上");
run.check(nativeGradient("kShellBackdropAngleDeg = 157.0f;") === null, "反例：没有色标数组应当读成 null");
run.check(
    cssHaloInset(".sc-halo--1 { left: -90px; right: 4px; top: 1px; }", 1) === null,
    "反例：左右都写了的柔光球位置读不出唯一答案，应当读成 null",
);
const movedHalo = nativeHalos("kShellHalos[] = {\n    {-90.0f, -80.0f, false, false, 0xb5acc9, 0.62f},\n};")[0];
run.check(
    JSON.stringify({ x: movedHalo.x + 1, y: movedHalo.y, right: false, bottom: false }) !==
        JSON.stringify(cssHaloInset(glass, 1)),
    "反例：柔光球挪动 1 px 应当对不上",
);

run.finish();
