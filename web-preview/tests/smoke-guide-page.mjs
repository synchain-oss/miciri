// SPDX-License-Identifier: GPL-3.0-or-later
//
// 应用内指南页面冒烟（无头浏览器）：预览页用假桥装载真源外壳，端到端核对
//   ① 打开：点页脚“?”出现对话框（有名字、aria-modal）、第一步是输入卡、步数写成“第 n 步 / 共 m 步”，
//      背景四个区域 inert、说明框落在设计盒内且不压住被圈的目标；
//   ② 翻页：按钮、方向键、点任意处都能下一步，上一步回得去，Tab 只在说明框的按钮里循环；
//   ③ 关闭：Esc、跳过、最后一页的“完成”都关，inert 解除、焦点回到“?”；已看过时不再记“指南已看”；
//   ④ 语言：切到 EN 后再开，标题与按钮是英文；
//   ⑤ 让位：别的模态开着时不开指南；
//   ③c 重画（窗口大小变了）不抢走说明框里按钮的焦点；羽化宽度乘后备倍率；手动开过之后快照变未看也不自动开；
//   ⑥ 首次启动：指南未看时开窗后自动打开一次，关掉只记一次“指南已看”，之后快照怎么变都不再弹；已看过则不弹；
//   ⑦ 全程没有页面异常、没有外网请求。
// 没有浏览器时整套跳过（退出码 2），不是通过。
//
// 用法：node web-preview/tests/smoke-guide-page.mjs [仓库根]

import path from "node:path";
import { pathToFileURL } from "node:url";
import { createRun, repoRoot } from "./lib/harness.mjs";
import { findBrowser, launchBrowser } from "./lib/browser.mjs";
import { startStaticServer } from "./lib/static-server.mjs";

const ROOT = repoRoot();
const run = createRun("应用内指南（页面）");
if (!findBrowser()) run.skip("没有 Chromium 系浏览器；可用环境变量 MICIRI_BROWSER 指定");

const at = (rel) => import(pathToFileURL(path.join(ROOT, rel)).href);
const { DESIGN } = await at("web/shared/design-box.js");
const { STEPS, GEOM } = await at("web/app/guide/guide-core.js");
const guide = (await at("web/app/guide/guide-strings.js")).default;
const fmt = (s, v) => s.replace(/\{(\w+)\}/g, (_, k) => v[k]);

const FD = "document.getElementById('frame').contentDocument";
const MOCK = "window.__MICIRI_MOCK__";
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const server = await startStaticServer(ROOT);
const browser = await launchBrowser({ width: 1400, height: 1000 });
try {
    const page = await browser.newPage();
    await page.send("Emulation.setDeviceMetricsOverride", {
        width: 1400,
        height: 1000,
        deviceScaleFactor: 1,
        mobile: false,
        screenWidth: 1400,
        screenHeight: 1000,
    });
    const requests = await page.recordRequests();
    const q = (sel) => `${FD}.querySelector(${JSON.stringify(sel)})`;
    const click = (sel) => page.eval(`${q(sel)}.click()`);
    const calls = () => page.eval(`${MOCK}.calls.map((c) => c.name + ":" + JSON.stringify(c.args))`);
    const key = (k, extra = "") =>
        page.eval(
            `(${FD}.activeElement || ${FD}.body).dispatchEvent(new KeyboardEvent("keydown", { key: ${JSON.stringify(k)}, ${extra} bubbles: true, cancelable: true }))`,
        );
    const seenCalls = async () => (await calls()).filter((c) => c.startsWith("setGuideSeen"));
    const isOpen = () => page.eval(`!!${q('[data-part="guide"]')}`);
    const stepId = () => page.eval(`${q('[data-part="guide-callout"]')}?.dataset.step ?? null`);
    const title = () => page.eval(`${q(".guide-call__title")}?.textContent ?? null`);
    const where = () => page.eval(`${q(".guide-call__step")}?.textContent ?? null`);
    const active = () => page.eval(`${FD}.activeElement?.dataset.part || ${FD}.activeElement?.className || ""`);
    const anyInert = () => page.eval(`${FD}.querySelectorAll("[inert]").length`);
    const openGuide = async () => {
        await click('[data-part="help"]');
        await page.waitFor(`!!${q('[data-part="guide-callout"]')}`, { label: "点“?”后应出现指南说明框" });
    };

    await page.goto(`${server.url}/web-preview/index.html`);
    await page.waitFor(`!!${FD}?.documentElement.dataset.bootError || ${FD}?.documentElement.dataset.ready === "1"`, {
        label: "首个快照应用后 data-ready 应为 1",
    });
    run.eq(await page.eval(`${FD}.documentElement.dataset.bootError ?? null`), null, "不应有启动错误");
    await sleep(500); // 留足自动打开的时间窗口：指南已看过时，一直不应弹出
    run.eq(await isOpen(), false, "指南已看过时，没点“?”之前不开");

    // ---- ① 打开 ----
    await openGuide();
    const total = await page.eval(`${FD}.querySelectorAll(".guide-call__dot").length`);
    run.check(total >= 1 && total <= STEPS.length, `步数在 1 到 ${STEPS.length} 之间（实际 ${total}）`);
    run.eq(await stepId(), "input", "第一步是输入卡");
    run.eq(await title(), guide.zh["guide.input.t"], "标题来自词条");
    run.eq(await where(), fmt(guide.zh["guide.step"], { n: 1, total }), "步数文字用占位符拼出");
    run.eq(
        await page.eval(
            `(() => { const o = ${q('[data-part="guide"]')}; const t = ${FD}.getElementById(o.getAttribute("aria-labelledby")); return o.getAttribute("role") + "|" + o.getAttribute("aria-modal") + "|" + (!!t && t.textContent.length > 0); })()`,
        ),
        "dialog|true|true",
        "对话框有角色、aria-modal 与标题名",
    );
    run.eq(await anyInert(), 4, "背景四个区域都是 inert");
    // 进度点：只有当前一步带 data-current（el() 不写 false，所以非当前的点没有这个属性）
    const dots = () =>
        page.eval(`Array.from(${FD}.querySelectorAll(".guide-call__dot")).map((d) => d.hasAttribute("data-current"))`);
    run.eq(
        (await dots()).map((on, i) => (on ? i : -1)).filter((i) => i >= 0),
        [0],
        "进度点：第一步只亮第一个",
    );
    const geometry = (stepSel) =>
        page.eval(`(() => {
            const shell = ${FD}.getElementById("shell").getBoundingClientRect();
            const c = ${q('[data-part="guide-callout"]')}.getBoundingClientRect();
            const t = ${q(stepSel)}?.getBoundingClientRect();
            const z = Number(${FD}.getElementById("shell").style.zoom) || 1;
            const inside = c.left >= shell.left - 1 && c.top >= shell.top - 1 && c.right <= shell.right + 1 && c.bottom <= shell.bottom + 1;
            const overlap = t ? !(c.right <= t.left || c.left >= t.right || c.bottom <= t.top || c.top >= t.bottom) : false;
            return { inside, overlap, z, w: shell.width / z, h: shell.height / z };
        })()`);
    for (let i = 0; i < total; i++) {
        const id = await stepId();
        const def = STEPS.find((s) => s.id === id);
        const sel = def.select[0];
        const g = await geometry(sel);
        run.check(g.inside, `第 ${i + 1} 步（${id}）：说明框在设计盒内`);
        if (def.select.length === 1) run.check(!g.overlap, `第 ${i + 1} 步（${id}）：说明框不压住被圈的目标`);
        if (i < total - 1) await click('[data-part="guide-next"]');
    }
    run.eq(await where(), fmt(guide.zh["guide.step"], { n: total, total }), "最后一步的步数写对");
    run.eq(
        await page.eval(`${q('[data-part="guide-next"]')}.textContent`),
        guide.zh["guide.done"],
        "最后一步的主按钮是“完成”",
    );

    // ---- ② 翻页 ----
    await click('[data-part="guide-prev"]');
    run.check((await where()) === fmt(guide.zh["guide.step"], { n: total - 1, total }), "上一步回得去");
    run.eq(
        (await dots()).map((on, i) => (on ? i : -1)).filter((i) => i >= 0),
        [total - 2],
        "进度点：只亮当前一步",
    );
    await key("ArrowLeft");
    run.check((await where()) === fmt(guide.zh["guide.step"], { n: Math.max(1, total - 2), total }), "左方向键上一步");
    await key("ArrowRight");
    run.check((await where()) === fmt(guide.zh["guide.step"], { n: total - 1, total }), "右方向键下一步");
    await page.eval(`${q('[data-part="guide"]')}.click()`);
    run.eq(await where(), fmt(guide.zh["guide.step"], { n: total, total }), "点暗幕任意处也是下一步");
    // Tab 只在说明框的按钮里循环：连按 6 次焦点始终在说明框里
    let trapped = true;
    for (let i = 0; i < 6; i++) {
        await key("Tab");
        if (!(await page.eval(`!!${FD}.activeElement?.closest('[data-part="guide-callout"]')`))) trapped = false;
    }
    run.check(trapped, "Tab 连按 6 次焦点没有跑出说明框");
    await key("Tab", "shiftKey: true,");
    run.check(
        await page.eval(`!!${FD}.activeElement?.closest('[data-part="guide-callout"]')`),
        "Shift+Tab 也在说明框里",
    );

    // 重画不抢焦点：焦点在“下一步”上，窗口大小变了，重画后焦点仍在“下一步”上
    await page.eval(`${q('[data-part="guide-next"]')}.focus()`);
    await page.eval(`${FD}.defaultView.dispatchEvent(new Event("resize"))`);
    await sleep(300);
    run.eq(await active(), "guide-next", "窗口大小变了重画，焦点仍在原来的按钮上");

    // ---- ③ 关闭：完成、Esc、跳过 ----
    await click('[data-part="guide-next"]'); // 最后一页的“完成”
    run.eq(await isOpen(), false, "最后一页点“完成”关闭");
    run.eq(await anyInert(), 0, "关闭后 inert 全部解除");
    run.eq(await active(), "help", "关闭后焦点回到“?”");
    run.eq(await seenCalls(), [], "已看过时，看完也不再记“指南已看”");
    await openGuide();
    await key("Escape");
    run.eq(await isOpen(), false, "Esc 关闭");
    run.eq(await anyInert(), 0, "Esc 关闭后 inert 解除");
    await openGuide();
    if (total > 1) await click('[data-part="guide-skip"]');
    run.eq(await isOpen(), false, "“跳过”关闭");
    run.eq(await seenCalls(), [], "已看过时，Esc 与跳过也都不记");

    // 手动开过之后，快照变成未看也不自动开（一个页面只自动开一回，手动开也算）
    await page.eval(`${MOCK}.mutate((st) => { st.guideSeen = false; })`);
    await sleep(500);
    run.eq(await isOpen(), false, "手动开过指南之后，快照变成未看也不自动打开");
    await page.eval(`${MOCK}.mutate((st) => { st.guideSeen = true; })`);

    // 羽化宽度：shadowBlur 不随画布变换缩放。把 dpr 假设成 2，倍率就明显不是 1，羽化要跟着乘
    await page.eval(`(() => {
        const w = ${FD}.defaultView;
        Object.defineProperty(w, "devicePixelRatio", { value: 2, configurable: true });
        const proto = w.CanvasRenderingContext2D.prototype;
        const d = Object.getOwnPropertyDescriptor(proto, "shadowBlur");
        w.__blurs = [];
        Object.defineProperty(proto, "shadowBlur", {
            configurable: true,
            get() { return d.get.call(this); },
            set(v) { w.__blurs.push(v); d.set.call(this, v); },
        });
    })()`);
    await openGuide();
    const feather = await page.eval(
        `({ blur: ${FD}.defaultView.__blurs.at(-1), k: ${q(".guide__mask")}.width / ${DESIGN.shell.w} })`,
    );
    run.check(feather.k >= 1.99, `画布后备倍率应含 dpr（实得 ${feather.k.toFixed(2)}）`);
    run.check(
        Math.abs(feather.blur - GEOM.feather * feather.k) < 0.05,
        `羽化宽度应为设计坐标里的 ${GEOM.feather} 乘后备倍率（实得 ${feather.blur}，倍率 ${feather.k.toFixed(2)}）`,
    );
    await key("Escape");

    // ---- ④ 语言 ----
    await click('[data-lang="en"]');
    await openGuide();
    run.eq(await title(), guide.en["guide.input.t"], "EN：标题是英文");
    run.eq(await where(), fmt(guide.en["guide.step"], { n: 1, total }), "EN：步数是英文");
    run.eq(
        await page.eval(`${q('[data-part="guide-next"]')}.textContent`),
        guide.en["guide.next"],
        "EN：下一步按钮是英文",
    );
    await key("Escape");
    await click('[data-lang="zh"]');

    // ---- ⑤ 让位 ----
    await click('[data-part="version"]');
    await page.waitFor(`!!${q('[data-part="about"]')} && !${q('[data-part="about"]')}.closest("[hidden]")`, {
        label: "关于窗应打开",
    });
    await page.eval(`${FD}.getElementById("shell").dispatchEvent(new CustomEvent("miciri:help", { bubbles: true }))`);
    run.eq(await isOpen(), false, "别的模态开着时，指南不开");
    await key("Escape");

    // ---- ⑥ 首次启动 ----
    await page.goto(`${server.url}/web-preview/index.html?guide=fresh`);
    await page.waitFor(`${FD}?.documentElement.dataset.ready === "1"`, { label: "首个快照应用后 data-ready 应为 1" });
    await page.waitFor(`!!${q('[data-part="guide-callout"]')}`, { label: "指南未看时，开窗后应自动打开" });
    run.eq(await stepId(), "input", "自动打开从第一步开始");
    run.eq(await seenCalls(), [], "自动打开时还没记“指南已看”");
    await key("Escape");
    await page.waitFor(`${MOCK}.calls.some((c) => c.name === "setGuideSeen")`, { label: "关掉后应记“指南已看”" });
    run.eq(await seenCalls(), ["setGuideSeen:[true]"], "关掉只记一次“指南已看”");
    await page.eval(`${MOCK}.sim.advance(1)`);
    await sleep(500);
    run.eq(await isOpen(), false, "关掉之后快照继续来，不再自动打开");
    // 保存没成功、快照里仍是未看：也不再弹（一次页面只自动开一回）
    await page.eval(`${MOCK}.mutate((st) => { st.guideSeen = false; })`);
    await sleep(500);
    run.eq(await isOpen(), false, "快照退回未看，也不再自动打开");
    await openGuide();
    await key("Escape");
    await sleep(300);
    run.eq(await seenCalls(), ["setGuideSeen:[true]"], "快照退回未看后再开再关，也只记过一次“指南已看”");

    // ---- ⑦ 全程 ----
    const external = requests
        .map((r) => (typeof r === "string" ? r : r.url))
        .filter((u) => !/^(http:\/\/127\.0\.0\.1|data:|about:)/.test(u));
    run.eq(external, [], "不应有任何外网请求");
    const errors = page.errors.filter((e) => !/\.woff2/.test(e));
    run.eq(errors, [], "不应有页面异常或 console.error");
    run.check(DESIGN.shell.w > 0, "设计盒尺寸可读");
} finally {
    await browser.close();
    await server.close();
}
run.finish();
