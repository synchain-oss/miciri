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
//   ⑧ 按钮措辞（重测这一步、去测方向）、首次测量、电流声写“高出本底”；
//   ⑨ 证据画布：按 evidenceRev 取数、图例、Ctrl+滚轮缩放与复位、键盘、快照更新后缩放与焦点不丢、没有证据时整块不渲染；
//   ⑩ getEvidence 回 no-evidence、回 null、抛错：详情窗照常，没有画布与空框，页面不报错，恢复后能画出来。
// 没有浏览器时整套跳过（退出码 2），不是通过。
//
// 用法：node web-preview/tests/smoke-report-page.mjs [仓库根]

import path from "node:path";
import { pathToFileURL } from "node:url";
import { createRun, repoRoot } from "./lib/harness.mjs";
import { NO_BROWSER_HINT, findBrowser, launchBrowser } from "./lib/browser.mjs";
import { startStaticServer } from "./lib/static-server.mjs";

const ROOT = repoRoot();
const run = createRun("报告页（页面）");
if (!findBrowser()) run.skip(NO_BROWSER_HINT);
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
    // 推荐距离是唯一写厘米的地方：查文案里有没有厘米数，要先把它拿掉
    const plain = (sel) =>
        page.eval(
            `(() => { const r = ${q(sel)}.cloneNode(true); r.querySelectorAll('[data-part="rec-dist"]').forEach((n) => n.remove()); return r.textContent; })()`,
        );
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
        // 开窗时落在接通页，界面已经自己开了一轮接通观察：停掉模拟里的那一轮和换场景放在同一拍里，
        // 界面再开的一轮落在新场景上，模拟和快照对得上
        await page.eval(
            `${M}.sim.cancel(), ${M}.mutate((s) => Object.assign(s, ${JSON.stringify(snap)}, { lang: ${JSON.stringify(lang)} }))`,
        );
        await page.eval(`${q('.step[data-step="report"]')}.click()`);
        await page.waitFor(`!!${q('[data-part="report-page"]')}`, { label: `报告页（${scene}）` });
    };
    // 开始的测量：接通观察是界面在接通页上自己开的，不算
    const measured = async () =>
        (await calls()).filter((c) => c[0] === "startStep" && c[1] !== "connect").map((c) => c[1]);
    const settle = () => page.waitFor(`!!${q('[data-part="report-page"]')}`, { label: "报告页" });

    // ---- ① 整页报告 ----
    await load("", "veryBoomy");
    run.eq(await page.eval(`${q(".fix")}.dataset.tone`), "red", "T20 很混：顶部是红色");
    run.check((await text(".fix__head")).length > 4, "顶部有一句话");
    run.eq(await page.eval(`${qa(".rp__grid .rc")}.length`), 9, "九张结果卡");
    run.eq(await exists(".chg"), true, "有“做了改动？”格");
    run.eq(await page.eval(`${q('[data-part="distance"]')}.dataset.st`), "fresh", "距离卡");
    run.eq(await exists(".stk--report .rp"), true, "报告在整页容器里，没有叠卡的上下露出");
    run.eq(await page.eval(`${qa(".stk__peek")}.length`), 0, "报告页没有摘要卡");
    const fitReport = () =>
        page.eval(`(() => {
        const main = ${q("#main")}.getBoundingClientRect();
        const bad = [];
        // 顶部一行的高度是固定的：卡里的内容不能把它撑出去压到下面的卡上
        const top = ${q(".rp__top")}.getBoundingClientRect();
        for (const n of ${qa(".rp .fix, .rp .dist")}) {
            if (n.getBoundingClientRect().bottom > top.bottom + 1) bad.push(n.className + " 超出顶部一行");
        }
        for (const n of ${qa(".rp .rc, .rp .fix, .rp .dist, .rp .chg")}) {
            const r = n.getBoundingClientRect();
            if (r.left < main.left - 1 || r.right > main.right + 1 || r.top < main.top - 1 || r.bottom > main.bottom + 1) bad.push(n.className + " 超出主区");
            if (n.scrollHeight > n.clientHeight + 1 || n.scrollWidth > n.clientWidth + 1) bad.push(n.className + " 内容溢出");
        }
        return bad;
    })()`);
    run.eq(await fitReport(), [], "报告里的卡片都在主区内、内容不溢出（卡内不滚动）");

    // ---- ② 色调与未测 ----
    const tone = (id) => page.eval(`${q(`.rp .rc[data-card="${id}"]`)}.dataset.tone`);
    run.check((await tone("sib")) !== "green" && (await tone("mouth")) !== "green", "齿音与口水音不着绿色（未分档）");
    run.eq(await text('.rp .rc[data-card="sib"] .rc__word'), "未分档", "齿音卡的词是“未分档”，不是“正常”");
    run.eq(await page.eval(`${q('.rp .rc[data-card="dir"]')}.dataset.st`), "none", "方向没测：灰色未测卡");
    run.check((await text('.rp .rc[data-card="dir"]')).includes("未测"), "未测卡写“未测”，不写“可信”");
    run.eq(await page.eval(`${qa(".ev")}.length`), 0, "第一次测位置：没有前后对比，不列三项读数");

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
    await page.waitFor(`${M}.calls.some((c) => c.name === "startStep" && c.args[0] !== "connect")`, {
        label: "重测这几项开始第一步",
    });
    run.eq(await measured(), ["gain"], "重测这几项：先开第一步（增益）");
    await page.eval(`${M}.sim.advance(100)`);
    await page.waitFor(`${q('[data-part="prep"]')}?.dataset.step === "quiet"`, {
        timeout: 8000,
        label: "第一步做完后落在下一步的准备页",
    });
    run.eq((await measured()).length, 1, "下一步由用户自己点开始，界面不替他开");

    // ---- ④ 方向提示、耳机麦 ----
    await load("", "veryBoomy");
    run.eq(await exists(".rc-slot__go"), true, "card-button：方向卡上有小按钮");
    await click(".rc-slot__go");
    await page.waitFor(`${q('[data-part="prep"]')}?.dataset.step === "direction"`, { label: "小按钮进方向准备页" });
    await load("", "conflicting");
    run.eq(await page.eval(`${q(".fix")}.dataset.tone`), "green", "说不准：距离不进最该改，顶部仍是绿的");
    run.eq(await page.eval(`${qa(".fix__btns .sc-btn")}.length`), 2, "主按钮下有去测方向的次要按钮");
    await click(".fix__btns .sc-btn:not(.sc-btn--cta)");
    await page.waitFor(`${q('[data-part="prep"]')}?.dataset.step === "direction"`, { label: "次要按钮进方向准备页" });
    await load("", "veryBoomy");
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
    await load("", "veryBoomy");
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
    // 接通页一出现界面就开一轮接通观察；这一轮还在跑时“再检查一次”不重复开，这一轮没了才开新的一轮
    const connectStarts = async () => (await calls()).filter((c) => c[0] === "startStep" && c[1] === "connect").length;
    const startsBefore = await connectStarts();
    await page.eval(`${q('.step[data-step="connect"]')}.click()`);
    await page.waitFor(`!!${q('[data-part="connect"]')}`, { label: "接通页" });
    await page.eval(`${M}.mutate((s) => { s.connect.signalState = "no-callback"; s.connect.callbackAgeSec = 3; })`);
    await page.waitFor(`${q('[data-part="connect"]')}?.dataset.mode === "nocb"`, { label: "没收到音频的诊断" });
    await page.waitFor(`${M}.sim.runningStep() === "connect"`, { label: "接通页一出现就开一轮接通观察" });
    run.eq(await connectStarts(), startsBefore + 1, "接通观察是界面自己开的，只开一轮");
    await click(".pn__cta .sc-btn:not(.sc-btn--cta)");
    await page.eval(`new Promise((r) => setTimeout(r, 200))`);
    run.eq(await connectStarts(), startsBefore + 1, "这一轮还在跑：“再检查一次”不重复开");
    await page.eval(`${M}.sim.cancel()`);
    await page.waitFor(
        `${M}.calls.filter((c) => c.name === "startStep" && c.args[0] === "connect").length === ${startsBefore + 2}`,
        {
            label: "这一轮没了、信号仍不正常：界面再开一轮",
        },
    );

    // ---- ⑥ 滚轮、英文 ----
    await load("", "veryBoomy");
    await page.eval(`${q("#main")}.dispatchEvent(new WheelEvent("wheel", { deltaY: -400, bubbles: true }))`);
    await page.eval(`new Promise((r) => setTimeout(r, 200))`);
    run.eq(await page.eval(`${q(".stk")}.dataset.step`), "report", "滚轮不会从报告翻回前一步");
    await load("?lang=en", "closer");
    const en = await page.eval(`${q(".rp")}.textContent`);
    run.check(!/[{}]|undefined|null/.test(en), "英文报告没有占位符残留");
    run.check(!/[一-鿿]/.test(en), "英文报告里没有汉字");
    await load("", "closer");
    const zh = await page.eval(`${q(".rp")}.textContent`);
    run.check(!/[{}]|undefined|null/.test(zh), "中文报告没有占位符残留");
    run.check(!/零延迟|绝对/.test(zh), "文案里没有禁用措辞");
    run.check(!/厘米|\bcm\b/.test(await plain(".rp")), "报告里除推荐距离外没有厘米数");

    // ---- ⑦ 详情面板 ----
    const open = (id) =>
        page.eval(`(() => { const b = ${q(`[data-part="card-open"][data-card="${id}"]`)}; b.focus(); b.click(); })()`);
    const shown = () => page.eval(`!${q('[data-part="detail"]')}.parentElement.hidden`);
    const inert = () => page.eval(`${q("#main")}.hasAttribute("inert")`);
    const esc = () =>
        page.eval(`${FD}.activeElement.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true }))`);
    await load("", "veryBoomy");
    run.eq(await page.eval(`${qa('[data-part="card-open"]')}.length`), 9, "每张卡都有“查看详情”按钮");
    const topmost = await page.eval(`(() => {
        const b = ${q('[data-part="dir-go"]')};
        const r = b.getBoundingClientRect();
        const hit = ${FD}.elementFromPoint(r.left + r.width / 2, r.top + r.height / 2);
        return hit === b || b.contains(hit);
    })()`);
    run.eq(topmost, true, "方向卡的小按钮在“查看详情”按钮之上，真的点得到（不是只能 .click()）");
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
    run.eq((await measured()).length, 0, "没有直接开始测量");

    await load("", "staleGain");
    await open("sib");
    run.eq(await page.eval(`${q('[data-part="detail"]')}.dataset.stale`), "1", "过期的齿音卡：面板标过期");
    run.check((await text(".dt__stale")).includes("调整之前"), "面板写“这是你调整之前的读数”");
    await click(".dt__cta .sc-btn");
    run.eq(await measured(), ["position"], "点“重测这一项”只开始它所属的位置步");

    const withDirection = SCENES.ready();
    Object.assign(withDirection.results, resultsFor("direction"));
    withDirection.steps.direction.status = "done";
    await load("", "ready");
    await page.eval(`${M}.mutate((s) => Object.assign(s, ${JSON.stringify(withDirection)}))`);
    await open("dir");
    run.eq(await page.eval(`${qa(".dt__tbl tbody tr")}.length`), 4, "方向详情：四个方向四行");
    run.eq(await exists(".dt__radar .radar svg"), true, "方向详情：有雷达");
    await page.eval(`${M}.mutate((s) => { s.micType = "headset"; })`);
    await page.waitFor(`${q('[data-part="detail"]')}.parentElement.hidden`, { label: "方向卡没了，面板跟着关" });
    run.eq(await shown(), false, "耳机麦：方向卡消失时，打开着的方向详情自动关闭");

    await load("?lang=en", "closer");
    await open("plosive");
    const dt = await text('[data-part="detail"]');
    run.check(!/[{}]|undefined|null|NaN/.test(dt), "英文详情没有占位符残留");
    run.check(!/[一-鿿]/.test(dt), "英文详情里没有汉字");
    // ---- ⑧ 按钮措辞、首次测量、电流声主数值 ----
    await load("", "staleGain");
    await open("sib");
    run.eq(await text(".dt__cta .sc-btn"), "重测这一步", "过期卡的按钮写“重测这一步”");
    const staleDir = SCENES.ready();
    Object.assign(staleDir.results, resultsFor("direction"));
    staleDir.results.direction.stale = true;
    staleDir.steps.direction.status = "stale";
    await load("", "ready");
    await page.eval(`${M}.mutate((s) => Object.assign(s, ${JSON.stringify(staleDir)}))`);
    await open("dir");
    run.eq(await text(".dt__cta .sc-btn"), "去测方向", "方向过期卡的按钮写“去测方向”");
    await click(".dt__cta .sc-btn");
    run.eq(await page.eval(`${q(".stk")}.dataset.step`), "direction", "“去测方向”去的是方向准备页");
    run.eq((await measured()).length, 0, "“去测方向”不直接开始测量");

    await load("", "retestedQuiet");
    const firstOf = () => page.eval(`[...${qa('[data-part="first"]')}].map((e) => e.closest(".rc").dataset.card)`);
    run.eq(await firstOf(), ["noise", "hum"], "重测过底噪：底噪与电流声卡写首次测量，增益卡走“上次 → 现在”");
    run.eq(await text('[data-part="first"]'), "首次测量", "首次测量的文案");
    await load("?lang=en", "retestedQuiet");
    run.eq(await text('[data-part="first"]'), "First measurement", "首次测量的英文");
    await load("", "evidence");
    run.eq(await firstOf(), [], "每步只做过一次：不写首次测量");
    await load("", "veryBoomy"); // 这个场景检出了电流声
    run.check(
        (await text('.rc[data-card="hum"] .rc__note')).includes("高出本底"),
        "电流声卡：数字是高出本底的 dB，占比降为次要",
    );
    await open("hum");
    const humText = await text('[data-part="detail"]');
    run.check(
        humText.includes("高出本底") && humText.includes("（参考）"),
        "电流声详情：写高出本底，占噪声能量只作参考",
    );
    run.eq(await exists(".dt__ev"), false, "电流声详情没有证据画布（频谱不在载荷里）");

    // ---- ⑨ 证据画布 ----
    const EV = '[data-part="evidence-canvas"]';
    const wheel = (init) =>
        page.eval(`(() => {
            const c = ${q(EV)}; const r = c.getBoundingClientRect();
            c.dispatchEvent(new WheelEvent("wheel", { bubbles: true, cancelable: true, clientX: r.left + r.width / 2, clientY: r.top + 40, ...${JSON.stringify(init)} }));
        })()`);
    const key = (k) =>
        page.eval(
            `${q(EV)}.dispatchEvent(new KeyboardEvent("keydown", { key: ${JSON.stringify(k)}, bubbles: true, cancelable: true }))`,
        );
    const axis = () => page.eval(`[...${qa(".dt__ax span")}].map((s) => s.textContent).join("|")`);
    const zoom = () => text('[data-part="evidence-zoom"]');
    const resetOff = () => page.eval(`${q('[data-part="evidence-reset"]')}.getAttribute("aria-disabled")`);
    const paint = () =>
        page.eval(`(() => {
            const c = ${q(EV)}; const k = c.width / c.clientWidth; const g = c.getContext("2d");
            const any = (y0, y1, min) => { const d = g.getImageData(0, Math.round(y0 * k), c.width, Math.max(1, Math.round((y1 - y0) * k))).data; for (let i = 3; i < d.length; i += 4) if (d[i] > min) return true; return false; };
            return { marks: any(0, 24, 150), body: any(40, 150, 60) };
        })()`);
    const evCalls = async () => (await calls()).filter((c) => c[0] === "getEvidence");
    const readyEv = () => page.waitFor(`!!${q(EV)}`, { label: "取到证据后画出画布" });
    await load("", "evidence");
    await open("plosive");
    await readyEv();
    run.eq(await evCalls(), [["getEvidence", "position"]], "打开喷麦卡：取一次位置步的证据");
    const cv = await page.eval(
        `(() => { const c = ${q(EV)}; return [c.getAttribute("role"), c.getAttribute("tabindex"), c.getAttribute("aria-label")]; })()`,
    );
    run.check(
        cv[0] === "img" && cv[1] === "0" && cv[2].length > 8 && !/[{}]/.test(cv[2]),
        "canvas 是 role=img、可聚焦、带标签",
    );
    run.eq(
        await page.eval(
            `[...${qa(".dt__leg [data-k]")}].map((e) => [e.dataset.k, e.dataset.cur || "0", !!e.querySelector("svg"), e.textContent])`,
        ),
        [
            ["plosive", "1", true, "喷麦"],
            ["sibilance", "0", true, "齿音"],
            ["mouth", "0", true, "口水音"],
        ],
        "图例：三种事件各一个形状加名字，当前类是喷麦",
    );
    run.eq([await zoom(), await resetOff()], ["缩放 1.00×", "true"], "缩放读数 1.00×，复位按钮此时不可用");
    const p0 = await paint();
    run.check(p0.marks && p0.body, "画布上画出了包络和事件标记");
    const a0 = await axis();
    run.eq(a0, "0 s|15 s|30 s|45 s|60 s", "整段 60 秒：五个时间刻度");
    await wheel({ ctrlKey: true, deltaY: -100 });
    run.eq([await zoom(), await resetOff()], ["缩放 1.25×", "false"], "Ctrl+滚轮向上一格放大一档，复位按钮可用");
    run.check((await axis()) !== a0, "放大后时间刻度跟着变");
    await wheel({ deltaY: 100 });
    const aPan = await axis();
    run.check(aPan.split("|")[0] !== "6 s", "普通滚轮平移：刻度跟着动");
    await wheel({ ctrlKey: true, deltaY: 100 });
    run.eq([await zoom(), await axis()], ["缩放 1.00×", a0], "Ctrl+滚轮向下缩回 1.00×，刻度回到整段");
    for (let i = 0; i < 9; i++) await wheel({ ctrlKey: true, deltaY: -10 });
    run.eq(await zoom(), "缩放 1.00×", "触摸板的小增量（九次共 90）不走档");
    await wheel({ ctrlKey: true, deltaY: -10 });
    run.eq(await zoom(), "缩放 1.25×", "攒够 100 走一档");
    for (let i = 0; i < 20; i++) await wheel({ ctrlKey: true, deltaY: -100 });
    run.eq(await zoom(), "缩放 16.00×", "最多放大到 16×");
    run.check(/\.\d/.test(await axis()), "放大后时间刻度出现小数位");
    run.check((await paint()).body, "放大后包络仍在画");
    await click('[data-part="evidence-reset"]');
    run.eq([await zoom(), await axis(), await resetOff()], ["缩放 1.00×", a0, "true"], "复位按钮回到整段");
    await key("+");
    run.eq(await zoom(), "缩放 1.25×", "键盘 + 放大");
    await key("ArrowRight");
    run.check((await axis()) !== a0, "键盘 → 平移");
    await key("0");
    run.eq([await zoom(), await axis()], ["缩放 1.00×", a0], "键盘 0 复位");
    // 普通滚轮：能平移才拦截；没放大或已平移到头时不拦截，滚轮留给面板滚动
    const wheelTaken = async (deltaY) =>
        !(await page.eval(
            `${q(EV)}.dispatchEvent(new WheelEvent("wheel", { bubbles: true, cancelable: true, deltaY: ${deltaY} }))`,
        ));
    run.eq(await wheelTaken(100), false, "没放大：普通滚轮不拦截，留给面板滚动");
    await key("+");
    run.eq(await wheelTaken(100), true, "放大后还能向右平移：拦截并平移");
    for (let i = 0; i < 20; i++) await wheelTaken(100);
    run.eq(await wheelTaken(100), false, "平移到右端：不再拦截，面板可以继续滚");
    run.eq(await wheelTaken(-100), true, "到头以后向左仍能平移：拦截");
    await key("0");
    // 快照更新：缩放不丢，焦点还在画布上，不重复取证据；这一步的 evidenceRev 变了才再取一次，并回到整段
    await key("+");
    await page.eval(`${q(EV)}.focus()`);
    await page.eval(`${M}.mutate((s) => { s.steps.gain.progressSec = 3; })`);
    await page.eval(`new Promise((r) => setTimeout(r, 100))`);
    run.eq(await zoom(), "缩放 1.25×", "快照更新后缩放读数还在");
    run.eq(await page.eval(`${FD}.activeElement.dataset.part`), "evidence-canvas", "快照更新后焦点还在画布上");
    run.eq((await evCalls()).length, 1, "evidenceRev 没变：不重复取证据");
    await page.eval(`${M}.mutate((s) => { s.steps.position.evidenceRev = 2; })`);
    await page.waitFor(`${M}.calls.filter((c) => c.name === "getEvidence").length === 2`, {
        label: "evidenceRev 变了再取一次",
    });
    await page.waitFor(`${q('[data-part="evidence-zoom"]')}?.textContent === "缩放 1.00×"`, {
        label: "新证据到了，视图回到整段",
    });
    run.eq(
        await page.eval(`(() => { ${M}.mutate((s) => { s.steps.position.evidenceRev = 3; }); return !!${q(EV)}; })()`),
        false,
        "evidenceRev 又变了：新数据到之前不再画旧一版的波形",
    );
    await readyEv();
    run.check(
        await page.eval(
            `(() => { const a = ${q('[data-part="evidence"]')}.getBoundingClientRect(), b = ${q('[data-part="detail"]')}.getBoundingClientRect(); return a.left >= b.left && a.right <= b.right; })()`,
        ),
        "画布整块在面板之内",
    );
    await esc();
    await open("level");
    await readyEv();
    run.eq(await page.eval(`${qa(".dt__leg [data-k]")}.length`), 0, "增益详情：只有包络，没有事件图例");
    run.eq((await paint()).marks, false, "增益详情：画布顶部没有事件标记");
    run.check((await paint()).body, "增益详情：有包络");
    await esc();
    await open("noise");
    run.eq(await exists(".dt__ev"), false, "底噪详情没有证据画布");
    await esc();
    await load("", "ready");
    await open("plosive");
    await page.eval(`new Promise((r) => setTimeout(r, 100))`);
    run.eq([await exists(".dt__ev"), (await evCalls()).length], [false, 0], "这一步没有证据：整块不渲染，也不去取");
    await esc();
    await load("?lang=en", "evidence");
    await open("sib");
    await readyEv();
    run.eq(
        await page.eval(`[...${qa(".dt__leg [data-k]")}].map((e) => e.textContent + (e.dataset.cur ? "*" : ""))`),
        ["Plosives", "Sibilance*", "Mouth clicks"],
        "英文图例，当前类是齿音",
    );
    const evEn = await text('[data-part="evidence"]');
    run.check(
        !/[一-鿿]|[{}]|undefined|NaN/.test(evEn) && evEn.includes("Zoom 1.00×"),
        "英文画布区没有汉字与占位符残留",
    );
    // ---- ⑩ 取不到证据：详情窗照常，不画布、不留空框、不报错 ----
    const faults = [
        ["no-evidence", "回 no-evidence"],
        ["null", "回 null"],
        ["reject", "抛错"],
    ];
    for (const [mode, label] of faults) {
        await load("", "evidence");
        await page.eval(`${M}.setEvidenceFault(${JSON.stringify(mode)})`);
        const errorsBefore = page.errors.length;
        await open("plosive");
        await page.waitFor(`${M}.calls.some((c) => c.name === "getEvidence")`, {
            label: `getEvidence ${label}：先取一次`,
        });
        await page.eval(`new Promise((r) => setTimeout(r, 150))`);
        run.eq(await shown(), true, `getEvidence ${label}：详情窗照常打开`);
        run.check((await page.eval(`${qa(".dt__tbl tbody tr")}.length`)) >= 1, `getEvidence ${label}：表格照常有读数`);
        run.eq(
            [await exists(".dt__ev"), await exists(EV), await page.eval(`${qa(".dt__leg [data-k]")}.length`)],
            [false, false, 0],
            `getEvidence ${label}：整块不渲染，没有画布、图例与空框`,
        );
        run.eq(page.errors.slice(errorsBefore), [], `getEvidence ${label}：页面没有报错`);
        run.eq((await evCalls()).length, 1, `getEvidence ${label}：只取了一次，没有重试风暴`);
        // 故障恢复后，这一步有新的证据（evidenceRev 变了）就能画出来
        await page.eval(`${M}.setEvidenceFault(null)`);
        await page.eval(`${M}.mutate((s) => { s.steps.position.evidenceRev = 5; })`);
        await readyEv();
        run.eq(await exists(".dt__ev"), true, `getEvidence ${label}：恢复后下一版证据能画出来`);
        await esc();
    }
    // ---- ⑪ 参考值、轻提示、推荐距离 ----
    const goPos = async (part) => {
        await page.eval(`${q('.step[data-step="position"]')}.click()`);
        await page.waitFor(`!!${q(`[data-part="${part}"]`)}`, { label: `位置步的${part}页` });
    };
    for (const lang of ["zh", "en"]) {
        const en = lang === "en";
        await load(en ? "?lang=en" : "", "overReference");
        const cardText = (id) => text(`.rp .rc[data-card="${id}"]`);
        run.eq(
            await page.eval(`${qa('.rp .rc .rc__ref[data-over="1"]')}.length`),
            3,
            `${lang}：喷麦、齿音、口水音三张卡的参考行都标出“超过”`,
        );
        const TIP_WORDS = en ? /pop filter|de-ess|step back|water/i : /防喷罩|齿音消除|退后一点|喝点水/;
        run.check(
            !TIP_WORDS.test(await page.eval(`[...${qa(".rp .rc")}].map((c) => c.textContent).join(" ")`)),
            `${lang}：报告页的卡很窄，只画参考行与“超过参考”标记，提示的话在详情窗里，不在卡上`,
        );
        run.eq(
            await page.eval(
                `${qa(".rp .rc:not([data-card=plosive]):not([data-card=sib]):not([data-card=mouth]) .rc__ref")}.length`,
            ),
            0,
            `${lang}：其余的卡没有参考行`,
        );
        // 完整提示在详情窗里：喷麦（防喷罩、推荐距离）与齿音（偏轴、齿音消除）
        await page.eval(`${q('[data-part="card-open"][data-card="plosive"]')}.click()`);
        await page.waitFor(`!!${q('[data-part="detail"] .dt__notes')}`, { label: "喷麦详情" });
        const pn = await text('[data-part="detail"] .dt__notes');
        run.check(
            pn.includes(en ? "pop filter" : "防喷罩") &&
                pn.includes("15 cm") &&
                pn.includes(en ? "rule of thumb" : "经验值"),
            `${lang}：喷麦详情写参考、完整提示与推荐距离`,
        );
        await page.eval(
            `${FD}.activeElement.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true }))`,
        );
        await page.eval(`${q('[data-part="card-open"][data-card="sib"]')}.click()`);
        await page.waitFor(`!!${q('[data-part="detail"] .dt__notes')}`, { label: "齿音详情" });
        const sn = await text('[data-part="detail"] .dt__notes');
        run.check(
            sn.includes(en ? "15–30°" : "15–30°") &&
                sn.includes(en ? "de-ess" : "齿音消除") &&
                !sn.includes(en ? "step back" : "退后"),
            `${lang}：齿音详情写偏轴 15–30° 与齿音消除，不说退后`,
        );
        await page.eval(
            `${FD}.activeElement.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true }))`,
        );
        run.check(
            (await cardText("plosive")).includes(en ? "under 9 dB" : "9 dB 以下") &&
                (await cardText("sib")).includes(en ? "under −3 dB" : "−3 dB 以下") &&
                (await cardText("mouth")).includes(en ? "under 6 per minute" : "每分钟 6 次以下"),
            `${lang}：参考行写出 bands 里的数（9、−3、6）`,
        );
        run.eq(await fitReport(), [], `${lang}：带参考行与提示的报告也不溢出（卡内不滚动）`);
        run.eq(await page.eval(`${q(".fix")}.dataset.tone`), "green", `${lang}：三条提示都不进“最该改”，顶部仍是绿色`);
        const body = await plain(".rp");
        run.check(!/[{}]|undefined|null|NaN/.test(body), `${lang}：报告没有占位符残留`);
        run.check(
            en ? !/[一-鿿]/.test(body) : !/厘米|\bcm\b|零延迟|绝对/.test(body),
            `${lang}：文案里没有${en ? "汉字" : "厘米数和禁用措辞"}`,
        );
        // 位置结果页：推荐距离是静态建议，只有这一处写厘米
        await goPos("result");
        const rec = await text('[data-part="rec-dist"]');
        run.check(
            rec.includes("15 cm") && (en ? rec.includes("10–30 cm") : rec.includes("10 cm") && rec.includes("30 cm")),
            `${lang}：位置结果页写推荐距离（15 cm，10 到 30 cm），数值取自快照`,
        );
        run.check(
            !/\bcm\b|厘米/.test(
                await page.eval(
                    `(() => { const r = ${q(".res")}.cloneNode(true); r.querySelector('[data-part="rec-dist"]').remove(); return r.textContent; })()`,
                ),
            ),
            `${lang}：位置结果页除推荐距离外不写厘米`,
        );
        run.check(
            !TIP_WORDS.test(await page.eval(`[...${qa(".res .rc")}].map((c) => c.textContent).join(" ")`)),
            `${lang}：卡片很窄，位置结果页的卡上也只有参考行与“超过参考”标记，提示的话只在右侧动作里`,
        );
        const acts = await page.eval(`[...${qa(".res__acts li")}].map((li) => li.textContent)`);
        run.check(
            acts.length === 3 &&
                acts[0].includes(en ? "pop filter" : "防喷罩") &&
                acts[1].includes(en ? "de-ess" : "齿音消除") &&
                acts[2].includes(en ? "step back" : "退后一点"),
            `${lang}：位置结果页右侧按卡的顺序列出三条轻提示（喷麦、齿音、口水音）`,
        );
        run.eq(
            await page.eval(`(() => {
                const res = ${q(".res")}.getBoundingClientRect();
                const bad = [];
                if (${qa(".res .rc")}.length !== 4) bad.push("卡不是四张");
                for (const n of ${qa(".res .rc, .res__acts, .recd, .pn__cta .sc-btn")}) {
                    const r = n.getBoundingClientRect();
                    if (r.bottom > res.bottom - 1 || r.right > res.right - 1) bad.push(n.className + " 超出结果面板");
                    if (n.scrollHeight > n.clientHeight + 1) bad.push(n.className + " 内容溢出");
                }
                const cta = ${q(".res .pn__cta")}.getBoundingClientRect();
                for (const c of ${qa(".res .rc")}) if (c.getBoundingClientRect().bottom > cta.top + 1) bad.push("卡压到了按钮");
                return bad;
            })()`),
            [],
            `${lang}：位置结果页的卡、动作、推荐距离与按钮都在面板内，不互相压住`,
        );
        // 准备页（重测）：同一条推荐距离
        await page.eval(`${M}.mutate((s) => { s.steps.position.status = "idle"; })`);
        await page.waitFor(`!!${q('[data-part="prep"]')}`, { label: "位置步变回准备页" });
        run.check(
            (await text('[data-part="prep"] [data-part="rec-dist"]')).includes("15 cm"),
            `${lang}：位置步的准备页写推荐距离`,
        );
        run.eq(
            await page.eval(`(() => {
                const p = ${q('[data-part="prep"]')}.getBoundingClientRect();
                return [...${qa('[data-part="prep"] .pn__cta .sc-btn, [data-part="prep"] .recd')}]
                    .filter((n) => n.getBoundingClientRect().bottom > p.bottom - 1)
                    .map((n) => n.className);
            })()`),
            [],
            `${lang}：位置步的准备页加了推荐距离后，按钮与推荐距离仍在面板内`,
        );
    }
    // 各种情形下报告里的卡都不溢出（第二行加了参考行，第一行只剩更少的位置）
    for (const lang of ["zh", "en"]) {
        for (const scene of [
            "ready",
            "veryBoomy",
            "closer",
            "conflicting",
            "lowConfidence",
            "staleGain",
            "stalePosition",
            "staleRoom",
            "maybeTooFar",
            "farther",
            "noChange",
            "retestedQuiet",
            "evidence",
            "overReference",
            "overReferenceHeadset",
        ]) {
            await load(lang === "en" ? "?lang=en" : "", scene);
            run.eq(await fitReport(), [], `${lang} 场景 ${scene}：报告里的卡片不溢出`);
        }
    }
    // 动圈：约一段，没有范围句；耳机麦：不显示，卡片提示不涉及距离
    await load("", "overReference");
    await page.eval(
        `${M}.mutate((s) => { s.micType = "dynamic"; s.recommendedDistance = { aroundLowCm: 5, aroundHighCm: 10, minCm: 5, maxCm: 10 }; })`,
    );
    await goPos("result");
    run.eq((await text('[data-part="rec-dist"] b')).replace(/\s/g, ""), "推荐距离约5–10cm", "动圈：推荐约 5–10 cm");
    await load("", "overReferenceHeadset");
    await goPos("result");
    const headActs = await page.eval(`[...${qa(".res__acts li")}].map((li) => li.textContent)`);
    run.check(
        headActs.length === 3 && headActs[0].includes("防喷罩") && !headActs[0].includes("退后"),
        "耳机麦：喷麦提示只说防喷罩，不说退后",
    );
    run.check(headActs[2].includes("喝点水") && !headActs[2].includes("退后"), "耳机麦：口水音提示不说退后");
    run.eq(await exists('[data-part="rec-dist"]'), false, "耳机麦：位置结果页不显示推荐距离");
    await load("", "overReference");
    await page.eval(
        `${M}.mutate((s) => { s.steps.position.status = "idle"; s.micType = "headset"; s.recommendedDistance = null; })`,
    );
    await goPos("prep");
    run.eq(await exists('[data-part="rec-dist"]'), false, "耳机麦：位置步的准备页不显示推荐距离");

    // ---- ⑫ 距离卡：第一次的弱提示与重测后的前后对比 ----
    const dist = (sel) => `${q('[data-part="distance"]')}.querySelector(${JSON.stringify(sel)})`;
    const mid = (sel) =>
        page.eval(
            `(() => { const r = ${dist(sel)}?.getBoundingClientRect(); return r ? r.left + r.width / 2 : null; })()`,
        );
    for (const lang of ["zh", "en"]) {
        const en = lang === "en";
        const qs = en ? "?lang=en" : "";
        await load(qs, "maybeTooFar");
        run.eq(
            [
                await exists('[data-part="distance"] [data-part="hint"]'),
                await exists('[data-part="compare"]'),
                await exists(".ev"),
            ],
            [true, false, false],
            `${lang}：第一次：只有“可能离远了”，没有前后标尺和三项读数`,
        );
        run.check(
            (await text('[data-part="distance"] [data-part="hint"]')).includes(en ? "Maybe too far" : "可能离远了"),
            `${lang}：弱提示的字`,
        );
        run.check(
            (await text('[data-part="distance"] [data-part="rec-dist"]')).includes("15 cm"),
            `${lang}：距离卡上写推荐距离`,
        );
        run.eq(await page.eval(`${q(".fix")}.dataset.tone`), "red", `${lang}：底噪偏大的顶部卡是红的`);
        await click(".fix__btns .sc-btn--cta");
        await page.waitFor(`${q('[data-part="prep"]')}?.dataset.step === "direction"`, {
            label: "可能离远了且方向没测：主按钮去方向准备页",
        });
        for (const scene of ["closer", "farther", "noChange", "conflicting"]) {
            await load(qs, scene);
            const items = await page.eval(`[...${qa(".dist .ev")}].map((e) => e.dataset.k + ":" + e.dataset.sign)`);
            run.eq(items.length, 3, `${lang} ${scene}：三项读数各一格（D/R、喷麦、齿音）`);
            run.eq(
                await exists('[data-part="distance"] [data-part="hint"]'),
                false,
                `${lang} ${scene}：重测后不再有弱提示`,
            );
            const dt = await text('[data-part="distance"]');
            run.check(!/[{}]|undefined|null|NaN/.test(dt), `${lang} ${scene}：距离卡没有占位符`);
            run.check(en ? !/[一-鿿]/.test(dt) : true, `${lang} ${scene}：英文距离卡里没有汉字`);
        }
        await load(qs, "closer");
        const ghost = await mid(".dist__ghost");
        run.check((await mid(".dist__mk")) < ghost, `${lang}：更近：现在的标记在上次的虚影左边`);
        run.check(
            (await text('[data-part="distance"]')).includes(en ? "Gain changed" : "增益变了，不比"),
            `${lang}：调过增益的齿音写“不比”，不假装比过`,
        );
        run.check((await text('[data-part="distance"]')).includes("+4.1"), `${lang}：比得出来的写差值`);
        await load(qs, "farther");
        run.check((await mid(".dist__mk")) > (await mid(".dist__ghost")), `${lang}：更远：标记在虚影右边`);
        await load(qs, "noChange");
        run.check(
            Math.abs((await mid(".dist__mk")) - (await mid(".dist__ghost"))) < 2,
            `${lang}：没有明显变化：标记和虚影重合`,
        );
        await load(qs, "conflicting");
        run.eq(
            [await exists(".dist__ghost"), await exists(".dist__mk")],
            [true, false],
            `${lang}：几项指向不同方向：只有虚影，不画现在的标记`,
        );
        await load(qs, "stalePosition");
        run.eq(
            await page.eval(`${q('[data-part="distance"]')}.dataset.st`),
            "stale",
            `${lang}：位置过期：距离卡标过期`,
        );
        run.eq(await exists(".dist__mk"), false, `${lang}：过期的距离卡不画标记`);
        await load(qs, "overReferenceHeadset");
        run.eq(await exists('.dist [data-part="rec-dist"]'), false, `${lang}：耳机麦：距离卡不写推荐距离`);
    }
    void settle;
} finally {
    await browser.close();
    server.close();
}
run.finish();
