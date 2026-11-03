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

const HAN = /\p{Script=Han}/u;
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

const DICT_FILES = ["web/shared/strings-common.js", "web/app/shell/shell-strings.js"];
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

// 源码里的词条键必须存在：外壳与共享件只用 common.、shell.、about. 三个前缀
if (merged) {
    const files = listFiles(ROOT, ["web/shared", "web/app/shell"], [".js"]).filter((f) => !DICT_FILES.includes(f));
    const used = new Set();
    for (const f of files) {
        for (const m of readText(ROOT, f).matchAll(/"((?:common|shell|about)\.[A-Za-z0-9.]+)"/g)) used.add(m[1]);
    }
    run.check(used.size > 0, "应当能在源码里找到词条键");
    for (const k of used) run.check(k in merged.zh, `源码用到的词条键 ${k} 不在词条表里`);
}

// 反例：检查要能变红
run.check(checkDict({ a: "你好" }, {}).length > 0, "反例：en 缺键应当被查出");
run.check(checkDict({ a: "共 {n} 项" }, { a: "items" }).length > 0, "反例：占位符不一致应当被查出");
run.check(checkDict({ a: "好" }, { a: "好 ok" }).length > 0, "反例：en 里有汉字应当被查出");
run.check(checkDict({ a: "好 ✓" }, { a: "ok" }).length > 0, "反例：字符图标应当被查出");
run.check(checkDict({ a: "好" }, { a: "ok" }).length === 0, "正例：干净的词条不应报错");

run.finish();
