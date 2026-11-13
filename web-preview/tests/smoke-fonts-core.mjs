// SPDX-License-Identifier: GPL-3.0-or-later
//
// 字体加载判读（不需要浏览器）：fontProblems 对正常样例给空表，对缺字面、加载失败、状态未定的样例都要报问题；
// 再核对 tokens.css 里的每个本地字体地址都指向 web/fonts 里真实存在的文件。页面里的实际加载由
// smoke-shell-page 与 smoke-flow-page 经 document.fonts 核对。
//
// 用法：node web-preview/tests/smoke-fonts-core.mjs [仓库根]

import fs from "node:fs";
import path from "node:path";
import { createRun, repoRoot } from "./lib/harness.mjs";
import { readText } from "./lib/files.mjs";
import { FONT_SAMPLES, fontProblems } from "./lib/fonts.mjs";

const ROOT = repoRoot();
const run = createRun("字体加载判读");

const families = FONT_SAMPLES.map((s) => s.family);
const good = families.map((family) => ({ family, status: "loaded" }));
run.eq(fontProblems(good), [], "正例：四个字面都是 loaded 时没有问题");

run.check(
    fontProblems(good.map((f) => (f.family === "Miciri Mono" ? { ...f, status: "error" } : f))).some((p) =>
        p.startsWith("Miciri Mono："),
    ),
    "反例：一个字面加载失败（error）应当报它",
);
run.check(
    fontProblems(good.map((f) => (f.family === "Noto Sans SC" ? { ...f, status: "unloaded" } : f))).length === 1,
    "反例：一个字面没有加载（unloaded）应当报且只报它",
);
run.check(
    fontProblems(good.filter((f) => f.family !== "Space Grotesk")).some((p) => p.startsWith("Space Grotesk：")),
    "反例：少一个族（@font-face 缺失或族名不对）应当报它",
);
run.check(fontProblems([]).length === families.length, "反例：一个字面也没有时每个族都报");

// tokens.css 的本地字体地址必须指向真实存在的文件（相对 tokens.css 所在目录解析），并且恰好覆盖四个族
const css = readText(ROOT, "web/shared/tokens.css");
const faces = [...css.matchAll(/@font-face\s*\{([^}]*)\}/g)].map((m) => ({
    family: /font-family\s*:\s*"([^"]+)"/.exec(m[1])?.[1] ?? null,
    url: /url\(\s*"([^"]+)"\s*\)/.exec(m[1])?.[1] ?? null,
}));
run.eq(faces.map((f) => f.family).sort(), [...families].sort(), "tokens.css 的 @font-face 应恰好声明这四个族");
for (const f of faces) {
    const file = f.url ? path.resolve(ROOT, "web/shared", f.url) : null;
    run.check(!!file && fs.existsSync(file), `${f.family}：字体地址 ${f.url} 应指向存在的文件`);
}

run.finish();
