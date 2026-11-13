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
    await page.eval(`${M}.mutate((s) => { s.micType = "headset"; })`);
    await page.waitFor(`${q('[data-part="detail"]')}.parentElement.hidden`, { label: "方向卡没了，面板跟着关" });
    run.eq(await shown(), false, "耳机麦：方向卡消失时，打开着的方向详情自动关闭");

    await load("?lang=en", "tooClose");
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
    run.eq((await calls()).filter((c) => c[0] === "startStep").length, 0, "“去测方向”不直接开始测量");

    await load("", "retestedQuiet");
    const firstOf = () => page.eval(`[...${qa('[data-part="first"]')}].map((e) => e.closest(".rc").dataset.card)`);
    run.eq(await firstOf(), ["noise", "hum"], "重测过底噪：底噪与电流声卡写首次测量，增益卡走“上次 → 现在”");
    run.eq(await text('[data-part="first"]'), "首次测量", "首次测量的文案");
    await load("?lang=en", "retestedQuiet");
    run.eq(await text('[data-part="first"]'), "First measurement", "首次测量的英文");
    await load("", "evidence");
    run.eq(await firstOf(), [], "每步只做过一次：不写首次测量");
    await load("", "t20longDrlow"); // 这个场景检出了电流声
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
    void settle;
} finally {
    await browser.close();
    server.close();
}
run.finish();
