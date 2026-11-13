// SPDX-License-Identifier: GPL-3.0-or-later
//
// 详情面板的纯逻辑冒烟（不要浏览器）：
//   ① 每张卡点开后的解释、表格、高亮行、说明；数字都来自快照，不写出 NaN 或 undefined；
//   ② 过期、要再测、没测过时的按钮；方向的表格与雷达；
//   ③ 缺 bands、口水音 0 次、齿音高频段不可测、T20 作废这几种缺口的写法；
//   ④ “首次测量”：这一步做完不止一次、而本区段没有上次的读数才标；方向数组异常时不画表；
//   ⑤ 证据画布的配置：哪几张卡画、取哪一步、哪一类是当前类；
//   ⑥ 反例：检查要能变红。
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
const { EVIDENCE_OF } = await at("web/app/detail/evidence-core.js");
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
const keyOk = (k) => k in dict.zh || k in flowDict.zh;
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
    run.eq(dict.zh["detail.retest"], "重测这一步", "过期卡的按钮写“重测这一步”（中文）");
    run.eq(dict.en["detail.retest"], "Re-test this step", "过期卡的按钮写 Re-test this step（英文）");
    const dirStale = detailOf("dir", staled(base, "position"));
    run.eq(
        [
            dirStale.card.stale,
            dirStale.cta && dirStale.cta.key,
            dirStale.cta && dirStale.cta.step,
            dirStale.cta && dirStale.cta.prep,
        ],
        [true, "detail.dir.go", "direction", true],
        "方向过期卡：按钮是“去测方向”，只开准备页（要用户先转话筒），不写“重测”",
    );
    run.eq([dict.zh["detail.dir.go"], dict.en["detail.dir.go"]], ["去测方向", "Test the direction"], "“去测方向”词条");
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
    // 并列最响：只亮原下标最小的那一行（turn 把当前方向转到了第一位，所以要在原下标上比较）
    const tie = (levelDb, currentIndex) =>
        detailOf(
            "dir",
            withSection(base, "direction", { levelDb, brightnessDb: [-1, -2, -3, -4], currentIndex }),
        ).rows.map((r) => r.hl);
    run.eq(tie([-3, -2, -2, -9], 0), [false, true, false, false], "方向并列最响：只高亮第一个");
    run.eq(
        tie([-6, -2, -2, -9], 2),
        [false, false, false, true],
        "方向并列最响：当前方向转到第一位后仍按原下标取第一个",
    );
    run.eq(tie([0, -2, 0, -9], 0), [true, false, false, false], "方向并列最响：当前方向自己就是最响之一时亮它");
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
    // 相对值带符号：为正（最响一次比说话峰值还响）时句子不能写成“低于”
    const loudSnap = (rel) =>
        withSection(measured(), "click", { count: 3, perMinute: 6, loudestRelDb: rel, loudestDbfs: -5 });
    const pos = detailOf("mouth", loudSnap(2.5));
    run.eq(
        [pos.expl.vars.v, pos.rows.find((r) => r.label.key === "detail.r.loud").rel.text],
        ["+2.5", "+2.5"],
        "口水音：相对值为正时解释句与表格都带 +",
    );
    run.eq(hit.expl.vars.v, "−24.0", "口水音：相对值为负时解释句带 −");
    run.check(
        !/低于/.test(dict.zh["detail.mouth.e"]) && !/below/.test(dict.en["detail.mouth.e"]),
        "口水音解释句的词条不写死“低于”：正负由数字的符号表达",
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

// ---- ④ 首次测量 ----
// 快照的 previous 只含被重测换掉的区段；每一步另有 runs（做完过几次）。标“首次测量”要两条同时成立：
// 这一步 runs 不少于 2，且这张卡所在的区段没有 previous。
{
    const firsts = (s) =>
        cardsOf(s)
            .filter((c) => c.first)
            .map((c) => c.id);
    const s = measured();
    run.eq(firsts(s), [], "第一次整套测试（runs 都不超过 1）：谁都不标首次测量");
    for (const id of ["gain", "quiet", "room", "position"]) s.steps[id].runs = 1;
    s.steps.gain.runs = 2;
    s.steps.quiet.runs = 2;
    s.previous = { gain: structuredClone(s.results.gain) };
    const by = Object.fromEntries(cardsOf(s).map((c) => [c.id, c]));
    run.check(!!by.level.prev && !by.level.first, "增益重测过且有上次：走“上次 → 现在”，不标首次");
    run.eq(
        firsts(s),
        ["noise", "hum"],
        "底噪那一步 runs 为 2 而 previous 里没有它：底噪与电流声标首次测量；runs 为 1 的步骤不标",
    );
    s.previous.noise = structuredClone(s.results.noise);
    run.eq(firsts(s), [], "previous 里有底噪（电流声与它同一步）：两张卡都不标");
    // 上次有这个区段、只是那次读数不可用（要再测）：卡上没有“上次 → 现在”，但也不是首次测量
    s.previous = {
        noise: withSection(measured(), "noise", { reliability: "retest", retestReason: "voice-detected", tone: "none" })
            .results.noise,
    };
    run.eq(
        firsts(s),
        ["level"],
        "上次有底噪区段但那次读数不可用：底噪与电流声不标首次测量（增益 runs 为 2 且没有上次，仍标）",
    );
    s.previous = {};
    s.steps.quiet.runs = 1;
    run.eq(firsts(s), ["level"], "反例：底噪那一步 runs 回到 1，底噪与电流声不再标；增益 runs 为 2 且没有上次，仍标");
    const old = measured();
    old.steps.quiet.runs = 3;
    old.results.noise.stale = true;
    old.results.hum.stale = true;
    run.eq(firsts(old), [], "过期的卡没有结论，不标首次测量");
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

// ---- ⑤ 证据画布的配置 ----
{
    const snap = (patch) => {
        const s = measured();
        for (const [id, rev] of Object.entries(patch)) s.steps[id].evidenceRev = rev;
        return s;
    };
    run.eq(Object.keys(EVIDENCE_OF).sort(), ["level", "mouth", "plosive", "sib"], "只有增益、喷麦、齿音、口水音画证据");
    for (const id of CARD_ORDER)
        run.eq(detailOf(id, measured()).evidence, null, `${id}：这一步没有证据（evidenceRev 为 0）不画`);
    const pl = detailOf("plosive", snap({ position: 3 })).evidence;
    run.eq(
        [pl.step, pl.rev, pl.focus, pl.kinds],
        ["position", 3, "plosive", ["plosive", "sibilance", "mouth"]],
        "喷麦：取位置步，三种事件都画，当前类是喷麦",
    );
    run.eq(detailOf("sib", snap({ position: 1 })).evidence.focus, "sibilance", "齿音：当前类是齿音");
    run.eq(detailOf("mouth", snap({ position: 1 })).evidence.focus, "mouth", "口水音：当前类是口水音");
    const lv = detailOf("level", snap({ gain: 2 })).evidence;
    run.eq([lv.step, lv.kinds, lv.focus], ["gain", [], null], "增益：取增益步，只有包络没有事件标记");
    for (const id of ["noise", "hum", "room", "dr"])
        run.eq(
            detailOf(id, snap({ gain: 1, quiet: 1, room: 1, position: 1 })).evidence,
            null,
            `${id}：证据不在载荷里，只给表格`,
        );
    const gone = snap({ position: 2 });
    delete gone.results.plosive;
    run.eq(detailOf("plosive", gone).evidence, null, "没测过的卡不画");
    run.eq(
        detailOf("plosive", staled(snap({ position: 1 }), "position")).evidence !== null,
        true,
        "过期的卡仍画那一次的证据（整块转灰由样式负责）",
    );
}

// ---- ⑥ 反例 ----
{
    const d = detailOf("level", measured());
    const broke = { ...d, rows: d.rows.map((r) => ({ ...r, abs: { text: "NaN" } })) };
    run.check(/NaN/.test(allText(broke)), "反例：表格里写出 NaN 会被查出");
    run.check(!/NaN/.test(allText(d)), "正例：正常读数没有 NaN");
    run.eq(detailOf("level", measured()).cta, null, "正例：有效的卡不给重测按钮");
}

run.finish();
