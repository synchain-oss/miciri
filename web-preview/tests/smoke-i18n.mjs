// SPDX-License-Identifier: GPL-3.0-or-later
//
// 词条检查：zh 与 en 的键完全一致、占位符一致、en 里没有汉字、没有 emoji 或字符图标；
// 合并时键不重名；源码里用到的词条键都存在。再用坏样例证明这些检查会变红。
//
// 用法：node web-preview/tests/smoke-i18n.mjs [仓库根]

import path from "node:path";
import { pathToFileURL } from "node:url";
import { createRun, repoRoot } from "./lib/harness.mjs";
import { listFiles, readText } from "./lib/files.mjs";

const ROOT = repoRoot();
const run = createRun("词条（zh 与 en）");
const load = async (rel) => (await import(pathToFileURL(path.join(ROOT, rel)).href)).default;

// 汉字之外还要拦全角标点（：，（ 等）：en 词条里混进一个全角冒号，肉眼在等宽字体里很难发现
const FULLWIDTH_PUNCT = /[\u3000-\u303f\uff00-\uffef]/;
const HAN = /[\p{Script=Han}\u3000-\u303f\uff00-\uffef]/u;
const PICTO = /[\p{Extended_Pictographic}✓✔⚠]/u;
const placeholders = (s) => [...String(s).matchAll(/\{(\w+)\}/g)].map((m) => m[1]).sort();

/** 返回错误列表；空表表示通过。 */
export function checkDict(zh, en) {
    const errors = [];
    const zk = Object.keys(zh);
    const ek = Object.keys(en);
    for (const k of zk) if (!(k in en)) errors.push(`en 缺键 ${k}`);
    for (const k of ek) if (!(k in zh)) errors.push(`zh 缺键 ${k}`);
    for (const k of zk.filter((x) => x in en)) {
        if (typeof zh[k] !== "string" || typeof en[k] !== "string" || !zh[k] || !en[k])
            errors.push(`${k} 必须是非空字符串`);
        if (placeholders(zh[k]).join() !== placeholders(en[k]).join()) errors.push(`${k} 两种语言的占位符不一致`);
        if (HAN.test(en[k])) errors.push(`${k} 的 en 里有汉字`);
        if (PICTO.test(zh[k]) || PICTO.test(en[k])) errors.push(`${k} 里有 emoji 或字符图标，图标一律用描边 SVG`);
    }
    return errors;
}

/** 去掉行尾的 // 注释；引号里的 // 不算注释（按引号配对逐字符扫，认反斜杠转义）。 */
export function stripLineComment(line) {
    let quote = null;
    for (let i = 0; i < line.length; i++) {
        const c = line[i];
        if (quote) {
            if (c === "\\") i++;
            else if (c === quote) quote = null;
        } else if (c === '"' || c === "'" || c === "`") {
            quote = c;
        } else if (c === "/" && line[i + 1] === "/") {
            return line.slice(0, i);
        }
    }
    return line;
}

/**
 * 页面层源码（非词条文件）里，字符串字面量不得夹全角标点：面向用户的标点要随语言走，必须在词条里。
 * 先去掉整行注释与行尾注释再扫；只用在外壳页面层，开发者向的报错文案不在此列。
 * @returns {string[]} 命中的行
 */
export function findFullwidthLiterals(src) {
    const hits = [];
    for (const raw of src.split("\n")) {
        const line = stripLineComment(raw);
        if (/^\s*(\/\/|\*|\/\*)/.test(line)) continue;
        for (const m of line.matchAll(/"[^"\n]*"|`[^`\n]*`|'[^'\n]*'/g)) {
            if (FULLWIDTH_PUNCT.test(m[0])) hits.push(raw.trim());
        }
    }
    return hits;
}

const DICT_FILES = [
    "web/shared/strings-common.js",
    "web/app/shell/shell-strings.js",
    "web/app/flow/flow-strings.js",
    "web/app/meter/meter-strings.js",
];
const parts = [];
for (const f of DICT_FILES) {
    const d = await load(f);
    parts.push(d);
    run.eq(checkDict(d.zh, d.en), [], `${f}：两种语言的词条应一致`);
}

// 合并：键重名要抛错
const { mergeDicts } = await import(pathToFileURL(path.join(ROOT, "web/shared/i18n.js")).href);
let merged = null;
try {
    merged = mergeDicts(...parts);
} catch (e) {
    run.check(false, `共享与外壳的词条合并失败：${e.message}`);
}
run.check(merged !== null, "词条合并成功");
let threw = false;
try {
    mergeDicts(parts[0], parts[0]);
} catch {
    threw = true;
}
run.check(threw, "同一个键出现在两份里，mergeDicts 应当抛错");

// 源码里的词条键必须存在：外壳与共享件用 common.、shell.、about.，流程页用 flow.，左栏输入卡用 meter.
if (merged) {
    const files = listFiles(ROOT, ["web/shared", "web/app"], [".js"]).filter((f) => !DICT_FILES.includes(f));
    const used = new Set();
    for (const f of files) {
        for (const m of readText(ROOT, f).matchAll(/"((?:common|shell|about|flow|meter)\.[A-Za-z0-9.-]+)"/g))
            used.add(m[1]);
    }
    run.check(used.size > 0, "应当能在源码里找到词条键");
    for (const k of used) run.check(k in merged.zh, `源码用到的词条键 ${k} 不在词条表里`);
}

// 页面层源码里不夹全角标点（联系行的全角冒号就是这样漏进去的）
for (const f of [
    "web/app/shell/shell-app.js",
    "web/app/shell/shell-main.js",
    "web/app/flow/flow-ui.js",
    "web/app/flow/flow-main.js",
    "web/app/flow/remote-ui.js",
    "web/app/flow/flow-core.js",
    "web/app/meter/meter-ui.js",
    "web/app/meter/meter-core.js",
]) {
    run.eq(findFullwidthLiterals(readText(ROOT, f)), [], `${f}：字符串字面量里不应有全角标点，标点放进词条`);
}

// 反例：检查要能变红
run.check(checkDict({ a: "你好" }, {}).length > 0, "反例：en 缺键应当被查出");
run.check(checkDict({ a: "共 {n} 项" }, { a: "items" }).length > 0, "反例：占位符不一致应当被查出");
run.check(checkDict({ a: "好" }, { a: "好 ok" }).length > 0, "反例：en 里有汉字应当被查出");
run.check(checkDict({ a: "好" }, { a: "Contact：x" }).length > 0, "反例：en 里有全角冒号应当被查出");
run.check(findFullwidthLiterals('el("span", { text: `：${C}` });').length > 0, "反例：字面量里的全角冒号应当被查出");
run.check(findFullwidthLiterals('const a = "a // b：c";').length > 0, "反例：字符串里带 // 的字面量不能被当成注释截掉");
run.check(
    findFullwidthLiterals('// 注释里的，没关系\nconst a = "ok"; // 行尾，也没关系').length === 0,
    "正例：注释里的全角标点不算",
);
run.check(checkDict({ a: "好 ✓" }, { a: "ok" }).length > 0, "反例：字符图标应当被查出");
run.check(checkDict({ a: "好" }, { a: "ok" }).length === 0, "正例：干净的词条不应报错");

run.finish();
