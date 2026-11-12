// SPDX-License-Identifier: GPL-3.0-or-later
//
// 报告页的纯逻辑冒烟（不要浏览器）：
//   ① 顶部“最该改”：fixes 与 advice 到标题、动作、主按钮的映射；过期、要再测、没测、可以录各走各的分支；
//   ② 方向提示的四种放法（主按钮、动作加次要按钮、方向卡上的小按钮、不放），耳机麦没有方向卡；
//   ③ 九张结果卡：顺序固定，未测是灰卡，齿音与口水音不分档所以卡片无色、词是“未分档”，口水音 0 次不写“最响一次”，
//      “上次 → 现在”只给有上次的、有结论的卡；
//   ④ 距离标尺：位置只是示意，证据越多越靠边，过期与要再测是灰的，齿音与口水音不作证据；
//   ⑤ 复制结果、方向确认文案（点出两个方向、不写角度）、频率常量与 thresholds.json 同值；
//   ⑥ 缺 bands 时结果页不退回准备页，设备降噪作废时写设备原因；快照缺字段不写 NaN；方向动作最多占第 3 条；
//   ⑦ 反例：检查要能变红。
//
// 用法：node web-preview/tests/smoke-report-core.mjs [仓库根]

import path from "node:path";
import { pathToFileURL } from "node:url";
import { createRun, repoRoot } from "./lib/harness.mjs";
import { readText } from "./lib/files.mjs";

const ROOT = repoRoot();
const run = createRun("报告页逻辑");
const at = (rel) => import(pathToFileURL(path.join(ROOT, rel)).href);
const core = await at("web/app/report/report-core.js");
const flow = await at("web/app/flow/flow-core.js");
const { SCENES, measured, staled, withSection } = await at("web-preview/mock/report-fixtures.js");
const { buildSnapshot } = await at("web-preview/mock/snapshot.js");
const { withResult } = await at("web-preview/mock/results.js");
const flowStrings = (await at("web/app/flow/flow-strings.js")).default;
const flowMeter = (await at("web/app/meter/meter-strings.js")).default;
const thresholds = JSON.parse(readText(ROOT, "thresholds.json"));

const headKey = (s) => core.fixModel(s).head.key;
const stepsOf = (m) => m.cta.steps;

// ---- ① 顶部“最该改” ----
{
    const m = core.fixModel(SCENES.t20longDrlow());
    run.eq(
        [m.tone, m.head.key, m.cta.kind, stepsOf(m), m.eyebrow],
        ["red", "report.fx.room1.h", "retest", ["gain", "quiet", "room", "position"], "report.fix"],
        "T20 长 + D/R 低：红，先换地方并靠近，重测四步",
    );
    run.eq(m.acts.length <= 3, true, "动作最多 3 条");
}
{
    const m = core.fixModel(SCENES.t20okDrlow());
    run.eq(
        [m.tone, m.head.key, m.cta.kind, m.cta.to, m.cta.prep],
        ["red", "report.fx.room2.h", "go", "direction", true],
        "T20 正常 + D/R 低且提示在主按钮：主按钮去方向的准备页",
    );
    const none = { ...SCENES.t20okDrlow(), advice: { ...SCENES.t20okDrlow().advice, directionPrompt: "none" } };
    run.eq(
        [headKey(none), core.fixModel(none).cta.kind, stepsOf(core.fixModel(none))],
        ["report.fx.room2.hs", "retest", ["position"]],
        "方向不提示（耳机麦或测过）：标题只说距离，主按钮重测位置",
    );
}
{
    const m = core.fixModel(SCENES.ready());
    run.eq(
        [m.tone, m.eyebrow, m.cta.kind],
        ["green", "report.fix.ok", "copy"],
        "都好：绿，眉标是“现在的结论”，主按钮是复制",
    );
    run.eq(
        m.acts.map((a) => a.key),
        ["report.fx.ok.a1", "report.fx.ok.a2"],
        "方向没测：多一条“方向检查是可选的”",
    );
    const tested = {
        ...SCENES.ready(),
        results: { ...SCENES.ready().results, direction: withResult("direction", {}) },
    };
    tested.steps.direction.status = "done";
    run.eq(core.fixModel(tested).acts.length, 1, "方向测过：不再提方向");
}
{
    const m = core.fixModel(SCENES.tooClose());
    run.eq(
        [m.tone, m.head.key, stepsOf(m)],
        ["red", "report.fx.near.h", ["position"]],
        "喷麦重、太近：红，先退后，重测位置",
    );
    const c = SCENES.conflict();
    const m2 = core.fixModel(c);
    run.eq(
        [m2.head.key, m2.acts.length, m2.acts[2] && m2.acts[2].dir, m2.sec && m2.sec.to],
        ["report.fx.conf.h", 3, true, "direction"],
        "证据矛盾 + 动作加按钮：最后一条是方向，次要按钮去方向",
    );
    const off = { ...c, advice: { ...c.advice, directionPrompt: "none" } };
    run.eq(
        [headKey(off), core.fixModel(off).acts.length, core.fixModel(off).sec],
        ["report.fx.conf.hs", 2, null],
        "矛盾且方向不提示：标题不提方向，没有次要按钮",
    );
}
{
    const m = core.fixModel(SCENES.lowConfidence());
    run.eq(
        [m.tone, m.head.key, m.head.vars.n, stepsOf(m), m.acts.length],
        ["gray", "report.fx.low.h", 2, ["quiet", "room"], 3],
        "两项要再测：灰，写项数，重测它们",
    );
}
{
    const g = core.fixModel(SCENES.staleGain());
    run.eq(
        [g.tone, g.head.key, stepsOf(g)],
        ["gray", "report.fx.rs1.h", ["gain", "quiet"]],
        "调了增益：只重测增益与安静（齿音单独过期不催位置步）",
    );
    const p = core.fixModel(SCENES.stalePosition());
    run.eq(
        [p.head.key, stepsOf(p), p.acts.length],
        ["report.fx.rs2.h", ["gain", "quiet", "position"], 2],
        "挪了位置：重测增益、安静、位置",
    );
    const r = core.fixModel(SCENES.staleRoom());
    run.eq([r.head.key, stepsOf(r)], ["report.fx.rs3.h", ["gain", "quiet", "room", "position"]], "换了房间：全部重测");
}
{
    const m = core.fixModel(buildSnapshot());
    run.eq(
        [m.tone, m.head.key, m.cta.kind, m.cta.to],
        ["gray", "report.fx.none.h", "go", "connect"],
        "什么都没测：灰，去接通页",
    );
    const part = measured();
    delete part.results.reverb;
    delete part.results.directRatio;
    delete part.results.plosive;
    delete part.results.sibilance;
    delete part.results.click;
    delete part.results.distance;
    const m2 = core.fixModel(part);
    run.eq(
        [m2.head.key, m2.head.key === "report.fx.part.h" && m2.cta.to, m2.acts[0].vars.steps],
        ["report.fx.part.h", "room", ["room", "position"]],
        "只测了增益和安静：灰，还没测房间和位置，主按钮去第一个没测的",
    );
    const keep = {
        ...SCENES.ready(),
        advice: { room: "keep-close-dont-back-off", distance: "none", directionPrompt: "none" },
    };
    run.eq(
        [core.fixModel(keep).tone, headKey(keep)],
        ["amber", "report.fx.keep.h"],
        "T20 长 + D/R 高：fixes 为空也给琥珀的“别退后”",
    );
    const clipStale = { ...staled(SCENES.ready(), "gain"), fixes: ["digital-clip"] };
    run.eq(headKey(clipStale), "report.fx.clip.h", "数字削波固定在前：有过期结果也照 fixes 来");
    const nosig = { ...SCENES.ready(), fixes: ["no-signal"] };
    run.eq([core.fixModel(nosig).cta.kind, core.fixModel(nosig).cta.to], ["go", "connect"], "没信号：主按钮回接通页");
    const hum = { ...SCENES.t20longDrlow(), fixes: ["hum"] };
    run.eq(
        [core.fixModel(hum).head.key, stepsOf(core.fixModel(hum))],
        ["report.fx.hum.h", ["quiet"]],
        "电流声：重测安静",
    );
}

// ---- ② 方向卡上的小按钮 ----
run.eq(core.directionSlot(SCENES.t20longDrlow()), "go", "card-button：方向卡上有“测一下方向”");
run.eq(
    core.directionSlot({ ...SCENES.ready(), advice: { ...SCENES.ready().advice, directionPrompt: "stale" } }),
    "again",
    "stale：方向卡上写“重测”",
);
run.eq(core.directionSlot(SCENES.t20okDrlow()), null, "主按钮放法：方向卡上不放小按钮");
run.eq(
    core.directionSlot({ ...SCENES.t20longDrlow(), micType: "headset" }),
    null,
    "耳机麦：没有方向卡，也就没有小按钮",
);

// ---- ③ 九张结果卡 ----
{
    const s = SCENES.ready();
    const cards = core.cardsOf(s);
    run.eq(
        cards.map((c) => c.id),
        core.CARD_ORDER,
        "九张卡，顺序固定",
    );
    run.eq(core.cardsOf({ ...s, micType: "headset" }).length, 8, "耳机麦：八张卡，没有方向卡");
    const dir = cards.find((c) => c.id === "dir");
    run.eq([dir.missing, dir.tone, dir.nums], [true, "gray", []], "方向没测：灰色的“未测”卡");
    const sib = cards.find((c) => c.id === "sib");
    const mouth = cards.find((c) => c.id === "mouth");
    run.eq(
        [sib.word, sib.tone, mouth.word, mouth.tone],
        ["flow.w.ungraded", "none", "flow.w.ungraded", "none"],
        "齿音、口水音不分档：词是“未分档”，卡片无色",
    );
    run.eq(sib.bandLabels, ["3–6 kHz", "6–12 kHz"], "齿音两个频段的标签取自常量，不写在词条里");
    run.eq(
        mouth.note.key === "flow.cd.mouth.note" && mouth.note.vars.f,
        "2",
        "口水音说明写“约 2 kHz 高通后”，频率从常量来",
    );
    const zero = withSection(s, "click", { count: 0, perMinute: 0, loudestRelDb: 0, loudestDbfs: 0 });
    run.eq(core.cardsOf(zero).find((c) => c.id === "mouth").note, null, "口水音 0 次：不写“最响一次”");
    const flat = withSection(s, "sibilance", { highBandDisabled: true, effectiveBandwidthHz: 8000 });
    const sb = core.cardsOf(flat).find((c) => c.id === "sib");
    run.eq([sb.nums[1].na, sb.note.vars.bw], ["flow.w.s.part", "8"], "带宽受限：高频段写“未测”，带宽数取自快照");
    const graded = withSection(s, "sibilance", { severity: "moderate", tone: "amber" });
    run.eq(
        core.cardsOf(graded).find((c) => c.id === "sib").word,
        "flow.w.s.hi",
        "将来分了档（tone 不再是 none）就按档位写词",
    );
}
{
    const s = staled(SCENES.ready(), "gain");
    const by = Object.fromEntries(core.cardsOf(s).map((c) => [c.id, c.stale]));
    run.eq(
        [by.level, by.noise, by.hum, by.sib, by.room, by.dr],
        [true, true, true, true, false, false],
        "调增益：增益、底噪、电流声、齿音过期，房间和 D/R 仍有效",
    );
}
{
    const s = SCENES.ready();
    const prevGain = withResult("gain", { truePeakDbfsL: -8.4, truePeakDbfsR: -8.4 });
    const now = withSection(s, "gain", { truePeakDbfsL: -6.8, truePeakDbfsR: -6.8 });
    const withPrev = { ...now, previous: { gain: prevGain } };
    run.eq(
        core.cardsOf(withPrev).find((c) => c.id === "level").prev,
        { a: "−8.4", b: "−6.8" },
        "上次 → 现在：用同一套卡构造，数字格式一致",
    );
    run.eq(core.cardsOf(withPrev).find((c) => c.id === "noise").prev, undefined, "没有上次的卡不写");
    const retest = withSection(withPrev, "gain", { reliability: "retest", retestReason: "too-short", tone: "none" });
    run.eq(core.cardsOf(retest).find((c) => c.id === "level").prev, undefined, "要再测的卡没有结论，不写上次 → 现在");
}

// ---- ④ 距离标尺 ----
{
    const pos = (kind, close = 0, far = 0) => core.rulerPos({ kind, closeEvidence: close, farEvidence: far });
    run.check(pos("too-close", 1) < pos("fine") && pos("fine") < pos("too-far", 0, 1), "标尺：太近 < 合适 < 太远");
    run.check(pos("too-close", 3) < pos("too-close", 1), "太近的证据越多，越靠左");
    run.check(pos("too-far", 0, 2) > pos("too-far", 0, 1), "太远的证据越多，越靠右");
    run.eq(pos("unknown"), null, "证据不足：没有标记");
    const r = core.rulerModel(SCENES.tooClose());
    run.eq(
        [r.st, r.near[0].on, r.near[1].on, r.near[2].on, r.near[1].off],
        ["fresh", true, false, false, true],
        "喷麦重触发“太近”；齿音与口水音不作证据",
    );
    run.eq(core.rulerModel(SCENES.t20longDrlow()).far[0].on, true, "D/R 低触发“太远”主证据");
    run.eq(core.rulerModel(SCENES.conflict()).conflict, true, "矛盾");
    run.eq(core.rulerModel(SCENES.stalePosition()).st, "stale", "位置过期：灰");
    run.eq(core.rulerModel(buildSnapshot()).st, "low", "没测位置：灰，没有标记");
    const ghost = {
        ...SCENES.tooClose(),
        previous: { distance: withResult("distance", { kind: "too-far", closeEvidence: 0, farEvidence: 1 }) },
    };
    run.check(core.rulerModel(ghost).prev > 0.5, "上次的位置画成虚影");
    run.eq(JSON.stringify(core.rulerModel(SCENES.tooClose())).match(/\bcm\b|厘米/), null, "标尺不显示距离数值");
}

// ---- ⑤ 复制结果、方向确认、常量 ----
{
    const c = core.copyParts(SCENES.ready());
    run.eq(
        c.parts.map((p) => p.key),
        ["report.cp.level", "report.cp.snr", "report.cp.t20", "report.cp.dr"],
        "复制结果：有结论的卡各一小段",
    );
    run.check(c.fix && c.fix.key === "report.fx.ok.h", "绿色时接最该改的那句");
    const st = core.copyParts(SCENES.staleGain());
    run.eq(
        [st.parts.map((p) => p.key), st.fix],
        [["report.cp.t20", "report.cp.dr"], null],
        "过期的不写，灰色的不接那句",
    );
}
{
    const live = (o) => ({ ...buildSnapshot(), live: { ...buildSnapshot().live, ...o } });
    run.eq(
        flow.confirmPair(live({ round: 2, rounds: 2, directionIndex: 2 })),
        { a: 1, b: 3 },
        "确认第二遍：点出方向 1 与方向 3（更响的那个）",
    );
    run.eq(
        flow.confirmPair(live({ round: 1, rounds: 2, directionIndex: 0 })).b,
        null,
        "还不知道更响的是哪个：用通用文案",
    );
    const keys = Object.keys(flowStrings.zh).filter((k) => /^flow\.rm\.dir\.(confirm|pair)/.test(k));
    run.check(keys.length >= 4, "确认文案的词条在");
    for (const k of keys)
        run.check(!/180|270|90/.test(flowStrings.zh[k] + flowStrings.en[k]), `${k}：方向确认文案不写角度`);
}
// 方向确认：测试中视图用点出两个方向的文案
{
    const s = buildSnapshot();
    s.steps.direction.status = "running";
    s.live = { ...s.live, phase: "confirm", round: 2, rounds: 2, directionIndex: 2, phaseDurationSec: 6 };
    const m = flow.remoteModel(s);
    run.eq(
        [m.cmd.key, m.cmd.vars.a, m.cmd.vars.b, m.words, m.wordsVars],
        ["flow.rm.dir.confirm", 1, 3, "flow.rm.dir.pair", { a: 1, b: 3 }],
        "方向确认的测试中视图点出方向 1 与方向 3",
    );
    s.live = { ...s.live, round: 1, directionIndex: 0 };
    run.eq(flow.remoteModel(s).cmd.key, "flow.rm.dir.confirm.gen", "不知道哪个方向更响时用通用文案");
}
// 连接步在跑时不进整窗的测试中视图
{
    const s = buildSnapshot();
    s.steps.connect.status = "running";
    run.eq(flow.remoteModel(s), null, "接通观察窗只有几秒，接通页自己显示，不整窗换成测试中视图");
}
// 频率与雷达环刻度不写死在词条里
for (const k of ["flow.cd.sib.bw", "flow.cd.mouth.note", "flow.dir.ring"]) {
    run.check(
        !/\d+(\.\d+)? ?(kHz|Hz)/.test(flowStrings.zh[k] + flowStrings.en[k]) && /\{\w+\}/.test(flowStrings.zh[k]),
        `${k}：频率和刻度用占位符，不写死数字`,
    );
}
run.check(!/−12|-12/.test(flowStrings.zh["flow.dir.ring"] + flowStrings.en["flow.dir.ring"]), "雷达环刻度文字取自常量");
// 话筒类型“跳过”和“不确定”是同一个选项，映射到 unknown
{
    const mics = Object.keys(flowMeter.zh).filter((k) => /^meter\.mic\./.test(k));
    run.eq(mics.length, 5, "话筒类型菜单只有五项：没有单独的“跳过”");
    run.check(
        flowMeter.zh["meter.mic.unknown"].includes("不确定") && flowMeter.zh["meter.mic.unknown"].includes("跳过"),
        "“不确定（可跳过）”是同一项，对应 unknown",
    );
}
run.eq(
    flow.SIB_BANDS_HZ,
    [thresholds.sibilance.mid_high_band_hz.value, thresholds.sibilance.high_band_hz.value],
    "齿音频段常量与 thresholds.json 同值",
);
run.eq(flow.CLICK_HIGHPASS_HZ, thresholds.click.highpass_hz.value, "口水音高通频率常量与 thresholds.json 同值");

// ---- ⑥ 缺 bands、设备降噪作废 ----
{
    const s = measured();
    const noBands = { ...s, bands: undefined };
    for (const id of ["gain", "quiet", "room", "position"]) {
        const m = flow.resultModel(noBands, id);
        run.check(m !== null && m.cards.length > 0, `缺 bands：${id} 的结果页照常画读数，不退回准备页`);
        if (m) run.eq(m.viz === null || m.viz.kind !== "bands", true, `缺 bands：${id} 没有区间条`);
    }
    run.eq(
        flow.resultModel(noBands, "gain").expl.key,
        "flow.rs.nobands",
        "缺 bands：说明里写明只显示读数，不出现 undefined",
    );
    const gated = withSection(s, "reverb", { gated: true, reliability: "retest", retestReason: "none", tone: "none" });
    run.eq(core.cardOf("room", gated).why, "flow.cf.why.device", "降噪或噪声门作废且没给原因：写设备原因");
    const few = withSection(s, "reverb", { tMidSec: null, retestReason: "none" });
    run.eq(core.cardOf("room", few).why, "flow.cf.why.fewclaps", "有效拍手不够：写拍手原因");
    run.eq(
        flow.prepModel({ ...s, steps: { ...s.steps, gain: { ...s.steps.gain, durationSec: 0 } } }, "gain").sec,
        0,
        "计划时长为 0：准备页模型给 0（界面不写“约 0 秒”）",
    );
}

// ---- ⑥b 评审补的：缺字段不写 NaN；方向动作占第 3 条；缺电流声的卡随噪声过期 ----
{
    const low = { ...withSection(measured(), "gain", { band: "quiet", tone: "amber" }), fixes: ["gain"] };
    const n = (st) => core.fixModel(st).acts[0].vars;
    run.check(Number.isFinite((n(low) || {}).n), "增益偏低、数据齐：动作里有 dB 数");
    const right = (st) => ({
        ...st,
        device: { ...st.device, layout: "stereo" },
        connect: { ...st.connect, analysedChannel: "right" },
    });
    const nan = right(withSection(low, "gain", { truePeakDbfsR: Number.NaN }));
    run.eq(
        core.fixModel(nan).acts.map((a) => a.key),
        ["flow.gr.retest"],
        "右声道峰值不是有限数：不写数字，只留重测一条",
    );
    const noConn = { ...right(low) };
    delete noConn.connect;
    run.check(Number.isFinite((n(noConn) || {}).n), "快照缺 connect：不抛错，按左声道算");
    const noBands = { ...low };
    delete noBands.bands;
    run.eq(core.fixModel(noBands).acts.length, 1, "缺 bands：不写数字");
}
{
    const m = core.fixModel(SCENES.room1Dir());
    run.eq(
        [m.acts.length, m.acts.map((a) => a.key).slice(0, 2), m.acts[2].dir, m.sec && m.sec.to],
        [3, ["flow.rr.a1", "report.fx.closer"], true, "direction"],
        "房间要换且提示是动作加按钮：最多 3 条，方向占第 3 条，原来的第 3 条（解释主按钮）让位",
    );
}
{
    const stale = withSection(measured(), "noise", { stale: true });
    delete stale.results.hum;
    const card = (st) => flow.resultModel(st, "quiet").cards[1];
    run.eq([card(stale).id, card(stale).stale], ["hum", true], "没给电流声结果：补的那张卡随噪声过期");
    const fresh = measured();
    delete fresh.results.hum;
    run.eq(card(fresh).stale, false, "噪声没过期：补的卡也不过期");
}

// ---- ⑦ 反例：检查要能变红 ----
{
    const greenUngraded = (c) => c.tone === "green" && (c.id === "sib" || c.id === "mouth");
    const good = core.cardsOf(SCENES.ready());
    run.eq(good.some(greenUngraded), false, "正例：未分档的卡不是绿色");
    const bad = good.map((c) => (c.id === "sib" ? { ...c, tone: "green" } : c));
    run.eq(bad.some(greenUngraded), true, "反例：把齿音卡着成绿色会被查出");
    const wrongRoom = {
        ...SCENES.t20longDrlow(),
        advice: { room: "unknown", distance: "none", directionPrompt: "none" },
    };
    run.check(core.fixModel(wrongRoom).tone !== "green", "反例：fixes 说要改房间，advice 缺失时也不能给绿色");
    run.check(
        core.fixModel({ ...SCENES.ready(), fixes: ["gain"] }).tone !== "green",
        "反例：fixes 非空就不会是“可以录”",
    );
}

run.finish();
