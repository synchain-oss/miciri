// SPDX-License-Identifier: GPL-3.0-or-later
//
// 开窗占位渐变：各页面 index.html 的 head 里内联的 linear-gradient，必须与 tokens.css 的 --page-gradient
// 逐字同色标（样式表到位之前铺的底，换成成品渐变时不能跳色）；外圈色 --page-backdrop 是另一个角色，
// 不能与占位渐变同值。再用坏样例证明这条检查会变红。
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

// 反例
const twoStop = "<style>html{background: linear-gradient(157deg, #b5acc9 0%, #fde8ed 100%);}</style>";
run.check(placeholderGradient(twoStop) !== gradient, "反例：两色标的占位应当与四色标的渐变对不上");
run.check(placeholderGradient("<style>html{margin:0}</style>") === null, "反例：没有渐变背景应当读成 null");
run.check(
    tokenValue("--a: linear-gradient(1deg,#fff 0%,#000 100%);", "--a") ===
        tokenValue("--a:  linear-gradient( 1deg , #FFF 0%, #000 100% ) ;", "--a"),
    "正例：空白与大小写差异不算不同",
);

run.finish();
