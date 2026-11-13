// SPDX-License-Identifier: GPL-3.0-or-later
//
// 应用内指南的纯逻辑冒烟（不要浏览器）：
//   ① 七个步骤的次序，每步都有中英词条，词条里不写数字（占位符除外），不碰文案禁区；
//   ② 亮洞：目标矩形按外壳缩放换回设计坐标，贴边时被夹在设计盒内；
//   ③ 说明框：优先放右、再左、下、上，取第一个不压住亮洞的；整框留在设计盒内；
//   ④ 翻页：越过最后一页是结束，越过第一页停在第一页；找不到目标的步骤被跳过；
//   ⑤ 反例：检查要能变红。
//
// 用法：node web-preview/tests/smoke-guide-core.mjs [仓库根]

import path from "node:path";
import { pathToFileURL } from "node:url";
import { createRun, repoRoot } from "./lib/harness.mjs";

const ROOT = repoRoot();
const run = createRun("应用内指南逻辑");
const at = (rel) => import(pathToFileURL(path.join(ROOT, rel)).href);
const { STEPS, GEOM, holeFromRect, placeCallout, presentSteps, turn } = await at("web/app/guide/guide-core.js");
const { DESIGN } = await at("web/shared/design-box.js");
const dict = (await at("web/app/guide/guide-strings.js")).default;
const { w: W, h: H } = DESIGN.shell;

// ---- ① 步骤与词条 ----
run.eq(
    STEPS.map((s) => s.id),
    ["input", "meter", "mic", "main", "cta", "scale", "help"],
    "七个步骤，次序固定",
);
run.check(
    STEPS.every((s) => s.select.length > 0 && s.select.every((q) => typeof q === "string")),
    "每步都有候选选择器",
);
for (const lang of ["zh", "en"]) {
    for (const s of STEPS) {
        run.check(!!dict[lang][`guide.${s.id}.t`] && !!dict[lang][`guide.${s.id}.b`], `${lang}：${s.id} 有标题和说明`);
    }
    for (const k of ["hint", "step", "prev", "next", "skip", "done"]) {
        run.check(!!dict[lang][`guide.${k}`], `${lang}：guide.${k} 存在`);
    }
}
run.eq(Object.keys(dict.zh).sort(), Object.keys(dict.en).sort(), "中英词条的键一致");

// 词条里不写数字：目标区、确认时限这类量由界面和快照负责；占位符 {n} {total} 不算
const digitsIn = (d) =>
    Object.entries(d)
        .filter(([, v]) => /\d/.test(String(v).replace(/\{\w+\}/g, "")))
        .map(([k]) => k);
run.eq(digitsIn(dict.zh), [], "zh 词条里没有数字");
run.eq(digitsIn(dict.en), [], "en 词条里没有数字");

// 文案禁区：不写厘米数、零延迟、支持所有 DAW、绝对
const BANNED = /零延迟|支持所有|绝对|厘米|\bcm\b|zero.?latency|all DAWs|absolute/i;
for (const lang of ["zh", "en"]) {
    run.eq(
        Object.entries(dict[lang])
            .filter(([, v]) => BANNED.test(v))
            .map(([k]) => k),
        [],
        `${lang} 词条里没有文案禁区的词`,
    );
}

// ---- ② 亮洞 ----
const shellRect = { left: 100, top: 50 };
const at1 = holeFromRect({ left: 200, top: 150, width: 100, height: 40 }, shellRect, 1);
run.eq(at1, { x: 90, y: 90, w: 120, h: 60 }, "缩放 1：外扩一圈 pad");
// 设计坐标 (300, 200)、大小 80×30，在缩放 0.5 下屏幕位置是 (100+150, 50+100)、大小 40×15
const half = holeFromRect({ left: 250, top: 150, width: 40, height: 15 }, shellRect, 0.5);
run.eq(half, { x: 290, y: 190, w: 100, h: 50 }, "缩放 0.5：屏幕矩形换回设计坐标");
const edge = holeFromRect({ left: 100, top: 50, width: W, height: H }, shellRect, 1);
run.check(edge.x === 0 && edge.y === 0 && edge.w <= W && edge.h <= H, "贴边：亮洞被夹在设计盒内");
run.eq(
    holeFromRect({ left: 200, top: 150, width: 100, height: 40 }, shellRect, 0),
    at1,
    "缩放不是正数时按 1 算（首帧或隐藏页不出 NaN）",
);

// ---- ③ 说明框 ----
const CH = 200;
const inside = (p) =>
    p.x >= GEOM.edge && p.y >= GEOM.edge && p.x + GEOM.callW <= W - GEOM.edge && p.y + CH <= H - GEOM.edge;
const overlaps = (p, h) => !(p.x + GEOM.callW <= h.x || p.x >= h.x + h.w || p.y + CH <= h.y || p.y >= h.y + h.h);
const mid = { x: 100, y: 300, w: 200, h: 100 };
const pMid = placeCallout(mid, CH);
run.check(pMid.side === "right" && !overlaps(pMid, mid) && inside(pMid), "亮洞在左边：说明框放右边，不压住亮洞");
const rightEdge = { x: 900, y: 300, w: 200, h: 100 };
const pRight = placeCallout(rightEdge, CH);
run.check(pRight.side === "left" && !overlaps(pRight, rightEdge) && inside(pRight), "亮洞贴右边：说明框换到左边");
const topBar = { x: 20, y: 20, w: W - 40, h: 60 };
const pTop = placeCallout(topBar, CH);
run.check(pTop.side === "bottom" && !overlaps(pTop, topBar) && inside(pTop), "亮洞是横条：说明框放下面");
const bottomBar = { x: 20, y: H - 80, w: W - 40, h: 60 };
const pBottom = placeCallout(bottomBar, CH);
run.check(pBottom.side === "top" && !overlaps(pBottom, bottomBar) && inside(pBottom), "亮洞是底部横条：说明框放上面");
const whole = { x: 0, y: 0, w: W, h: H };
const pWhole = placeCallout(whole, CH);
run.check(inside(pWhole), "亮洞盖满整窗：压不开也留在设计盒内");
const pNone = placeCallout(null, CH);
run.check(pNone.side === null && pNone.x === (W - GEOM.callW) / 2 && pNone.y === (H - CH) / 2, "没有亮洞：居中");

// ---- ④ 翻页与跳过 ----
run.eq(turn(2, 1, 7), 3, "下一页");
run.eq(turn(2, -1, 7), 1, "上一页");
run.eq(turn(6, 1, 7), null, "越过最后一页是结束");
run.eq(turn(0, -1, 7), 0, "越过第一页停在第一页");
run.eq(turn(0, 1, 1), null, "只有一页时下一页就是结束");
const noMic = presentSteps((s) => s.id !== "mic");
run.eq(
    noMic.map((s) => s.id),
    ["input", "meter", "main", "cta", "scale", "help"],
    "找不到目标的步骤被跳过，其余保持次序",
);
run.eq(
    presentSteps(() => false),
    [],
    "一个目标都没有：空表",
);

// ---- ⑤ 反例 ----
run.check(digitsIn({ a: "10 秒内没确认就退回" }).length > 0, "反例：词条里夹数字应当被查出");
run.check(BANNED.test("零延迟") && BANNED.test("works in all DAWs"), "反例：文案禁区的词应当被查出");
run.check(
    JSON.stringify(holeFromRect({ left: 250, top: 150, width: 40, height: 15 }, shellRect, 1)) !== JSON.stringify(half),
    "反例：忽略缩放会得到另一个亮洞，上面的缩放 0.5 断言会变红",
);
run.check(
    overlaps({ x: W - GEOM.callW - GEOM.edge, y: 250 }, rightEdge),
    "反例：亮洞贴右边时，放在右边的说明框确实会压住亮洞，选边的断言会变红",
);

run.finish();
