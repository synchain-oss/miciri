// SPDX-License-Identifier: GPL-3.0-or-later
//
// 外壳页面冒烟（无头浏览器）：预览页用假桥装载真源外壳，端到端核对
//   ① 加载：首帧与首个快照信号、设计盒真实尺寸、四个区域的位置、没有页面错误、没有外网请求；
//   ② 语言：切到 EN 后文字、html lang、按钮按下态都变，桥收到 setLang，再切回；
//   ③ 缩放：下拉 7 档、放不下的置灰；选档立刻缩放并弹确认窗；Esc 回退并把焦点还给缩放钮；
//      确认态里再选档重置倒计时、回退目标不变；保存提交且不再回退；确认期间页脚仍点得到；
//   ④ 关于窗与可访问名：版本钮开关、焦点回收、所有按钮有名字、对话框有 aria-modal 与标题。
// 10 秒超时的逐秒行为由 smoke-scale-ui.mjs 用假定时器覆盖，这里不等真实 10 秒。
// 没有浏览器时整套跳过（退出码 2），不是通过。
//
// 用法：node web-preview/tests/smoke-shell-page.mjs [仓库根]

import path from "node:path";
import { pathToFileURL } from "node:url";
import { createRun, repoRoot } from "./lib/harness.mjs";
import { findBrowser, launchBrowser } from "./lib/browser.mjs";
import { startStaticServer } from "./lib/static-server.mjs";

const ROOT = repoRoot();
const run = createRun("外壳（页面）");
if (!findBrowser()) run.skip("没有 Chromium 系浏览器；可用环境变量 MICIRI_BROWSER 指定");

const at = (rel) => import(pathToFileURL(path.join(ROOT, rel)).href);
const { DESIGN } = await at("web/shared/design-box.js");
const strings = (await at("web/app/shell/shell-strings.js")).default;
const common = (await at("web/shared/strings-common.js")).default;
const T = { zh: { ...common.zh, ...strings.zh }, en: { ...common.en, ...strings.en } };

const FD = "document.getElementById('frame').contentDocument";
const MOCK = "window.__MICIRI_MOCK__";
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const server = await startStaticServer(ROOT);
const browser = await launchBrowser({ width: 1400, height: 1000 });
try {
    const page = await browser.newPage();
    // 无头浏览器默认报告 800×600 的屏幕，会让大档位全被置灰；固定成 1400×1000，结果不随机器变
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
    const text = (sel) => page.eval(`${q(sel)}?.textContent ?? null`);
    const click = (sel) => page.eval(`${q(sel)}.click()`);
    const key = (k, sel = `${FD}.activeElement`) =>
        page.eval(
            `(${sel} || ${FD}.body).dispatchEvent(new KeyboardEvent("keydown", { key: ${JSON.stringify(k)}, bubbles: true, cancelable: true }))`,
        );
    const calls = () => page.eval(`${MOCK}.calls.map((c) => c.name + ":" + JSON.stringify(c.args))`);
    const frameWidth = () => page.eval(`document.getElementById("frame").style.width`);
    const zoom = () => page.eval(`Number(${FD}.getElementById("shell").style.zoom)`);
    const active = () => page.eval(`${FD}.activeElement?.id || ${FD}.activeElement?.dataset.part || ""`);
    const shown = (part) =>
        page.eval(`!!${q(`[data-part="${part}"]`)} && !${q(`[data-part="${part}"]`)}.closest("[hidden]")`);

    // ---- ① 加载 ----
    await page.goto(`${server.url}/web-preview/index.html`);
    await page.waitFor(`${FD}?.documentElement.dataset.ready === "1" || ${FD}?.documentElement.dataset.bootError`, {
        label: "首个快照应用后 data-ready 应为 1",
    });
    run.eq(await page.eval(`${FD}.documentElement.dataset.bootError ?? null`), null, "不应有启动错误");
    run.eq(await page.eval(`${FD}.documentElement.dataset.firstFrame`), "1", "应有首帧信号");
    const rect = (sel) =>
        page.eval(
            `(() => { const r = ${q(sel)}.getBoundingClientRect(); return [r.x, r.y, r.width, r.height].map(Math.round); })()`,
        );
    const [sx, sy, sw, sh] = await rect("#shell");
    run.eq([sw, sh], [DESIGN.shell.w, DESIGN.shell.h], "100% 缩放时外壳应为设计盒 1120×700");
    // 外壳有 1 px 边线，区域按内框定位，所以整体向内缩 1 px
    run.eq(await rect("#hdr"), [sx + 1, sy + 1, 1118, 52], "页眉应在顶部、高 52");
    run.eq(await rect("#lcol"), [sx + 15, sy + 59, 260, 598], "左栏应在 left 14、top 58、宽 260（内框坐标）");
    run.eq(await rect("#main"), [sx + 287, sy + 59, 818, 598], "主区应在 left 286、right 14（内框坐标，宽 818）");
    run.eq((await rect("#ftr"))[3], 28, "页脚高 28");
    run.eq(await zoom(), 1, "100% 时 zoom 应为 1");
    run.eq(await text('[data-part="footer"] .ftr__hint'), T.zh["shell.hint"], "默认应显示快照里的语言（zh）");
    run.eq(await text('[data-part="version"]'), "Synchain 0.1.0", "页脚应显示版本");
    run.eq(await text("#scale-btn"), "1×", "缩放钮应显示当前档位");
    for (const part of ["header", "steps", "left", "main", "footer"]) {
        run.eq(await page.eval(`!!${q(`[data-part="${part}"]`)}`), true, `应有挂载点 ${part}`);
    }

    // ---- ② 语言 ----
    await click('[data-lang="en"]');
    await page.waitFor(`${FD}.documentElement.lang === "en"`, { label: "切到 EN 后 html lang 应为 en" });
    run.eq(await text('[data-part="footer"] .ftr__hint'), T.en["shell.hint"], "EN 下页脚文字应换成英文");
    run.eq(await page.eval(`${q('[data-lang="en"]')}.getAttribute("aria-pressed")`), "true", "EN 钮应为按下态");
    run.eq(await page.eval(`${q('[data-lang="zh"]')}.getAttribute("aria-pressed")`), "false", "中文钮应为未按下");
    run.eq(
        await page.eval(`${q("#scale-btn")}.getAttribute("aria-label")`),
        T.en["shell.scale.aria"],
        "aria-label 也应换成英文",
    );
    run.check((await calls()).includes('setLang:["en"]'), "桥应收到 setLang en");
    await click('[data-lang="zh"]');
    await page.waitFor(`${FD}.documentElement.lang === "zh"`, { label: "切回中文" });
    run.eq(await text('[data-part="footer"] .ftr__hint'), T.zh["shell.hint"], "切回中文后文字应恢复");

    // ---- ③ 缩放 ----
    const screenSize = await page.eval(`[${FD}.defaultView.screen.availWidth, ${FD}.defaultView.screen.availHeight]`);
    const wantOff = DESIGN.shell.presets.filter(
        (f) =>
            DESIGN.shell.w * f + DESIGN.hostFrame.w > screenSize[0] ||
            DESIGN.shell.h * f + DESIGN.hostFrame.h > screenSize[1],
    );
    run.eq(wantOff, [1.25, 1.5, 2], "1400×1000 的屏幕上 1.25 及以上放不下");
    await click("#scale-btn");
    run.eq(
        await page.eval(`${q("#scale-btn")}.getAttribute("aria-expanded")`),
        "true",
        "展开后 aria-expanded 应为 true",
    );
    run.eq(await page.eval(`${q('[data-part="scale-menu"]')}.getAttribute("role")`), "listbox", "菜单应是 listbox");
    run.eq(await page.eval(`${FD}.querySelectorAll('[data-part="scale-menu"] [role=option]').length`), 7, "应有 7 档");
    run.eq(
        await page.eval(
            `[...${FD}.querySelectorAll('[data-part="scale-menu"] [aria-disabled=true]')].map((e) => Number(e.dataset.id))`,
        ),
        wantOff,
        "放不下当前屏幕的档位应置灰",
    );
    run.eq(
        await page.eval(`${q('[data-part="scale-menu"] [aria-selected=true]')}.dataset.id`),
        "1",
        "当前档应带选中态",
    );
    run.check(
        await page.eval(`${FD}.activeElement?.getAttribute("aria-selected") === "true"`),
        "展开后焦点应落在当前档",
    );
    await key("Escape");
    run.eq(await page.eval(`!!${q('[data-part="scale-menu"]')}`), false, "Esc 应关闭菜单");
    run.eq(await active(), "scale-btn", "Esc 关闭菜单后焦点应回到缩放钮");
    run.eq(await calls().then((c) => c.filter((x) => x.startsWith("setUiScale")).length), 0, "只开关菜单不应改缩放");

    // 选 0.8：立刻缩放并弹确认窗
    await click("#scale-btn");
    await click('[data-part="scale-menu"] [data-id="0.8"]');
    await page.waitFor(
        `!!${q('[data-part="scale-confirm"]')} && !${q('[data-part="scale-confirm"]')}.closest("[hidden]")`,
        { label: "确认窗应出现" },
    );
    run.eq(await frameWidth(), "896px", "窗口应立刻缩到 0.8（896 px 宽）");
    await page.waitFor(`Math.abs(Number(${FD}.getElementById("shell").style.zoom) - 0.8) < 0.01`, {
        label: "页面倍率应跟到 0.8",
    });
    run.check((await text('[data-part="scale-body"]')).includes("10"), "确认窗应显示 10 秒倒计时");
    run.eq(
        await page.eval(`${q('[data-part="scale-confirm"]')}.getAttribute("aria-modal")`),
        "true",
        "确认窗应是 aria-modal 对话框",
    );
    run.eq(await page.eval(`${FD}.getElementById("shell").dataset.keepFtr`), "1", "确认期间页脚应抬到遮罩之上");
    run.eq(
        await page.eval(`["#hdr", "#lcol", "#main", "#ftr"].map((s) => ${FD}.querySelector(s).hasAttribute("inert"))`),
        [true, true, true, false],
        "背景应 inert，页脚除外",
    );
    run.eq(await active(), "confirm-keep", "焦点应进入确认窗");
    run.check(
        await page.eval(`(() => { const d = ${FD}; const b = d.getElementById("scale-btn").getBoundingClientRect();
            const hit = d.elementFromPoint(b.x + b.width / 2, b.y + b.height / 2); return hit === d.getElementById("scale-btn") || d.getElementById("scale-btn").contains(hit); })()`),
        "确认期间缩放钮不应被遮罩盖住",
    );

    // Esc 回退到 1，焦点回缩放钮
    await key("Escape");
    await page.waitFor(`${q('[data-part="scale-confirm"]')}.closest("[hidden]") !== null`, { label: "确认窗应关闭" });
    await page.waitFor(`document.getElementById("frame").style.width === "1120px"`, { label: "Esc 应回到 100%" });
    run.eq(await active(), "scale-btn", "关闭确认窗后焦点应回到缩放钮");
    run.eq(await page.eval(`${FD}.getElementById("shell").dataset.keepFtr`), "0", "确认结束页脚应放回原层");
    run.eq(await page.eval(`${FD}.querySelectorAll("[inert]").length`), 0, "确认结束后不应残留 inert");
    run.eq(
        (await calls()).filter((x) => x.startsWith("setUiScale")),
        ["setUiScale:[0.8]", "setUiScale:[1]"],
        "桥应依次收到 0.8 与回退的 1",
    );

    // 确认态里再选档：倒计时重置，回退目标仍是 1；取消回到 1
    await click("#scale-btn");
    await click('[data-part="scale-menu"] [data-id="0.65"]');
    await page.waitFor(`${q('[data-part="scale-confirm"]')}?.dataset.state === "countdown-10"`, {
        label: "倒计时从 10 起",
    });
    await sleep(2300);
    run.check(
        Number((await page.eval(`${q('[data-part="scale-confirm"]')}.dataset.state`)).split("-")[1]) <= 8,
        "过 2 秒后倒计时应已走动",
    );
    await click("#scale-btn");
    await click('[data-part="scale-menu"] [data-id="0.5"]');
    run.eq(
        await page.eval(`${q('[data-part="scale-confirm"]')}.dataset.state`),
        "countdown-10",
        "确认态里再选档应把倒计时重置为 10",
    );
    run.eq(await frameWidth(), "560px", "再选的档位应立刻生效");
    await click('[data-part="confirm-cancel"]');
    await page.waitFor(`document.getElementById("frame").style.width === "1120px"`, {
        label: "取消应回到最初的 100%，而不是中间的 0.65",
    });
    run.eq(
        (await calls()).filter((x) => x.startsWith("setUiScale")).slice(-3),
        ["setUiScale:[0.65]", "setUiScale:[0.5]", "setUiScale:[1]"],
        "桥调用顺序",
    );

    // 保存：提交且之后不再回退
    await click("#scale-btn");
    await click('[data-part="scale-menu"] [data-id="0.8"]');
    await page.waitFor(
        `!!${q('[data-part="scale-confirm"]')} && !${q('[data-part="scale-confirm"]')}.closest("[hidden]")`,
        { label: "确认窗应出现" },
    );
    await click('[data-part="confirm-keep"]');
    run.check((await calls()).includes("commitUiScale:[]"), "保存应调 commitUiScale");
    await sleep(1300);
    run.eq(await frameWidth(), "896px", "保存后档位应保持");
    run.eq(await text("#scale-btn"), "0.8×", "缩放钮应显示新档位");
    run.eq(await active(), "scale-btn", "保存后焦点应回到缩放钮");

    // ---- ④ 关于窗与可访问名 ----
    await click('[data-part="version"]');
    run.eq(await shown("about"), true, "点版本钮应打开关于窗");
    run.check((await text('[data-part="about"]')).includes("contact@synchain.ca"), "关于窗应写对外邮箱");
    run.check(
        await page.eval(
            `(() => { const p = ${q('[data-part="about"]')}; const t = ${FD}.getElementById(p.getAttribute("aria-labelledby")); return !!t && t.textContent.length > 0 && p.getAttribute("aria-modal") === "true"; })()`,
        ),
        "对话框应有 aria-modal 与有内容的标题",
    );
    await key("Escape");
    run.eq(await page.eval(`${q('[data-part="about"]')}.closest("[hidden]") !== null`), true, "Esc 应关闭关于窗");
    run.eq(await page.eval(`${FD}.activeElement?.dataset.part`), "version", "关闭关于窗后焦点应回到版本钮");
    run.eq(
        await page.eval(
            `[...${FD}.querySelectorAll("button")].filter((b) => !(b.getAttribute("aria-label") || b.textContent).trim()).length`,
        ),
        0,
        "所有按钮都应有可访问名",
    );

    // ---- 全程 ----
    const external = requests
        .map((r) => (typeof r === "string" ? r : r.url))
        .filter((u) => !/^(http:\/\/127\.0\.0\.1|data:|about:)/.test(u));
    run.eq(external, [], "不应有任何外网请求");
    // 字体文件由字体那张卡提供，缺失时回退系统字体，这里只放过 woff2 的 404
    const errors = page.errors.filter((e) => !/\.woff2/.test(e));
    run.eq(errors, [], "不应有页面异常或 console.error");
} finally {
    await browser.close();
    await server.close();
}
run.finish();
