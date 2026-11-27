// SPDX-License-Identifier: GPL-3.0-or-later
//
// 流程页面冒烟（无头浏览器）：预览页用假桥装载真源页面，端到端核对
//   ① 接通页：接通观察自己开、一轮接一轮、听到声音后变成信号正常且不挪焦点，离开接通页或开别的步骤时先取消它；首次接通、信号正常、无信号的红色诊断（开始按钮不可用）、立体声一路无声（可点的药丸与声道提示窗）；
//   ② 叠卡：翻页（点上下露出的卡、PageDown/PageUp、滚轮）、耳机麦没有方向步、步骤条按状态着图标；
//   ③ 测试中：整窗远距离模式，指令 ≥ 48 px、念的字 ≥ 64 px，倒计时与拍手槽位随模拟推进，取消回到准备页；
//      手动分段：收够之前“结束”不可用并写明还差什么，够了用键盘结束，之后每段等“开始”、待开始时不倒数也不推进，
//      重来时退回待开始；自动推进（phases=auto）时没有这两个按钮；
//   ④ 成功屏与结果页：绿色成功屏之后落在这一步的结果页，数字与档位词来自快照，重测显示准备页；
//   ⑤ 左栏：结果标记与悬停提示、峰值读数；
//   ⑥ 英文下同样成立，没有占位符残留，没有页面异常。
// 没有浏览器时整套跳过（退出码 2），不是通过。
//
// 用法：node web-preview/tests/smoke-flow-page.mjs [仓库根]

import { createRun, repoRoot } from "./lib/harness.mjs";
import { NO_BROWSER_HINT, findBrowser, launchBrowser } from "./lib/browser.mjs";
import { startStaticServer } from "./lib/static-server.mjs";
import { checkFontsLoaded } from "./lib/fonts.mjs";

const ROOT = repoRoot();
const run = createRun("流程页（页面）");
if (!findBrowser()) run.skip(NO_BROWSER_HINT);

const FD = "document.getElementById('frame').contentDocument";
const M = "window.__MICIRI_MOCK__";
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
    const q = (sel) => `${FD}.querySelector(${JSON.stringify(sel)})`;
    const exists = (sel) => page.eval(`!!${q(sel)}`);
    const text = (sel) => page.eval(`${q(sel)}?.textContent ?? null`);
    const click = (sel) => page.eval(`${q(sel)}.click()`);
    const call = (js) => page.eval(`${M}.${js}`);
    const stackStep = () => page.eval(`${q(".stk")}?.dataset.step ?? null`);
    const mode = () => page.eval(`${FD}.getElementById("shell").dataset.remote`);
    const fontPx = (sel) => page.eval(`parseFloat(getComputedStyle(${q(sel)}).fontSize)`);
    const focused = () => page.eval(`${FD}.activeElement?.dataset.part ?? null`);
    const phaseCalls = async () =>
        (await page.eval(`${M}.calls.map((c) => c.name)`)).filter((n) => n === "startPhase" || n === "endPhase");
    // 真的按键（经浏览器的输入管线），落在页面里有焦点的元素上：按钮按 Enter 会触发 click
    const pressEnter = async () => {
        const key = { key: "Enter", code: "Enter", windowsVirtualKeyCode: 13, nativeVirtualKeyCode: 13 };
        await page.send("Input.dispatchKeyEvent", { type: "keyDown", text: "\r", ...key });
        await page.send("Input.dispatchKeyEvent", { type: "keyUp", ...key });
    };
    const goStep = async (id) => {
        await click(`.step[data-step="${id}"]`);
        await page.waitFor(`${q(".stk")}?.dataset.step === ${JSON.stringify(id)}`, { label: `翻到 ${id}` });
    };
    const loadPage = async (query = "") => {
        await page.goto(`${server.url}/web-preview/index.html${query}`);
        await page.waitFor(
            `${FD}?.documentElement.dataset.ready === "1" || !!${FD}?.documentElement.dataset.bootError`,
            {
                label: "首个快照应用后 data-ready 应为 1",
            },
        );
        run.eq(await page.eval(`${FD}.documentElement.dataset.bootError ?? null`), null, "不应有启动错误");
    };

    // ---- ① 接通页 ----
    await loadPage();
    await checkFontsLoaded(run, page, FD, "流程页");
    run.eq(await stackStep(), "connect", "没有结果时落在接通页");
    run.eq(await page.eval(`${q('[data-part="connect"]')}.dataset.mode`), "first", "首次接通页");
    run.eq(await page.eval(`${q('[data-part="cta"]')}.disabled`), true, "没有声音时开始按钮不可用");
    // 接通观察：接通页一出现界面就自己开一轮，不用点；没到“信号正常”就一轮接一轮，开始、结束都不挪焦点
    const connectStarts = () =>
        page.eval(`${M}.calls.filter((c) => c.name === "startStep" && c.args[0] === "connect").length`);
    await page.waitFor(`${M}.sim.runningStep() === "connect"`, { label: "接通页一出现就开一轮接通观察" });
    run.eq(await connectStarts(), 1, "接通观察是界面自己开的，开了一轮");
    await page.eval(`${FD}.getElementById("mic-sel").focus()`);
    await call(`sim.advance(6)`);
    await page.waitFor(`${M}.calls.filter((c) => c.name === "startStep" && c.args[0] === "connect").length === 2`, {
        label: "一轮没听到声音：接着开下一轮",
    });
    run.eq(await page.eval(`${FD}.activeElement?.id ?? null`), "mic-sel", "一轮结束、再开一轮都不挪焦点");
    run.eq(await page.eval(`${q('[data-part="connect"]')}.dataset.mode`), "first", "还没听到声音：仍是首次接通页");
    await call(`sim.setInput("ok")`);
    await call(`sim.advance(6)`);
    await page.waitFor(`${q('[data-part="connect"]')}?.dataset.mode === "ok"`, {
        label: "听到声音：这一轮结束后接通页换成信号正常",
    });
    run.eq(await page.eval(`${q('[data-part="cta"]')}.disabled`), false, "信号正常：开始增益测试可点");
    await sleep(200);
    run.eq(await connectStarts(), 2, "信号正常后不再开新的一轮");
    run.eq(await page.eval(`${M}.sim.runningStep()`), null, "信号正常后没有在跑的接通观察");
    await call(`sim.setInput(null)`);
    await call(`mutate((s) => { s.connect.signalState = "ok"; })`);
    await page.waitFor(`${q('[data-part="connect"]')}?.dataset.mode === "ok"`, { label: "信号正常后接通页应换成 ok" });
    run.eq(await page.eval(`${q('[data-part="cta"]')}.disabled`), false, "信号正常后开始按钮可用");
    await call(`mutate((s) => { s.connect.signalState = "no-callback"; s.connect.callbackAgeSec = 3; })`);
    await page.waitFor(`${q('[data-part="connect"]')}?.dataset.mode === "nocb"`, { label: "没收到音频的诊断" });
    run.eq(await page.eval(`${q('[data-part="cta"]')}.disabled`), true, "没收到音频：开始按钮不可用");
    run.eq(await page.eval(`${q(".dg__ic")}.dataset.tone`), "red", "没收到音频：红色图标");
    run.check((await text(".pn__lead")).includes("3"), "没收到音频：说明里带秒数（来自快照）");
    run.eq(
        await page.eval(`!!${q("#dg-why")} && ${q('[data-part="cta"]')}.getAttribute("aria-describedby") === "dg-why"`),
        true,
        "不可用的按钮指向原因",
    );
    await call(
        `mutate((s) => { s.connect.signalState = "ok"; s.device.layout = "stereo"; s.connect.channelStatusL = "signal"; s.connect.channelStatusR = "silent"; s.connect.channelPrompt = "one-silent"; })`,
    );
    await page.waitFor(`${q('[data-part="connect"]')}?.dataset.mode === "st1"`, { label: "一路无声的诊断" });
    run.eq(await page.eval(`${q('[data-part="pill-action"]')}.hidden`), false, "左栏药丸出现可点的声道提示");
    await click('[data-part="pill-action"]');
    run.eq(await exists('[data-part="channel-prompt"]'), true, "点药丸打开声道提示窗");
    run.eq(await page.eval(`!${q('[data-part="channel-prompt"]')}.closest("[hidden]")`), true, "声道提示窗可见");
    await click('[data-part="chp-close"]');
    run.eq(
        await page.eval(`${q('[data-part="channel-prompt"]')}.closest("[hidden]") !== null`),
        true,
        "关闭声道提示窗",
    );
    await call(`mutate((s) => { s.connect.channelPrompt = "identical"; })`);
    await page.waitFor(`${q('[data-part="connect"]')}?.dataset.mode === "same"`, { label: "两路相同的诊断" });
    await click('[data-part="dismiss"]');
    await page.waitFor(`${q('[data-part="connect"]')}?.dataset.mode === "ok"`, { label: "不再提醒后回到正常页" });
    run.check((await page.eval(`${M}.calls.map((c) => c.name)`)).includes("dismissChannelPrompt"), "“不再提醒”走桥");
    await call(`mutate((s) => { s.device.layout = "mono"; s.connect.channelPrompt = "none"; })`);

    // ---- ② 叠卡与步骤条 ----
    const names = await page.eval(`[...${FD}.querySelectorAll(".step")].map((b) => b.dataset.step)`);
    run.eq(names, ["connect", "gain", "quiet", "room", "position", "direction", "report"], "步骤条：6 步加报告");
    run.eq(
        await page.eval(`${q(".step[data-step='connect']")}.getAttribute("aria-current")`),
        "step",
        "当前步有 aria-current",
    );
    await click('.stk__peek[data-pos="down"]');
    await page.waitFor(`${q(".stk")}?.dataset.step === "gain"`, { label: "点下面露出的卡翻到增益" });
    run.eq(await page.eval(`${q(".stk__peek[data-pos='up']")}?.dataset.step`), "connect", "上面露出上一步");
    await page.eval(
        `${FD}.dispatchEvent(new KeyboardEvent("keydown", { key: "PageDown", bubbles: true, cancelable: true }))`,
    );
    await page.waitFor(`${q(".stk")}?.dataset.step === "quiet"`, { label: "PageDown 翻到安静" });
    await page.eval(
        `${FD}.dispatchEvent(new KeyboardEvent("keydown", { key: "PageUp", bubbles: true, cancelable: true }))`,
    );
    await page.waitFor(`${q(".stk")}?.dataset.step === "gain"`, { label: "PageUp 回到增益" });
    await page.eval(`${q("#main")}.dispatchEvent(new WheelEvent("wheel", { deltaY: 120, bubbles: true }))`);
    await page.waitFor(`${q(".stk")}?.dataset.step === "quiet"`, { label: "滚轮向下翻一页" });
    await sleep(500);
    await page.eval(`${q("#main")}.dispatchEvent(new WheelEvent("wheel", { deltaY: -120, bubbles: true }))`);
    await page.waitFor(`${q(".stk")}?.dataset.step === "gain"`, { label: "滚轮向上翻一页" });
    await goStep("connect");
    await call(`mutate((s) => { s.micType = "headset"; })`);
    await page.waitFor(`!${q(".step[data-step='direction']")}`, { label: "耳机麦：步骤条没有方向步" });
    await call(`mutate((s) => { s.micType = "unknown"; })`);
    await page.waitFor(`!!${q(".step[data-step='direction']")}`, { label: "换回别的话筒，方向步回来" });

    // 接通观察在跑时离开接通页：取消那一轮；不挡按钮的琥珀诊断下直接点“开始增益测试”：先取消接通观察，再开增益
    await call(`mutate((s) => { s.connect.signalState = "very-low-noise"; })`);
    await page.waitFor(`${M}.sim.runningStep() === "connect"`, { label: "信号偏低：开一轮接通观察" });
    await goStep("gain");
    await page.waitFor(`${M}.sim.runningStep() === null`, { label: "离开接通页就取消那一轮" });
    run.eq(await stackStep(), "gain", "取消接通观察不把页面拽回接通页");
    // 别处（宿主测试的探针脚本）开的接通：界面不在接通页时也不去取消它
    await call(`startStep("connect")`);
    await sleep(300);
    run.eq(await page.eval(`${M}.sim.runningStep()`), "connect", "不是本页开的接通观察，界面不取消");
    await call(`sim.cancel()`);
    await goStep("connect");
    await page.waitFor(`${M}.sim.runningStep() === "connect"`, { label: "回到接通页又开一轮" });
    run.eq(await page.eval(`${q('[data-part="cta"]')}.disabled`), false, "信号偏低：开始增益测试可点");
    await click('[data-part="cta"]');
    await page.waitFor(`${M}.sim.runningStep() === "gain"`, { label: "先取消接通观察，再开增益" });
    run.check(
        (await page.eval(`${M}.calls.map((c) => c.name)`)).slice(-2).join(",") === "cancelStep,startStep",
        "开增益之前先取消接通观察",
    );
    await page.waitFor(`${FD}.getElementById("shell").dataset.remote === "1"`, { label: "增益进入远距离模式" });
    await click('[data-part="cancel"]');
    await page.waitFor(`${FD}.getElementById("shell").dataset.remote === "0"`, { label: "取消增益" });
    await call(`mutate((s) => { s.connect.signalState = "ok"; })`);

    // ---- ③ 测试中：远距离模式 ----
    await goStep("gain");
    await click('[data-part="cta"]');
    await page.waitFor(`${FD}.getElementById("shell").dataset.remote === "1"`, { label: "开始测试后进入远距离模式" });
    run.check(
        (await page.eval(`${M}.calls.map((c) => c.name + ":" + JSON.stringify(c.args))`)).includes(
            'startStep:["gain"]',
        ),
        "开始按钮走桥 startStep(gain)",
    );
    run.eq(await page.eval(`getComputedStyle(${q("#hdr")}).visibility`), "hidden", "远距离模式下外壳本体不可见");
    run.eq(await page.eval(`${q(".rm")}.dataset.rtone`), "violet", "进行中是紫色");
    // 均方根很低（−45）、真峰值很高（−9）：全宽条的液面按峰值画（刻度 −60..0 上约 85%），不跟均方根（约 25%）
    await call(`emitMeters({ levelDbfsL: -45, peakDbfsL: -9 })`);
    await page.waitFor(`parseFloat(${q(".rm__liq")}.style.getPropertyValue("--lv")) > 80`, {
        label: "全宽电平条的液面跟真峰值走",
    });
    run.check((await fontPx(".rm__cmd")) >= 48, "指令字号 ≥ 48 px（1.5 米可读）");
    run.eq(await page.eval(`!!${q(".rm__zone")}`), true, "增益测试时电平条上有目标区");
    run.eq(await text(".rm__num"), "8", "倒计时是总时长（来自快照）");
    await call(`sim.advance(3)`);
    await page.waitFor(`${q(".rm__num")}?.textContent === "5"`, { label: "倒计时随进度减少" });

    await page.eval(`(() => { window.__cmd = ${q(".rm__cmd")}; return true; })()`);
    await call(`sim.advance(1)`);
    await page.waitFor(`${q(".rm__num")}?.textContent === "4"`, { label: "倒计时继续减少" });
    run.eq(await page.eval(`window.__cmd === ${q(".rm__cmd")}`), true, "只改文字不重建节点（aria-live 不重复念）");

    await click('[data-part="cancel"]');
    await page.waitFor(`${FD}.getElementById("shell").dataset.remote === "0"`, { label: "取消后离开远距离模式" });
    run.check((await page.eval(`${M}.calls.map((c) => c.name)`)).includes("cancelStep"), "取消走桥 cancelStep");
    run.eq(await exists('[data-part="prep"]'), true, "取消后回到准备页");

    // 房间步：拍手槽位随模拟推进，判定来自快照；字号 ≥ 64 的“念的字”在位置步
    await goStep("room");
    await click('[data-part="cta"]');
    await page.waitFor(`${q(".rm")}?.dataset.step === "room"`, { label: "房间步进入远距离模式" });
    await call(`sim.advance(8)`);
    await page.waitFor(`${FD}.querySelectorAll(".rm__slots .slot").length === 8`, {
        label: "房间步有 8 个槽位（总数来自快照）",
    });
    run.eq(
        await page.eval(`[...${FD}.querySelectorAll(".rm__slots .slot")].map((s) => s.dataset.s)`),
        ["valid", "soft", "valid", "noisy", "wait", "wait", "wait", "wait"],
        "每一下拍手有自己的判定",
    );
    await click('[data-part="cancel"]');
    await page.waitFor(`${FD}.getElementById("shell").dataset.remote === "0"`, { label: "取消房间步" });

    // 手动分段：准备页的“开始”直接开始第一段；收够之前“结束”不可用并写明还差什么；够了按 Enter 结束；之后每段等“开始”
    await goStep("position");
    await click('[data-part="cta"]');
    await page.waitFor(`${q(".rm")}?.dataset.step === "position"`, { label: "位置步进入远距离模式" });
    run.eq(await exists('[data-part="start"]'), false, "第一段随准备页的“开始”直接开始，不用再点");
    run.eq(await page.eval(`${q('[data-part="end"]')}?.getAttribute("aria-disabled")`), "true", "收够之前“结束”不可用");
    run.eq(await focused(), "end", "焦点在“结束”上（键盘可操作）");
    run.check((await text('[data-part="need"]')).includes("2"), "写明还差几下合格的拍手（数字来自 needLeft）");
    run.eq(await page.eval(`${FD}.querySelectorAll(".rm__dot").length`), 0, "手动分段时轻拍是一段，不画遍数的点");
    run.check((await fontPx('[data-part="end"]')) >= 36, "“结束”的字号 ≥ 36 px（1.5 米外看得清）");
    await click('[data-part="end"]');
    await sleep(200);
    run.eq(await phaseCalls(), [], "不可用时点“结束”不走桥");
    await call(`sim.advance(4)`);
    await page.waitFor(`${q('[data-part="end"]')}?.getAttribute("aria-disabled") === "false"`, {
        label: "收够之后“结束”可用",
    });
    run.check((await text('[data-part="need"]')).includes("够了"), "收够之后写“够了”");
    run.eq(await focused(), "end", "只改属性不重建：焦点还在“结束”上");
    await pressEnter();
    await page.waitFor(`!!${q('[data-part="start"]')}`, { label: "结束后下一段等用户开始" });
    run.eq(await phaseCalls(), ["endPhase"], "按 Enter 结束走桥 endPhase");
    run.eq(await focused(), "start", "焦点移到“开始”");
    run.eq(await page.eval(`${q(".rm")}.dataset.struct`), "position:plosive:wait", "待开始的是爆破音字表");
    run.eq(await exists(".rm__num"), false, "待开始时不倒数");
    run.eq(await exists('[data-part="end"]'), false, "待开始时没有“结束”");
    run.check((await fontPx(".rm__words")) >= 64, "待开始时先给出念的字，字号 ≥ 64 px（1.5 米可读）");
    run.check((await fontPx('[data-part="start"]')) >= 36, "“开始”的字号 ≥ 36 px");
    await call(`sim.advance(30)`);
    await sleep(200);
    run.eq(await exists('[data-part="start"]'), true, "等多久都还在等：待开始没有超时，也不推进");
    await pressEnter();
    await page.waitFor(`${q(".rm")}?.dataset.struct === "position:plosive"`, { label: "按 Enter 开始字表" });
    run.eq(await phaseCalls(), ["endPhase", "startPhase"], "开始走桥 startPhase");
    run.eq(await text(".rm__num"), "20", "字表两遍合成一段：倒计时是这一段的上限");
    run.eq(await focused(), "end", "开始后焦点在“结束”上");
    run.check((await text('[data-part="need"]')).includes("6"), "字表写明还差几个字");
    // 字表可以提前结束：念够 6 个字就能结束，不用等满 20 秒
    await call(`sim.advance(7)`);
    await page.waitFor(`${q('[data-part="end"]')}?.getAttribute("aria-disabled") === "false"`, {
        label: "字表念够就能提前结束",
    });
    await click('[data-part="end"]');
    await page.waitFor(`${q(".rm")}?.dataset.struct === "position:sibilant:wait"`, { label: "提前结束后到齿音字表" });
    // 到上限照样自动结束：齿音字表不按“结束”，念满 20 秒自己结束
    await click('[data-part="start"]');
    await page.waitFor(`${q(".rm")}?.dataset.struct === "position:sibilant"`, { label: "开始齿音字表" });
    await call(`sim.advance(20)`);
    await page.waitFor(`${q(".rm")}?.dataset.struct === "position:natural:wait"`, { label: "到上限自动结束" });
    // 重来：退回待开始，不倒数，提示留到再点“开始”
    await call(`mutate((s) => { s.live.notice = "too-quiet"; s.live.noticeSec = 0; })`);
    await page.waitFor(`${q(".rm")}?.dataset.rtone === "red"`, { label: "声音太轻：整窗换成红色重来" });
    run.eq(await exists('[data-part="start"]'), true, "重来时有“开始”");
    run.eq(await exists(".rm__num"), false, "手动分段的重来不倒数");
    run.check(!(await text(".rm")).includes("秒后自动"), "不写“几秒后自动重新开始”");
    await click('[data-part="start"]');
    await page.waitFor(`${q(".rm")}?.dataset.rtone === "violet"`, { label: "再点开始，提示清掉" });
    await click('[data-part="cancel"]');
    await page.waitFor(`${FD}.getElementById("shell").dataset.remote === "0"`, { label: "取消位置步" });

    // 方向：四个方向各自开始、结束
    await goStep("direction");
    await click('[data-part="cta"]');
    await page.waitFor(`${q(".rm")}?.dataset.step === "direction"`, { label: "方向步进入远距离模式" });
    for (let d = 1; d <= 4; d++) {
        if (d > 1) {
            await page.waitFor(`${q(".rm")}?.dataset.struct === "direction:direction:wait"`, {
                label: `方向 ${d} 等用户开始`,
            });
            run.check((await text(".rm__aux")).includes(String(d)), `方向 ${d}：待开始时就写明是第几个方向`);
            await click('[data-part="start"]');
        }
        await page.waitFor(`${q(".rm")}?.dataset.struct === "direction:direction"`, { label: `方向 ${d} 开始` });
        await call(`sim.advance(2.5)`);
        await page.waitFor(`${q('[data-part="end"]')}?.getAttribute("aria-disabled") === "false"`, {
            label: `方向 ${d} 收够`,
        });
        await click('[data-part="end"]');
    }
    await page.waitFor(`${q(".rm")}?.dataset.struct === "direction:confirm:wait"`, { label: "四个方向之后是确认" });
    await click('[data-part="cancel"]');
    await page.waitFor(`${FD}.getElementById("shell").dataset.remote === "0"`, { label: "取消方向步" });

    // ---- ④ 成功屏与结果页 ----
    await goStep("gain");
    await click('[data-part="cta"]');
    await page.waitFor(`${q(".rm")}?.dataset.step === "gain"`, { label: "重新开始增益" });
    await call(`sim.advance(8)`);
    await page.waitFor(`${q(".rm")}?.dataset.rtone === "green"`, { label: "完成后先显示绿色成功屏" });
    await page.waitFor(`${FD}.getElementById("shell").dataset.remote === "0"`, {
        label: "成功屏之后回到主界面",
        timeout: 5000,
    });
    run.eq(await stackStep(), "gain", "完成后落在这一步的结果页");
    run.eq(await exists('[data-part="result"]'), true, "显示结果页");
    run.eq(await text(".rc__word"), "合适", "档位词来自快照里的档位（good → 合适）");
    run.check((await text(".rc__num b")).includes("8.4"), "读数来自快照");
    run.eq(await page.eval(`${FD}.querySelectorAll(".bands__seg").length`), 6, "增益区间条：6 段");
    run.check(
        (await text(".pn__lead")).includes("−12") || (await text(".pn__lead")).includes("-12"),
        "说明里的目标区数字来自 bands",
    );
    run.eq(
        await page.eval(
            `${FD}.querySelector(".stk").textContent.includes("null") || ${FD}.querySelector(".stk").textContent.includes("{")`,
        ),
        false,
        "没有 null 或占位符残留",
    );
    // 过期：改了增益，结果卡变灰
    await call(`reportChange("gain")`);
    await page.waitFor(`${q(".rc")}?.dataset.st === "stale"`, { label: "改了增益后结果卡变成过期" });

    // 全部做完，逐页看结果
    await call(`sim.complete(["gain", "quiet", "room", "position", "direction"])`);
    await goStep("quiet");
    run.eq(await page.eval(`${FD}.querySelectorAll(".rc").length`), 2, "安静：两张结果卡");
    await goStep("position");
    run.eq(await page.eval(`${FD}.querySelectorAll(".rc").length`), 4, "位置：四张结果卡");
    await goStep("direction");
    run.eq(await page.eval(`${FD}.querySelectorAll(".radar__lb").length`), 4, "方向：雷达四个标签");
    run.eq(await page.eval(`${FD}.querySelectorAll(".rd__pt").length`), 4, "方向：雷达四个点");
    run.eq(await page.eval(`${q(".rd__pt[data-max='1']")} !== null`), true, "方向：最响的点高亮");
    // 重测：显示准备页，开始才走桥
    await click(".pn__cta .sc-btn:not(.sc-btn--cta)");
    await page.waitFor(`!!${q('[data-part="prep"]')}`, { label: "点重测显示准备页" });
    await goStep("report");
    run.eq(await exists(`[data-part="report-page"]`), true, "报告页是整页（没有叠卡的上下露出）");

    // ---- ⑤ 左栏：结果标记与悬停提示 ----
    await goStep("quiet");
    const marks = await page.eval(`${FD}.querySelectorAll(".lt-bd, .lt-mk").length`);
    run.check(marks > 0, "测完后电平管上有结果标记");
    await page.eval(
        `(() => { const b = ${q(".lt-bd")}; b.dispatchEvent(new PointerEvent("pointerover", { bubbles: true })); })()`,
    );
    await page.waitFor(`!${q(".lt-tip")}.hidden`, { label: "悬停标记出现提示" });
    run.check((await text(".lt-tip")).length > 0, "提示里有读数");

    // ---- ⑤b 自动推进：各段首尾相接，没有“开始”“结束”（宿主测试的探针就是这样） ----
    await loadPage("?phases=auto");
    await call(`mutate((s) => { s.connect.signalState = "ok"; })`);
    await goStep("position");
    await click('[data-part="cta"]');
    await page.waitFor(`${q(".rm")}?.dataset.step === "position"`, { label: "自动推进：位置步进入远距离模式" });
    run.eq(
        (await exists('[data-part="end"]')) || (await exists('[data-part="start"]')),
        false,
        "自动推进：没有开始、结束",
    );
    run.eq(await page.eval(`${FD}.querySelectorAll(".rm__dot").length`), 3, "自动推进：轻拍三个点，个数来自快照");
    await call(`sim.advance(8)`);
    await page.waitFor(`${q(".rm")}?.dataset.struct === "position:plosive"`, { label: "自动推进：轻拍完接着念字表" });
    run.eq(await exists('[data-part="start"]'), false, "自动推进：段与段之间不等开始");
    await click('[data-part="cancel"]');
    await page.waitFor(`${FD}.getElementById("shell").dataset.remote === "0"`, { label: "自动推进：取消" });

    // ---- ⑥ 英文 ----
    await loadPage("?lang=en");
    run.eq(await page.eval(`${FD}.documentElement.lang`), "en", "英文开窗");
    await call(`mutate((s) => { s.connect.signalState = "ok"; })`);
    await call(`sim.complete(["gain", "quiet", "room", "position", "direction"])`);
    for (const id of ["gain", "quiet", "room", "position", "direction"]) {
        await goStep(id);
        const body = await page.eval(`${q(".stk__cur")}.textContent + ${q(".stk__track")}.textContent`);
        run.check(!/[{}]|null|undefined|\bflow\.|\bmeter\./.test(body), `英文 ${id}：没有占位符或键名残留`);
        run.check(!/[一-鿿]/.test(body), `英文 ${id}：没有汉字`);
    }
    run.eq(page.errors, [], "不应有页面异常或 console.error");
} finally {
    await browser.close();
    await server.close();
}
run.finish();
