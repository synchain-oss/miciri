// SPDX-License-Identifier: GPL-3.0-or-later
//
// axe 无障碍检查（需要 playwright 与 axe-core）：对预览页里的外壳跑 WCAG 2 A 与 AA 规则，
// serious、critical 级别的违规判红。本仓没有 npm 运行时依赖；这两个包只在测试工具链里用，版本与完整性哈希
// 钉在 web-preview/tests/axe/package.json 与 package-lock.json（docs-truth 的 pins 检查把关），安装用
// `npm ci --ignore-scripts`，浏览器用 `playwright install chromium`（版本由钉死的 playwright 决定）。
// 依赖目录：环境变量 MICIRI_AXE_DEPS 指向装好 node_modules 的目录；缺省是 web-preview/tests/axe/。
// 找不到任何一个包时整套跳过（退出码 2）并写明缺什么，绝不记成通过；CI 设了 MICIRI_REQUIRE_AXE=1，
// 此时缺包不再是跳过而是失败，免得有人删掉安装步骤后 axe 悄悄变成空转。
//
// 审四个状态：默认页、报告页、报告页上打开着的详情窗（过期的卡）、打开着带证据画布的详情窗。
//
// 用法：node web-preview/tests/smoke-axe-page.mjs [仓库根]

import fs from "node:fs";
import path from "node:path";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";
import { createRun, repoRoot } from "./lib/harness.mjs";
import { startStaticServer } from "./lib/static-server.mjs";

const ROOT = repoRoot();
const run = createRun("axe 无障碍");
const here = path.dirname(fileURLToPath(import.meta.url));
const depsDir = process.env.MICIRI_AXE_DEPS ? path.resolve(process.env.MICIRI_AXE_DEPS) : path.join(here, "axe");
const require = createRequire(path.join(depsDir, "package.json"));

const missing = ["playwright", "axe-core"].filter((name) => {
    try {
        require.resolve(name);
        return false;
    } catch {
        return true;
    }
});
if (missing.length > 0) {
    const why = `没有安装 ${missing.join("、")}；axe 未运行。依赖目录是 MICIRI_AXE_DEPS，缺省 web-preview/tests/axe/；安装用 npm ci --ignore-scripts，再 playwright install chromium`;
    if (process.env.MICIRI_REQUIRE_AXE === "1") {
        run.check(false, `MICIRI_REQUIRE_AXE=1 要求 axe 必须运行，但${why}`);
        run.finish();
        process.exit(process.exitCode);
    }
    run.skip(why);
}

const { chromium } = require("playwright");
const axeSource = fs.readFileSync(require.resolve("axe-core/axe.min.js"), "utf8");
const server = await startStaticServer(ROOT);
const browser = await chromium.launch();
try {
    const page = await browser.newPage({ viewport: { width: 1400, height: 1000 } });
    await page.goto(`${server.url}/web-preview/index.html`);
    const frame = page.frames().find((f) => f.url().endsWith("/web/app/shell/index.html"));
    run.check(!!frame, "应能找到外壳 iframe");
    await frame.waitForFunction(() => document.documentElement.dataset.ready === "1");
    await frame.evaluate(axeSource);
    // 同一套规则审几个状态：默认页、报告页、报告页上打开着的详情窗（背景 inert、对话框名字、焦点）
    const audit = async (label) => {
        const result = await frame.evaluate(() => axe.run(document, { runOnly: ["wcag2a", "wcag2aa"] }));
        const bad = result.violations.filter((v) => v.impact === "serious" || v.impact === "critical");
        for (const v of bad) {
            run.check(false, `axe（${label}）${v.impact} ${v.id}：${v.help}（${v.nodes.length} 处）`);
            // 把命中的节点打出来，免得只看到“1 处”却不知道是哪个元素
            for (const n of v.nodes) run.log(`    ${n.target.join(" ")}  ${n.html.replace(/\s+/g, " ").slice(0, 240)}`);
        }
        run.check(bad.length === 0, `${label}：不应有 serious 或 critical 的 axe 违规`);
        run.log(
            `  axe（${label}）规则 ${result.passes.length} 条通过，${result.violations.length} 条违规（其中 serious 以上 ${bad.length}），${result.incomplete.length} 条需人工复核`,
        );
    };
    await audit("默认页");
    const snap = await page.evaluate(
        async (u) => (await import(u)).SCENES.staleGain(),
        `${server.url}/web-preview/mock/report-fixtures.js`,
    );
    await page.evaluate((x) => window.__MICIRI_MOCK__.mutate((st) => Object.assign(st, x)), snap);
    await frame.evaluate(() => document.querySelector('.step[data-step="report"]').click());
    await frame.waitForSelector('[data-part="report-page"]');
    await audit("报告页");
    await frame.evaluate(() => {
        const b = document.querySelector('[data-part="card-open"][data-card="sib"]');
        b.focus();
        b.click();
    });
    await frame.waitForSelector('[data-part="detail"]', { state: "visible" });
    await audit("详情窗（过期的卡）");
    // 带证据画布的详情窗：先关掉上一个，换成各步有证据的场景，打开喷麦卡，等画布画出来
    await page.keyboard.press("Escape");
    await frame.waitForSelector('[data-part="detail"]', { state: "hidden" });
    const withEvidence = await page.evaluate(
        async (u) => (await import(u)).SCENES.evidence(),
        `${server.url}/web-preview/mock/report-fixtures.js`,
    );
    await page.evaluate((x) => window.__MICIRI_MOCK__.mutate((st) => Object.assign(st, x)), withEvidence);
    await frame.evaluate(() => {
        const b = document.querySelector('[data-part="card-open"][data-card="plosive"]');
        b.focus();
        b.click();
    });
    await frame.waitForSelector('[data-part="evidence-canvas"]', { state: "visible" });
    await audit("详情窗（带证据画布）");
} finally {
    await browser.close();
    await server.close();
}
run.finish();
