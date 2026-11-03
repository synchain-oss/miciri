// SPDX-License-Identifier: GPL-3.0-or-later
//
// 注入点与离线检查：web/ 与 web-preview/ 下不得出现把字符串当 HTML 或代码执行的写法、原生下拉、在线资源。
// 白名单只有 JUCE 自带的辅助脚本（web/js/juce/，逐字收录的第三方文件）。
// 规则里的词用 j() 拼出，免得本文件自己被自己扫出来；再用坏样例证明规则会变红。
//
// 用法：node web-preview/tests/smoke-no-html-sinks.mjs [仓库根]

import { createRun, repoRoot } from "./lib/harness.mjs";
import { listFiles, readText } from "./lib/files.mjs";

const j = (...p) => p.join("");
const RULES = [
    ["字符串转 HTML", new RegExp(`\\.(?:${j("inner", "HTML")}|${j("outer", "HTML")}|${j("setHTML", "Unsafe")})\\b`)],
    ["字符串转 HTML", new RegExp(`\\b${j("insertAdjacent", "HTML")}\\b`)],
    ["字符串转 HTML", new RegExp(`\\b${j("createContextual", "Fragment")}\\b`)],
    ["字符串转 HTML", new RegExp(`\\b${j("parseHTML", "Unsafe")}\\b`)],
    ["字符串转 HTML", new RegExp(`\\.${j("src", "doc")}\\b|\\b${j("src", "doc")}=`)],
    ["写文档流", new RegExp(`\\b${j("document", "\\.write")}(?:ln)?\\s*\\(`)],
    [
        "动态执行代码",
        new RegExp(
            String.raw`(?<![.\w])(?<!async\s)${j("ev", "al")}\s*\(|\b(?:window|globalThis|self)\.${j("ev", "al")}\s*\(`,
        ),
    ],
    ["动态执行代码", new RegExp(`\\bnew\\s+${j("Func", "tion")}\\s*\\(`)],
    ["字符串形式的定时器", /\bset(?:Timeout|Interval)\s*\(\s*["'`]/],
    ["原生下拉", new RegExp(`<${j("sel", "ect")}\\b|createElement\\(\\s*["']${j("sel", "ect")}["']`)],
    ["在线资源", /\b(?:src|href)\s*=\s*["']https?:/],
    ["在线资源", /@import\s+(?:url\()?["']?https?:/],
    ["在线字体服务", new RegExp(`${j("fonts\\.", "googleapis")}|${j("fonts\\.", "gstatic")}`)],
];

/** 返回命中列表 [{rule, line}]；空表表示干净。 */
export function scanText(text) {
    const hits = [];
    text.split(/\r?\n/).forEach((line, i) => {
        for (const [rule, re] of RULES) if (re.test(line)) hits.push({ rule, line: i + 1 });
    });
    return hits;
}

const ROOT = repoRoot();
const run = createRun("注入点与离线");

const files = listFiles(ROOT, ["web", "web-preview"], [".js", ".mjs", ".html", ".css"], ["web/js/juce"]);
run.check(files.length > 0, "应当能扫到 web 与 web-preview 下的文件");
for (const f of files) {
    const hits = scanText(readText(ROOT, f));
    run.eq(hits, [], `${f}：不应有注入点、原生下拉或在线资源`);
}

// 反例：规则要能变红
const bad = [
    `el.${j("inner", "HTML")} = x;`,
    `el.${j("outer", "HTML")} = x;`,
    `el.${j("insertAdjacent", "HTML")}("beforeend", x);`,
    `${j("document", ".write")}(x);`,
    `${j("ev", "al")}(x);`,
    `window.${j("ev", "al")}(x);`,
    `new ${j("Func", "tion")}("return 1");`,
    `${j("set", "Timeout")}("go()", 10);`,
    `<${j("sel", "ect")} id="a"></${j("sel", "ect")}>`,
    `<script ${j("sr", "c")}="${j("htt", "ps")}://example.org/a.js"></script>`,
    `@import url("${j("htt", "ps")}://example.org/a.css");`,
];
for (const b of bad) run.check(scanText(b).length > 0, `反例应当被查出：${b}`);
run.check(
    scanText(`page.${j("ev", "al")}(x); async ${j("ev", "al")}(expr) {}`).length === 0,
    "正例：页面库自己的 eval 方法不算动态执行代码",
);
run.check(
    scanText(`el.textContent = x; const xmlns = "http://www.w3.org/2000/svg";`).length === 0,
    "正例：textContent 与 svg 命名空间不应报错",
);

run.finish();
