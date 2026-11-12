// SPDX-License-Identifier: GPL-3.0-or-later
//
// 详情面板的纯逻辑冒烟（不要浏览器）：
//   ① 每张卡点开后的解释、表格、高亮行、说明；数字都来自快照，不写出 NaN 或 undefined；
//   ② 过期、要再测、没测过时的按钮；方向的表格与雷达；
//   ③ 缺 bands、口水音 0 次、齿音高频段不可测、T20 作废这几种缺口的写法；
//   ④ 不标“首次测量”（快照分不开“新测的”和“没动过的”）；方向数组异常时不画表；
//   ⑤ 反例：检查要能变红。
//
// 用法：node web-preview/tests/smoke-detail-core.mjs [仓库根]

import path from "node:path";
import { pathToFileURL } from "node:url";
import { createRun, repoRoot } from "./lib/harness.mjs";

const ROOT = repoRoot();
const run = createRun("详情面板逻辑");
const at = (rel) => import(pathToFileURL(path.join(ROOT, rel)).href);
const { detailOf } = await at("web/app/detail/detail-core.js");
const { cardsOf, CARD_ORDER } = await at("web/app/report/report-core.js");
const { SCENES, measured, staled, withSection } = await at("web-preview/mock/report-fixtures.js");
const { resultsFor } = await at("web-preview/mock/results.js");
const dict = (await at("web/app/detail/detail-strings.js")).default;
const flowDict = (await at("web/app/flow/flow-strings.js")).default;

const withDir = (s) => {
    const o = structuredClone(s);
    Object.assign(o.results, resultsFor("direction"));
    o.steps.direction.status = "done";
    return o;
};
const base = withDir(SCENES.ready());
const keyOk = (k) => k in dict.zh || k in flowDict.zh || k.startsWith("flow.");
const cellText = (c) => (c ? `${c.text}${c.unit || ""}` : "");
const allText = (d) =>
    JSON.stringify([d.expl, d.rows.map((r) => [r.label, cellText(r.abs), cellText(r.rel)]), d.notes]);

// ---- ① 每张卡 ----
for (const id of CARD_ORDER) {
    const s = id === "dir" ? base : measured();
    const d = detailOf(id, s);
    run.check(keyOk(d.expl.key), `${id}：解释的词条键 ${d.expl.key} 存在`);
    run.check(!/NaN|undefined|null/.test(allText(d)), `${id}：解释、表格与说明里没有 NaN 或 undefined`);
    for (const k of [...d.notes.map((n) => n.key), ...d.rows.map((r) => r.label.key).filter(Boolean)])
        run.check(keyOk(k), `${id}：词条键 ${k} 存在`);
    if (d.rows.length)
        run.check(
            d.rows.some((r) => r.hl),
            `${id}：表格里有高亮行`,
        );
    run.check(
        d.cols === undefined || d.rows.every((r) => !r.rel || d.cols.length > 1),
        `${id}：只有一列数值时不给相对值`,
    );
}
{
    const lv = detailOf("level", measured());
    run.eq(
        [lv.rows[0].label.key, lv.rows[0].hl, lv.cols.length],
        ["detail.r.tp", true, 2],
        "增益：第一行是真峰值并高亮，两列数值",
    );
    run.check(lv.rows[0].rel && /^[+−]/.test(lv.rows[0].rel.text), "增益：相对目标中心带正负号");
    run.eq(detailOf("noise", measured()).rows[0].label.key, "detail.r.nA", "底噪：A 计权的一行在最前并高亮");
}

// ---- ② 按钮、方向 ----
{
    const st = detailOf("sib", staled(SCENES.ready(), "gain"));
    run.eq(
        [st.cta && st.cta.key, st.cta && st.cta.step, st.cta && st.cta.prep],
        ["detail.retest", "position", false],
        "过期的齿音卡：按钮重测它所属的位置步，不需要先去准备页",
    );
    const fresh = detailOf("level", measured());
    run.eq(fresh.cta, null, "有效的卡没有按钮");
    const lowc = detailOf("noise", SCENES.lowConfidence());
    run.eq(lowc.cta && lowc.cta.key, "detail.retest", "要再测的卡也给重测按钮");
    run.check(
        lowc.notes.some((n) => n.key.startsWith("flow.cf.why.")),
        "要再测的卡把原因写在说明里",
    );
    const none = detailOf("dir", measured());
    run.eq(
        [none.expl.key, none.cta && none.cta.key, none.cta && none.cta.prep, none.rows.length],
        ["detail.dir.none", "detail.go", true, 0],
        "方向没测：一句说明加“去测”，去的是准备页，不直接开始",
    );
    const miss = detailOf("room", buildMissingRoom());
    run.eq([miss.expl.key, miss.cta.key], ["detail.none", "detail.go"], "没测过的卡：写“还没测”并给“去测”");
    const dir = detailOf("dir", base);
    run.eq(dir.rows.length, 4, "方向：四行");
    run.eq(dir.rows.filter((r) => r.hl).length, 1, "方向：最响的一行高亮");
    run.check(dir.radar && dir.radar.rel.length === 4, "方向：有四点雷达");
    run.eq(dir.rows[0].label.key, "flow.dir.cur", "方向：第一行是当前方向");
}
function buildMissingRoom() {
    const s = measured();
    delete s.results.reverb;
    return s;
}

// ---- ③ 缺口 ----
{
    const nb = measured();
    delete nb.bands;
    const d = detailOf("level", nb);
    run.eq([d.expl.key, d.rows[0].rel], ["detail.level.e0", null], "缺 bands：增益不写目标区，也没有相对值");
    run.check(!detailOf("noise", nb).notes.some((n) => n.key === "detail.n.noise"), "缺 bands：底噪不写档位边界");
    const z = withSection(measured(), "click", { count: 0, perMinute: 0, loudestRelDb: 0, loudestDbfs: 0 });
    const m = detailOf("mouth", z);
    run.eq(
        [m.expl.key, m.rows.some((r) => r.label.key === "detail.r.loud")],
        ["detail.mouth.0", false],
        "口水音 0 次：不写最响一次",
    );
    const hit = detailOf(
        "mouth",
        withSection(measured(), "click", { count: 3, perMinute: 6, loudestRelDb: -24, loudestDbfs: -32.4 }),
    );
    run.eq(
        hit.rows.find((r) => r.label.key === "detail.r.loud").label.vars.f,
        "2",
        "口水音：高通频率取常量，不写在文案里",
    );
    run.check(
        hit.notes.some((n) => n.key === "detail.n.ungraded"),
        "口水音不分档：写明只给绝对电平",
    );
    const narrow = detailOf(
        "sib",
        withSection(measured(), "sibilance", { highBandDisabled: true, effectiveBandwidthHz: 8000 }),
    );
    run.eq(
        [narrow.expl.key, narrow.rows[1].abs.text],
        ["detail.sib.n", "—"],
        "齿音高频段不可测：写带宽原因，那一格是“—”",
    );
    const gated = detailOf(
        "room",
        withSection(measured(), "reverb", { gated: true, reliability: "retest", retestReason: "none", tone: "none" }),
    );
    run.eq([gated.expl.key, gated.rows.length], ["detail.room.none", 0], "T20 作废：只写原因，不画表");
    const humNone = detailOf(
        "hum",
        withSection(measured(), "hum", { kind: "none", overFloorDb: 0, energySharePct: 0 }),
    );
    run.eq([humNone.expl.key, humNone.rows.length], ["detail.hum.none", 0], "没检出电流声：一句话，没有表");
    const lower = detailOf("dr", withSection(measured(), "directRatio", { lowerBoundOnly: true }));
    run.check(lower.rows[0].abs.text.startsWith("≥"), "D/R 只报下限：数字前写“≥”");
}

// ---- ④ 没有“首次测量” ----
// 快照的 previous 只含被重测换掉的区段：没有 previous 既可能是重测之后才第一次有读数，也可能是原来那一整套里测过、这次没动。
// 两种情况分不开，所以卡片不标“首次测量”，免得对没动过的卡写错；要标需要快照另给每一步做过几次。
{
    const s = measured();
    s.previous = { gain: structuredClone(s.results.gain) };
    const by = Object.fromEntries(cardsOf(s).map((c) => [c.id, c]));
    run.eq(
        cardsOf(s).some((c) => c.first !== undefined),
        false,
        "只重测了增益：没动过的卡（底噪等）不会被写成“首次测量”",
    );
    run.check(!!by.level.prev, "增益卡照旧走“上次 → 现在”");
}
{
    const bad = withSection(withDir(SCENES.ready()), "direction", { levelDb: [1, 2, 3] });
    const d = detailOf("dir", bad);
    run.eq(
        [d.expl.key, d.rows.length, d.radar],
        ["detail.dir.none", 0, undefined],
        "方向数组长度不对：不画表，也不抛错",
    );
    const nan = detailOf(
        "dir",
        withSection(withDir(SCENES.ready()), "direction", { brightnessDb: [1, 2, Number.NaN, 4] }),
    );
    run.eq(nan.rows.length, 0, "方向数组里有非有限数：不画表");
}

// ---- ⑤ 反例 ----
{
    const d = detailOf("level", measured());
    const broke = { ...d, rows: d.rows.map((r) => ({ ...r, abs: { text: "NaN" } })) };
    run.check(/NaN/.test(allText(broke)), "反例：表格里写出 NaN 会被查出");
    run.check(!/NaN/.test(allText(d)), "正例：正常读数没有 NaN");
    run.eq(detailOf("level", measured()).cta, null, "正例：有效的卡不给重测按钮");
}

run.finish();
