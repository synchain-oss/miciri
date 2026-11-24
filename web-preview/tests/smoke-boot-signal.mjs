// SPDX-License-Identifier: GPL-3.0-or-later
//
// 页面启动守卫（web/shared/boot-signal.js）对资源加载失败的判定：脚本和样式表（rel="stylesheet"）
// 取不到就是启动失败，要报 bootError 让宿主切兜底面板；图标、preload 之类与启动无关的链接失败不报。
// 每个场景在一张新页里单独装一次守卫（它只报第一个错误），用假的 window.__JUCE__ 收集它发给宿主的消息。
//
// 用法：node web-preview/tests/smoke-boot-signal.mjs [仓库根]

import { createRun, repoRoot } from "./lib/harness.mjs";
import { NO_BROWSER_HINT, findBrowser, launchBrowser } from "./lib/browser.mjs";
import { startStaticServer } from "./lib/static-server.mjs";

const ROOT = repoRoot();
const run = createRun("页面启动守卫：哪些资源失败算启动失败");
if (!findBrowser()) run.skip(NO_BROWSER_HINT);

const BOOT_ERROR = "__miciri__bootError";
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const server = await startStaticServer(ROOT);
const browser = await launchBrowser({ width: 800, height: 600 });
try {
    const page = await browser.newPage();

    // 空白页里装上假宿主和守卫，再执行 setup（字符串形式的页面脚本），等 waitMs 后取守卫报出的启动错误。
    const bootErrors = async (setup, waitMs = 1200) => {
        // 同源的一页纯文本当底（不装页面自己的脚本），再用 DOM 方法装上假宿主和守卫
        await page.goto(`${server.url}/LICENSE`);
        await page.eval(`(() => {
            window.__posted = [];
            window.__JUCE__ = { postMessage(m) { window.__posted.push(JSON.parse(m)); } };
            const s = document.createElement("script");
            s.src = ${JSON.stringify(server.url)} + "/web/shared/boot-signal.js";
            document.head.appendChild(s);
        })()`);
        await page.waitFor(`!!window.__miciriBoot`, { label: "守卫脚本装上了" });
        await page.eval(`(() => { const base = ${JSON.stringify(server.url)}; ${setup} })()`);
        await sleep(waitMs);
        return page.eval(
            `window.__posted.filter((m) => m.eventId === ${JSON.stringify(BOOT_ERROR)}).map((m) => m.payload)`,
        );
    };
    // 往 head 里加一个链接，变量名 v；extra 是赋 href 之前要设的属性
    const addLink = (v, rel, file, extra = "") =>
        `const ${v} = document.createElement("link"); ${v}.rel = ${JSON.stringify(rel)}; ${extra}
         ${v}.href = base + ${JSON.stringify(file)}; document.head.appendChild(${v});`;

    // 与启动无关的链接（守卫只报第一个错误，所以各开一页）：真加载一个不存在的 preload；
    // 图标的失败事件直接派发（浏览器自己取图标时不一定在元素上报错）
    const preload = await bootErrors(addLink("pre", "preload", "/missing-preload.css", 'pre.as = "style";'));
    run.eq(preload, [], "preload 链接加载失败不该报启动错误");
    const icon = await bootErrors(
        `${addLink("ico", "icon", "/missing-icon.ico")} ico.dispatchEvent(new Event("error"));`,
    );
    run.eq(icon, [], "图标链接加载失败不该报启动错误");

    const sheet = await bootErrors(addLink("css", "stylesheet", "/missing-sheet.css"));
    run.check(
        sheet.length === 1 && sheet[0].stage === "resource" && String(sheet[0].detail).includes("missing-sheet.css"),
        `样式表加载失败要报一次 resource 启动错误并写出文件名，实得 ${JSON.stringify(sheet)}`,
    );

    const script = await bootErrors(
        `const s = document.createElement("script"); s.src = base + "/missing-script.js"; document.head.appendChild(s);`,
    );
    run.check(
        script.length === 1 && script[0].stage === "resource" && String(script[0].detail).includes("missing-script.js"),
        `脚本加载失败要报一次 resource 启动错误并写出文件名，实得 ${JSON.stringify(script)}`,
    );
} finally {
    await browser.close();
    await server.close();
}
run.finish();
