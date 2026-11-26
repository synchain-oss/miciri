// SPDX-License-Identifier: GPL-3.0-or-later
//
// 左栏电平表弹道的纯逻辑冒烟（不要浏览器）：
//   ① 液面是峰值表：瞬时升到真峰值、慢落；峰值线保持后才回落；液面不会越过峰值线；
//   ② 均方根只是淡色内层：不影响液面和峰值线，且不高于液面；
//   ③ 人声式的读数序列（每次读取间的峰值忽高忽低）：液面最高处与峰值线一致，不再被均方根拉低一截；
//   ④ 远距离条的接线：液面只看峰值，源码里不再用均方根喂液面；
//   ⑤ 反例：把液面改回按均方根画，这里的断言会变红。
//
// 用法：node web-preview/tests/smoke-meter-core.mjs [仓库根]

import path from "node:path";
import { pathToFileURL } from "node:url";
import { createRun, repoRoot } from "./lib/harness.mjs";
import { readText } from "./lib/files.mjs";

const ROOT = repoRoot();
const run = createRun("电平表弹道");
const meter = await import(pathToFileURL(path.join(ROOT, "web/app/meter/meter-core.js")).href);
const { advance, rest, atRest, FLOOR, FALL_DB_S, PEAK_HOLD_MS, PEAK_FALL_DB_S } = meter;
const FRAME = 33; // 原生每 1/30 秒读一次

// ---- ① 液面是峰值表 ----
let s = advance(rest(), -4, FRAME);
run.eq([s.db, s.peakDb], [-4, -4], "第一次读到 −4 dBTP，液面和峰值线都在 −4");
s = advance(s, -30, FRAME);
const fell = -4 - (FALL_DB_S * FRAME) / 1000;
run.check(Math.abs(s.db - fell) < 1e-9, "峰值落下去之后液面按固定速度慢落，不是立刻掉到新值");
run.eq(s.peakDb, -4, "峰值线保持");
run.check(s.db <= s.peakDb, "液面不高于峰值线");
for (let ms = 0; ms <= PEAK_HOLD_MS; ms += FRAME) s = advance(s, -80, FRAME);
const afterHold = advance(s, -80, FRAME);
run.check(afterHold.peakDb < -4, "保持时间过后峰值线才开始回落");
run.check(
    Math.abs(afterHold.peakDb - Math.max(afterHold.db, s.peakDb - (PEAK_FALL_DB_S * FRAME) / 1000)) < 1e-9,
    "保持之后按 PEAK_FALL_DB_S 回落",
);
run.eq(advance(rest(), 12, FRAME).db, 6, "超过显示上限的读数夹在上限");
run.eq(advance(rest(), Number.NaN, FRAME).db, FLOOR, "非数值当作底");
run.check(atRest(rest()), "初始在底");

// ---- ② 均方根内层 ----
const withRms = advance(rest(), -6, FRAME, -20);
run.check(withRms.db === -6 && withRms.peakDb === -6 && withRms.rmsDb === -20, "均方根只给内层，不动液面和峰值线");
run.check(advance(rest(), -20, FRAME, -6).rmsDb <= -20, "内层不高于液面（均方根不会比峰值大）");
run.check(atRest(advance(rest(), -80, FRAME, -40)), "液面在底时内层也夹在底");
let tail = advance(rest(), -6, FRAME, -6);
tail = advance(tail, -80, FRAME, -80);
run.check(tail.rmsDb < -6 && tail.rmsDb > FLOOR, "内层同样慢落");

// ---- ③ 人声式的读数 ----
// 每个读取间隔的真峰值：音节里在 −9 到 −1.2 之间起伏，音节之间落到 −40；均方根比峰值低约 12 dB。
const syllable = [-9, -3, -1.2, -2.5, -6, -14, -40, -40, -40, -40, -40, -40];
let lv = rest();
let liquidMax = FLOOR;
let rmsMax = FLOOR;
for (let rep = 0; rep < 3; rep++)
    for (const peak of syllable) {
        lv = advance(lv, peak, FRAME, peak - 12);
        liquidMax = Math.max(liquidMax, lv.db);
        rmsMax = Math.max(rmsMax, lv.rmsDb);
    }
run.eq(liquidMax, -1.2, "液面最高处就是增益步测到的 −1.2，不再低一截");
run.check(rmsMax <= liquidMax - 10, "均方根内层比液面低一截（正常的峰均比），不拿它当液面");

// ---- ④ 远距离条与左栏同源 ----
const remote = readText(ROOT, "web/app/flow/remote-ui.js");
run.check(!/levelDbfs/.test(remote), "远距离条不读 levelDbfs（均方根），液面只看峰值");
run.check(/advance\(lvl, target\.peak, dt\)/.test(remote), "远距离条按峰值推进弹道");

// ---- ⑤ 反例 ----
// 若液面按均方根画（旧做法），同一串读数里液面最高处会比真峰值低一大截，③ 的断言就会红。
let old = rest();
let oldMax = FLOOR;
for (const peak of syllable) {
    old = advance(old, peak - 12, FRAME);
    oldMax = Math.max(oldMax, old.db);
}
run.check(oldMax < -1.2 - 10, "反例：按均方根喂液面，最高处低于真峰值一大截");

run.finish();
