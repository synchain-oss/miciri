// SPDX-License-Identifier: GPL-3.0-or-later
//
// 设计盒：design-box.js 是唯一真源（1120×700，缩放 0.5 到 2 共 7 档），CSS 里的 --design-w、--design-h
// 必须与它逐值相等；适配倍率与「放不下屏幕」的判据按算术核对。真实布局尺寸由页面冒烟核对。
//
// 用法：node web-preview/tests/smoke-design-box.mjs [仓库根]

import path from "node:path";
import { pathToFileURL } from "node:url";
import { createRun, repoRoot } from "./lib/harness.mjs";
import { readText } from "./lib/files.mjs";

const ROOT = repoRoot();
const run = createRun("设计盒");
const at = (rel) => import(pathToFileURL(path.join(ROOT, rel)).href);
const { DESIGN } = await at("web/shared/design-box.js");
const { fitFactor } = await at("web/shared/shell-fit.js");
const { scaleOverflows, createScaleUi } = await at("web/shared/scale-ui.js");

const box = DESIGN.shell;
run.eq([box.w, box.h], [1120, 700], "设计盒应为 1120×700");
run.eq(box.presets, [0.5, 0.65, 0.8, 1, 1.25, 1.5, 2], "缩放档位应为 0.5 到 2 共 7 档");
run.check(
    box.presets.every((f, i, a) => i === 0 || f > a[i - 1]),
    "档位应严格递增",
);

// CSS 里的设计盒尺寸与真源逐值相等
const css = readText(ROOT, "web/app/shell/shell-tokens.css");
const cssNum = (name) => Number((new RegExp(`${name}:\\s*(\\d+)px`).exec(css) || [])[1]);
run.eq(
    [cssNum("--design-w"), cssNum("--design-h")],
    [box.w, box.h],
    "shell-tokens.css 的 --design-w、--design-h 应与 design-box.js 一致",
);

// 适配倍率：短边贴边、量化向下、不放大超过视口
run.eq(fitFactor(1120, 700, box.w, box.h), 1, "视口恰为设计盒时倍率为 1");
run.eq(fitFactor(560, 350, box.w, box.h), 0.5, "视口为设计盒一半时倍率为 0.5");
run.eq(fitFactor(2240, 700, box.w, box.h), 1, "宽视口时高度贴边");
run.eq(fitFactor(1120, 350, box.w, box.h), 0.5, "矮视口时高度贴边");
run.check(fitFactor(1119, 700, box.w, box.h) < 1, "视口差 1 px 也要缩小，不能溢出");

// 放不下当前屏幕的档位：窗口尺寸 = 设计盒 × 档位 + 宿主框
const fr = DESIGN.hostFrame;
const fits = (f, sw, sh) => !scaleOverflows("shell", f, { availWidth: sw, availHeight: sh });
run.check(fits(1, box.w + fr.w, box.h + fr.h), "屏幕恰好放得下时不置灰");
run.check(!fits(1, box.w + fr.w - 1, box.h + fr.h), "宽差 1 px 应置灰");
run.check(!fits(1, box.w + fr.w, box.h + fr.h - 1), "高差 1 px 应置灰");
run.check(fits(2, 99999, 99999) && !fits(2, 1920, 1080), "2 倍在小屏上置灰、在大屏上可选");
run.check(!scaleOverflows("shell", 2, null), "屏幕尺寸未知时不判溢出");
let threw = false;
try {
    createScaleUi({ role: "nope", bridge: {} });
} catch {
    threw = true;
}
run.check(threw, "未知角色应当抛错");

run.finish();
