// SPDX-License-Identifier: GPL-3.0-or-later
//
// 报告页面冒烟（无头浏览器）：预览页用假桥装载真源页面，端到端核对
//   ① 整页报告：顶部“最该改”与距离标尺，九张结果卡加“做了改动？”格，内容都在设计盒内、卡内不溢出；
//   ② 齿音与口水音卡片无色（不着绿）、过期卡变灰、未测卡写“未测”；
//   ③ 报告动作：三个“我动过了”走 reportChange，快照回来后顶部换成过期的说法；“重测这几项”先开第一步，其余落在准备页；
//   ④ 方向提示：方向卡上的小按钮进方向准备页；耳机麦没有方向卡；
//   ⑤ 复制结果：剪贴板可用时给提示条，不可用时弹出手动复制窗；
//   ⑥ 滚轮不会从报告翻回前一步；英文下没有占位符残留与汉字；
//   ⑦ 详情面板：点卡打开、Esc 与遮罩关闭、焦点还回去；过期卡写明已过期并能重测它所属的步骤；方向有雷达与四行表。
// 没有浏览器时整套跳过（退出码 2），不是通过。
//
// 用法：node web-preview/tests/smoke-report-page.mjs [仓库根]

import path from "node:path";
import { pathToFileURL } from "node:url";
import { createRun, repoRoot } from "./lib/harness.mjs";
import { findBrowser, launchBrowser } from "./lib/browser.mjs";
import { startStaticServer } from "./lib/static-server.mjs";

const ROOT = repoRoot();
const run = createRun("报告页（页面）");
if (!findBrowser()) run.skip("没有 Chromium 系浏览器；可用环境变量 MICIRI_BROWSER 指定");
const { SCENES } = await import(pathToFileURL(path.join(ROOT, "web-preview/mock/report-fixtures.js")).href);
const { resultsFor } = await import(pathToFileURL(path.join(ROOT, "web-preview/mock/results.js")).href);

const FD = "document.getElementById('frame').contentDocument";
const M = "window.__MICIRI_MOCK__";

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
    const q = (sel) => `${FD}.querySelector(${JSON.stringify(sel)})`;
    const qa = (sel) => `${FD}.querySelectorAll(${JSON.stringify(sel)})`;
    const exists = (sel) => page.eval(`!!${q(sel)}`);
    const text = (sel) => page.eval(`${q(sel)}?.textContent ?? null`);
    const click = (sel) => page.eval(`${q(sel)}.click()`);
    const calls = () => page.eval(`${M}.calls.map((c) => [c.name, ...c.args])`);
    const load = async (query, scene) => {
        await page.goto(`${server.url}/web-preview/index.html${query}`);
        await page.waitFor(`${FD}?.documentElement.dataset.ready === "1"`, {
            label: "首个快照应用后 data-ready 应为 1",
        });
        await show(scene, query.includes("lang=en") ? "en" : "zh");
    };
    const show = async (scene, lang = "zh") => {
        const snap = SCENES[scene]();
        await page.eval(
            `${M}.mutate((s) => Object.assign(s, ${JSON.stringify(snap)}, { lang: ${JSON.stringify(lang)} }))`,
        );
        await page.eval(`${q('.step[data-step="report"]')}.click()`);
        await page.waitFor(`!!${q('[data-part="report-page"]')}`, { label: `报告页（${scene}）` });
    };
    const settle = () => page.waitFor(`!!${q('[data-part="report-page"]')}`, { label: "报告页" });

    // ---- ① 整页报告 ----
    await load("", "t20longDrlow");
    run.eq(await page.eval(`${q(".fix")}.dataset.tone`), "red", "T20 长 + D/R 低：顶部是红色");
    run.check((await text(".fix__head")).length > 4, "顶部有一句话");
    run.eq(await page.eval(`${qa(".rp__grid .rc")}.length`), 9, "九张结果卡");
    run.eq(await exists(".chg"), true, "有“做了改动？”格");
    run.eq(await page.eval(`${q(".ruler")}.dataset.st`), "fresh", "距离标尺");
    run.eq(await exists(".stk--report .rp"), true, "报告在整页容器里，没有叠卡的上下露出");
    run.eq(await page.eval(`${qa(".stk__peek")}.length`), 0, "报告页没有摘要卡");
    const fit = await page.eval(`(() => {
        const main = ${q("#main")}.getBoundingClientRect();
        const bad = [];
        for (const n of ${qa(".rp .rc, .rp .fix, .rp .ruler, .rp .chg")}) {
            const r = n.getBoundingClientRect();
            if (r.left < main.left - 1 || r.right > main.right + 1 || r.top < main.top - 1 || r.bottom > main.bottom + 1) bad.push(n.className + " 超出主区");
            if (n.scrollHeight > n.clientHeight + 1 || n.scrollWidth > n.clientWidth + 1) bad.push(n.className + " 内容溢出");
        }
        return bad;
    })()`);
    run.eq(fit, [], "报告里的卡片都在主区内、内容不溢出（卡内不滚动）");

    // ---- ② 色调与未测 ----
    const tone = (id) => page.eval(`${q(`.rp .rc[data-card="${id}"]`)}.dataset.tone`);
    run.check((await tone("sib")) !== "green" && (await tone("mouth")) !== "green", "齿音与口水音不着绿色（未分档）");
    run.eq(await text('.rp .rc[data-card="sib"] .rc__word'), "未分档", "齿音卡的词是“未分档”，不是“正常”");
    run.eq(await page.eval(`${q('.rp .rc[data-card="dir"]')}.dataset.st`), "none", "方向没测：灰色未测卡");
    run.check((await text('.rp .rc[data-card="dir"]')).includes("未测"), "未测卡写“未测”，不写“可信”");
    run.eq(await page.eval(`${qa(".ev")}.length`), 5, "标尺的证据标签：近 3 个，远 2 个");

    // ---- ③ 报告动作 ----
    await click(".chg .sc-btn:nth-of-type(1)");
    await page.waitFor(`${q(".fix")}?.dataset.tone === "gray"`, { label: "调了增益后顶部变成灰色" });
    run.eq(
        (await calls()).some((c) => c[0] === "reportChange" && c[1] === "gain"),
        true,
        "“我调了增益”走 reportChange",
    );
    run.eq((await page.eval(`${qa('.rp .rc[data-st="stale"]')}.length`)) > 0, true, "过期的卡变成虚线灰卡");
    run.check((await text(".fix__head")).includes("增益"), "顶部写“增益改过了”");
    await click(".fix__btns .sc-btn--cta");
    await page.waitFor(`${M}.calls.some((c) => c.name === "startStep")`, { label: "重测这几项开始第一步" });
    run.eq(
        (await calls()).filter((c) => c[0] === "startStep").map((c) => c[1]),
        ["gain"],
        "重测这几项：先开第一步（增益）",
    );
    await page.eval(`${M}.sim.advance(100)`);
    await page.waitFor(`${q('[data-part="prep"]')}?.dataset.step === "quiet"`, {
        timeout: 8000,
        label: "第一步做完后落在下一步的准备页",
    });
    run.eq((await calls()).filter((c) => c[0] === "startStep").length, 1, "下一步由用户自己点开始，界面不替他开");

    // ---- ④ 方向提示、耳机麦 ----
    await load("", "t20longDrlow");
    run.eq(await exists(".rc-slot__go"), true, "card-button：方向卡上有小按钮");
    await click(".rc-slot__go");
    await page.waitFor(`${q('[data-part="prep"]')}?.dataset.step === "direction"`, { label: "小按钮进方向准备页" });
    await load("", "conflict");
    run.eq(await page.eval(`${qa(".fix__acts li")}.length`), 3, "矛盾：动作 3 条");
    run.eq(await page.eval(`${q(".fix__acts li:last-child")}.dataset.dirhint`), "act", "方向这条排最后");
    run.eq(await page.eval(`${qa(".fix__btns .sc-btn")}.length`), 2, "主按钮下有次要按钮");
    await load("", "t20longDrlow");
    await page.eval(`${M}.mutate((s) => { s.micType = "headset"; })`);
    await page.waitFor(`${q(".rp__grid")}?.dataset.n === "8"`, { label: "耳机麦：八张卡" });
    run.eq(
        [await exists('.rp .rc[data-card="dir"]'), await exists(".rc-slot__go")],
        [false, false],
        "耳机麦没有方向卡与小按钮",
    );

    // ---- ⑤ 复制结果 ----
    await load("", "ready");
    run.eq(await page.eval(`${q(".fix")}.dataset.tone`), "green", "都好：绿色");
    await page.eval(`(() => {
        const w = ${FD}.defaultView;
        w.__copied = null;
        Object.defineProperty(w.navigator, "clipboard", { configurable: true, value: { writeText: async (t) => { w.__copied = t; } } });
    })()`);
    await click(".fix__btns .sc-btn--cta");
    await page.waitFor(`${FD}.defaultView.__copied !== null`, { label: "复制写进剪贴板" });
    run.check((await page.eval(`${FD}.defaultView.__copied`)).startsWith("Miciri"), "复制的是一句话结果");
    run.eq(await page.eval(`${q('[data-part="toast"]')}.dataset.state`), "report.copied", "复制成功给提示条");
    await page.eval(`(() => {
        const w = ${FD}.defaultView;
        Object.defineProperty(w.navigator, "clipboard", { configurable: true, value: { writeText: async () => { throw new Error("denied"); } } });
        ${FD}.execCommand = () => false;
    })()`);
    await click(".chg .sc-btn:last-of-type");
    await page.waitFor(`!${q('[data-part="copy-manual"]')}?.closest("[hidden]")`, {
        label: "复制不可用时弹出手动复制窗",
    });
    run.check(
        (await page.eval(`${q('[data-part="copy-manual"] textarea')}.value`)).startsWith("Miciri"),
        "手动复制窗里是同一句话",
    );

    // ---- 流程页的挂账 ----
    await load("", "t20longDrlow");
    await page.eval(
        `${M}.mutate((s) => { s.steps.gain.durationSec = 0; s.steps.gain.status = "idle"; delete s.results.gain; })`,
    );
    await page.eval(`${q('.step[data-step="gain"]')}.click()`);
    await page.waitFor(`${q('[data-part="prep"]')}?.dataset.step === "gain"`, { label: "增益准备页" });
    run.check(!(await text('[data-part="prep"]')).includes("约 0"), "计划时长为 0：准备页不写“约 0 秒”");
    await page.eval(
        `${M}.mutate((s) => { delete s.bands; s.steps.gain.status = "done"; s.results.gain = ${JSON.stringify(SCENES.ready().results.gain)}; })`,
    );
    await page.waitFor(`${q('[data-part="result"]')}?.dataset.step === "gain"`, { label: "缺 bands 时仍是结果页" });
    run.eq(await exists('[data-part="prep"]'), false, "缺 bands：结果页不退回准备页");
    await page.eval(`${q('.step[data-step="connect"]')}.click()`);
    await page.waitFor(`!!${q('[data-part="connect"]')}`, { label: "接通页" });
    await page.eval(`${M}.mutate((s) => { s.connect.signalState = "no-callback"; s.connect.callbackAgeSec = 3; })`);
    await page.waitFor(`${q('[data-part="connect"]')}?.dataset.mode === "nocb"`, { label: "没收到音频的诊断" });
    await click(".pn__cta .sc-btn:not(.sc-btn--cta)");
    await page.waitFor(`${M}.calls.some((c) => c.name === "startStep" && c.args[0] === "connect")`, {
        label: "再检查一次开一轮接通观察",
    });
    run.eq(
        (await calls()).filter((c) => c[0] === "startStep" && c[1] === "connect").length,
        1,
        "接通页的“再检查一次”调 startStep(connect)，只调一次",
    );

    // ---- ⑥ 滚轮、英文 ----
    await load("", "t20longDrlow");
    await page.eval(`${q("#main")}.dispatchEvent(new WheelEvent("wheel", { deltaY: -400, bubbles: true }))`);
    await page.eval(`new Promise((r) => setTimeout(r, 200))`);
    run.eq(await page.eval(`${q(".stk")}.dataset.step`), "report", "滚轮不会从报告翻回前一步");
    await load("?lang=en", "tooClose");
    const en = await page.eval(`${q(".rp")}.textContent`);
    run.check(!/[{}]|undefined|null/.test(en), "英文报告没有占位符残留");
    run.check(!/[一-鿿]/.test(en), "英文报告里没有汉字");
    await load("", "tooClose");
    const zh = await page.eval(`${q(".rp")}.textContent`);
    run.check(!/[{}]|undefined|null/.test(zh), "中文报告没有占位符残留");
    run.check(!/厘米|\bcm\b|零延迟|绝对/.test(zh), "文案里没有厘米数和禁用措辞");

    // ---- ⑦ 详情面板 ----
    const open = (id) =>
        page.eval(`(() => { const b = ${q(`[data-part="card-open"][data-card="${id}"]`)}; b.focus(); b.click(); })()`);
    const shown = () => page.eval(`!${q('[data-part="detail"]')}.parentElement.hidden`);
    const inert = () => page.eval(`${q("#main")}.hasAttribute("inert")`);
    const esc = () =>
        page.eval(`${FD}.activeElement.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true }))`);
    await load("", "t20longDrlow");
    run.eq(await page.eval(`${qa('[data-part="card-open"]')}.length`), 9, "每张卡都有“查看详情”按钮");
    await open("level");
    run.eq(await shown(), true, "点增益卡：详情面板打开");
    run.eq(await text("#dt-title"), "增益", "面板标题是卡名");
    run.check((await page.eval(`${qa(".dt__tbl tbody tr")}.length`)) >= 3, "表格有读数行");
    run.eq(await page.eval(`${qa('.dt__tbl tr[data-hl="1"]')}.length`), 1, "表格有一行高亮");
    run.eq(await inert(), true, "打开后背景 inert");
    run.eq(await page.eval(`${FD}.activeElement.dataset.part`), "detail-close", "焦点在关闭按钮上");
    run.eq(await exists(".dt__stale"), false, "有效的卡不写“已过期”");
    await esc();
    run.eq(await shown(), false, "Esc 关闭");
    run.eq(await inert(), false, "关闭后背景恢复");
    run.eq(await page.eval(`${FD}.activeElement.dataset.card`), "level", "焦点回到打开它的卡");
    await open("noise");
    await page.eval(`${q('[data-part="detail"]')}.parentElement.click()`);
    run.eq(await shown(), false, "点遮罩关闭");
    await open("dir");
    run.check((await text('[data-part="detail"]')).includes("还没做方向检查"), "方向没测：写明还没做");
    await click(".dt__cta .sc-btn");
    run.eq(await shown(), false, "点“去测这一项”后面板关闭");
    run.eq(await page.eval(`${q(".stk")}.dataset.step`), "direction", "去的是方向的准备页");
    run.eq((await calls()).filter((c) => c[0] === "startStep").length, 0, "没有直接开始测量");

    await load("", "staleGain");
    await open("sib");
    run.eq(await page.eval(`${q('[data-part="detail"]')}.dataset.stale`), "1", "过期的齿音卡：面板标过期");
    run.check((await text(".dt__stale")).includes("调整之前"), "面板写“这是你调整之前的读数”");
    await click(".dt__cta .sc-btn");
    run.eq(
        (await calls()).filter((c) => c[0] === "startStep").map((c) => c[1]),
        ["position"],
        "点“重测这一项”只开始它所属的位置步",
    );

    const withDirection = SCENES.ready();
    Object.assign(withDirection.results, resultsFor("direction"));
    withDirection.steps.direction.status = "done";
    await load("", "ready");
    await page.eval(`${M}.mutate((s) => Object.assign(s, ${JSON.stringify(withDirection)}))`);
    await open("dir");
    run.eq(await page.eval(`${qa(".dt__tbl tbody tr")}.length`), 4, "方向详情：四个方向四行");
    run.eq(await exists(".dt__radar .radar svg"), true, "方向详情：有雷达");
    await esc();

    await load("?lang=en", "tooClose");
    await open("plosive");
    const dt = await text('[data-part="detail"]');
    run.check(!/[{}]|undefined|null|NaN/.test(dt), "英文详情没有占位符残留");
    run.check(!/[一-鿿]/.test(dt), "英文详情里没有汉字");
    void settle;
} finally {
    await browser.close();
    server.close();
}
run.finish();
