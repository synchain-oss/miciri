// SPDX-License-Identifier: GPL-3.0-or-later
//
// axe 无障碍检查（需要 playwright 与 axe-core）：对预览页里的外壳跑 WCAG 2 A 与 AA 规则，
// serious、critical 级别的违规判红。本仓没有 npm 运行时依赖，本机与 CI 默认都没有这两个包：
// 找不到任何一个就整套跳过（退出码 2）并写明缺什么，绝不记成通过。
// 启用方式：在 CI 里安装并钉版 playwright 与 axe-core（需要改 workflow，由维护者决定）。
// 说明：这条路径在没有 playwright 的机器上无法运行，所以本文件里「已安装」那一支没有在本机验证过。
//
// 用法：node web-preview/tests/smoke-axe-page.mjs [仓库根]

import { createRequire } from "node:module";
import { createRun, repoRoot } from "./lib/harness.mjs";
import { startStaticServer } from "./lib/static-server.mjs";

const ROOT = repoRoot();
const run = createRun("axe 无障碍");
const require = createRequire(`${ROOT}/package.json`);

const missing = ["playwright", "axe-core"].filter((name) => {
    try {
        require.resolve(name);
        return false;
    } catch {
        return true;
    }
});
if (missing.length > 0) {
    run.skip(`没有安装 ${missing.join("、")}；axe 未运行。本仓不带 npm 依赖，CI 的 web-smoke 也不装它们`);
}

const { chromium } = require("playwright");
const axeSource = (await import("node:fs")).readFileSync(require.resolve("axe-core/axe.min.js"), "utf8");
const server = await startStaticServer(ROOT);
const browser = await chromium.launch();
try {
    const page = await browser.newPage({ viewport: { width: 1400, height: 1000 } });
    await page.goto(`${server.url}/web-preview/index.html`);
    const frame = page.frames().find((f) => f.url().endsWith("/web/app/shell/index.html"));
    run.check(!!frame, "应能找到外壳 iframe");
    await frame.waitForFunction(() => document.documentElement.dataset.ready === "1");
    await frame.evaluate(axeSource);
    const result = await frame.evaluate(() => axe.run(document, { runOnly: ["wcag2a", "wcag2aa"] }));
    const bad = result.violations.filter((v) => v.impact === "serious" || v.impact === "critical");
    for (const v of bad) run.check(false, `axe ${v.impact} ${v.id}：${v.help}（${v.nodes.length} 处）`);
    run.check(bad.length === 0, "不应有 serious 或 critical 的 axe 违规");
    run.log(
        `  axe 规则 ${result.passes.length} 条通过，${result.violations.length} 条违规（其中 serious 以上 ${bad.length}）`,
    );
} finally {
    await browser.close();
    await server.close();
}
run.finish();
