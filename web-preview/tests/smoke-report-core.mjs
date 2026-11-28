// SPDX-License-Identifier: GPL-3.0-or-later
//
// 报告页的纯逻辑冒烟（不要浏览器）：
//   ① 顶部“最该改”：fixes 与 advice 到标题、动作、主按钮的映射；过期、要再测、没测、可以录各走各的分支；
//      房间只有很混、偏混两种建议，距离与喷麦不在 fixes 里，旧的原生端留下的项与建议值不照它写；
//   ② 方向提示的四种放法（主按钮、动作加次要按钮、方向卡上的小按钮、不放），耳机麦没有方向卡；
//   ③ 九张结果卡：顺序固定，未测是灰卡，D/R、喷麦、齿音与口水音不分档所以卡片无色、词是“未分档”，口水音 0 次不写
//      “最响一次”，“上次 → 现在”只给有上次的、有结论的卡；喷麦、齿音、口水音的参考值取自 bands，超过时只在自己的卡上
//      给提示，不进“最该改”，耳机麦的提示不涉及距离；
//   ④ 距离卡：第一次只有“可能离远了”，重测后按三项读数说更近、更远、没有明显变化或说不准；上次的位置是虚影，
//      标记只示意方向，不显示距离数值；过期与要再测是灰的；
//      房间读数要再测时不写档位；
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
    const m = core.fixModel(SCENES.veryBoomy());
    run.eq(
        [m.tone, m.head.key, m.cta.kind, stepsOf(m), m.eyebrow],
        ["red", "report.fx.room1.h", "retest", ["gain", "quiet", "room", "position"], "report.fix"],
        "T20 很混：红，先换地方并靠近，重测四步",
    );
    run.eq(m.acts.length <= 3, true, "动作最多 3 条");
}
{
    const m = core.fixModel(SCENES.maybeTooFar());
    run.eq(
        [m.tone, m.head.key, m.cta.kind, m.cta.to, m.cta.prep, m.acts[0].key],
        ["red", "report.fx.far.h", "go", "direction", true, "report.fx.closer"],
        "T20 正常 + 底噪偏大 + 可能离远了且提示在主按钮：先靠近，主按钮去方向的准备页",
    );
    const none = { ...SCENES.maybeTooFar(), advice: { ...SCENES.maybeTooFar().advice, directionPrompt: "none" } };
    run.eq(
        [headKey(none), core.fixModel(none).cta.kind, stepsOf(core.fixModel(none))],
        ["report.fx.farnoise.h", "retest", ["position"]],
        "方向不提示（耳机麦或测过）：标题只说离远了，主按钮重测位置",
    );
    const boomy = core.fixModel({
        ...SCENES.veryBoomy(),
        fixes: ["room"],
        advice: { ...SCENES.veryBoomy().advice, room: "move-closer-then-treat" },
    });
    run.eq(
        [boomy.tone, boomy.head.key, stepsOf(boomy)],
        ["amber", "report.fx.room3.h", ["gain", "quiet", "position"]],
        "T20 偏混：琥珀，先靠近再测，不行再加吸声",
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
    // 距离与喷麦不进“最该改”：重测后更近、更远、说不准，顶部都还是绿的
    for (const name of ["closer", "farther", "noChange", "overReference"]) {
        const m = core.fixModel(SCENES[name]());
        run.eq([m.tone, m.head.key], ["green", "report.fx.ok.h"], `${name}：顶部仍是“可以开始录音了”`);
    }
    const c = core.fixModel(SCENES.conflicting());
    run.eq(
        [c.tone, c.head.key, c.acts.length, c.sec && c.sec.to, c.sec && c.sec.prep],
        ["green", "report.fx.ok.h", 2, "direction", true],
        "几项读数指向不同方向 + 动作加按钮：顶部仍是绿的，多一个去方向准备页的次要按钮",
    );
    const off = { ...SCENES.conflicting(), advice: { ...SCENES.conflicting().advice, directionPrompt: "none" } };
    run.eq(core.fixModel(off).sec, null, "方向不提示：没有次要按钮");
    // 旧的原生端留下的项与建议值：不认识的项跳过，不认识的建议不单独成卡
    const legacy = { ...SCENES.ready(), fixes: ["distance", "plosive"] };
    run.eq(headKey(legacy), "report.fx.ok.h", "fixes 里只有界面不认识的项：按没有处理");
    const legacy2 = { ...SCENES.veryBoomy(), fixes: ["distance", "room"] };
    run.eq(headKey(legacy2), "report.fx.room1.h", "不认识的项排在前面：跳过，照后面认识的写");
    const oldRoom = {
        ...SCENES.veryBoomy(),
        advice: { ...SCENES.veryBoomy().advice, room: "move-closer-check-direction" },
    };
    run.eq(
        [headKey(oldRoom), core.fixModel(oldRoom).tone],
        ["report.fx.room0.h", "amber"],
        "fixes 有房间但建议是旧的取值：只说“房间声偏多”，不替原生写具体做法",
    );
    const keep = {
        ...SCENES.ready(),
        advice: { room: "keep-close-dont-back-off", distance: "back-off-a-little", directionPrompt: "none" },
    };
    run.eq(headKey(keep), "report.fx.ok.h", "旧的“别退后”“退后一点”不再单独成卡");
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
    const clipStale = { ...staled(SCENES.ready(), "gain"), fixes: ["digital-clip"] };
    run.eq(headKey(clipStale), "report.fx.clip.h", "数字削波固定在前：有过期结果也照 fixes 来");
    const nosig = { ...SCENES.ready(), fixes: ["no-signal"] };
    run.eq([core.fixModel(nosig).cta.kind, core.fixModel(nosig).cta.to], ["go", "connect"], "没信号：主按钮回接通页");
    const hum = { ...SCENES.veryBoomy(), fixes: ["hum"] };
    run.eq(
        [core.fixModel(hum).head.key, stepsOf(core.fixModel(hum))],
        ["report.fx.hum.h", ["quiet"]],
        "电流声：重测安静",
    );
}

// ---- ② 方向卡上的小按钮 ----
run.eq(core.directionSlot(SCENES.veryBoomy()), "go", "card-button：方向卡上有“测一下方向”");
run.eq(
    core.directionSlot({ ...SCENES.ready(), advice: { ...SCENES.ready().advice, directionPrompt: "stale" } }),
    "again",
    "stale：方向卡上写“重测”",
);
run.eq(core.directionSlot(SCENES.maybeTooFar()), null, "主按钮放法：方向卡上不放小按钮");
run.eq(core.directionSlot({ ...SCENES.veryBoomy(), micType: "headset" }), null, "耳机麦：没有方向卡，也就没有小按钮");

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
    run.eq(
        sib.nums.map((n) => [n.text, n.unit, n.label]),
        [["−6.0", "dB", { key: "flow.cd.sib.vowel", vars: { b: "6–12 kHz" } }]],
        "齿音卡只有一个数：6-12 kHz 比元音，频段取自常量；两个频段的绝对电平不上卡",
    );
    run.eq(sib.note, null, "能和元音比：齿音卡没有说明");
    run.eq(
        [mouth.note.key, mouth.note.vars],
        ["flow.cd.mouth.note", { s: "20" }],
        "口水音卡写“基于 N 秒说话”，秒数取自 analysedSeconds；不写“高通后最响一次”",
    );
    const zero = withSection(s, "click", { count: 0, perMinute: 0, loudestRelDb: 0, loudestDbfs: 0 });
    run.eq(core.cardsOf(zero).find((c) => c.id === "mouth").note.vars.s, "20", "口水音 0 次：照样写按多少秒说话算的");
    const noSecs = withSection(s, "click", { analysedSeconds: 0 });
    run.eq(core.cardsOf(noSecs).find((c) => c.id === "mouth").note, null, "说话时长为 0：不写秒数");
    const tiny = withSection(s, "click", { analysedSeconds: 0.4 });
    run.eq(core.cardsOf(tiny).find((c) => c.id === "mouth").note, null, "说话时长取整后是 0：也不写“0 秒”");
    const flat = withSection(s, "sibilance", { highBandDisabled: true, effectiveBandwidthHz: 8000 });
    const sb = core.cardsOf(flat).find((c) => c.id === "sib");
    run.eq(
        [sb.nums.map((n) => [n.text, n.unit, n.label]), sb.note],
        [
            [["−12.0", "dBFS", { key: "flow.cd.sib.level", vars: { b: "3–6 kHz" } }]],
            { key: "flow.cd.sib.bw", vars: { bw: "8" } },
        ],
        "带宽受限：没法和元音比，卡上退回 3-6 kHz 的电平并写明；带宽数取自快照",
    );
    const nv = core.cardsOf(withSection(s, "sibilance", { vowelEvents: 0 })).find((c) => c.id === "sib");
    run.eq(
        [nv.nums.map((n) => [n.text, n.unit, n.label]), nv.note],
        [
            [["−18.0", "dBFS", { key: "flow.cd.sib.level", vars: { b: "6–12 kHz" } }]],
            { key: "flow.cd.sib.novowel", vars: undefined },
        ],
        "没有带元音参考的擦音：卡上退回 6-12 kHz 的电平，写明没法和元音比",
    );
    // 不分档是契约：原生即使给了档位与色调（旧的原生端），这四张卡也不按档写词、不着色
    const graded = (id, section, patch) => core.cardsOf(withSection(s, section, patch)).find((c) => c.id === id);
    run.eq(
        [
            graded("sib", "sibilance", { severity: "moderate", tone: "amber" }),
            graded("plosive", "plosive", { severity: "heavy", tone: "red", worstRatioDb: 12 }),
            graded("mouth", "click", { severity: "heavy", tone: "red" }),
            graded("dr", "directRatio", { tone: "green" }),
        ].map((c) => [c.word, c.tone]),
        Array(4).fill(["flow.w.ungraded", "none"]),
        "原生给了档位与色调：D/R、喷麦、齿音、口水音仍写“未分档”、无色",
    );
    run.eq(
        graded("plosive", "plosive", { worstRatioDb: 10.3 }).nums[0].text,
        "+10.3",
        "喷麦卡的数字是每遍字表最大的瞬态比",
    );
}

// 参考值与轻提示：数值取自快照的 bands；超过参考值时只在各自的卡上给提示，不进“最该改”
{
    const by = (s) => Object.fromEntries(core.cardsOf(s).map((c) => [c.id, c]));
    const base = by(SCENES.ready());
    run.eq(
        [base.plosive.ref, base.plosive.tip],
        [{ key: "flow.ref.plosive", vars: { v: "9" }, over: false }, null],
        "喷麦：参考 9 dB 以下，取自 bands.plosiveReferenceDb；没超过不提示",
    );
    run.eq(
        base.sib.ref,
        { key: "flow.ref.sib", vars: { v: "−3" }, over: false },
        "齿音：比值就是主数字，参考行只写参考 −3 dB，取自 bands.sibilanceReferenceDb",
    );
    // 假数据里已有口水音参考值，这里显式去掉，测 null 的分支
    const noClickRef = { ...SCENES.ready(), bands: { ...SCENES.ready().bands, clickReferencePerMin: null } };
    run.eq(by(noClickRef).mouth.ref, null, "口水音：参考值是 null 时不显示参考");
    const moved = { ...SCENES.ready(), bands: { ...SCENES.ready().bands, plosiveReferenceDb: 7.5 } };
    run.eq(by(moved).plosive.ref.vars.v, "7.5", "参考值不写在界面里：bands 变了，显示跟着变");
    const noBands = SCENES.ready();
    delete noBands.bands;
    run.eq(
        [by(noBands).plosive.ref, by(noBands).sib.ref, by(noBands).mouth.ref],
        [null, null, null],
        "旧的原生端没有 bands：不显示参考，也不写 undefined",
    );
    const narrow = withSection(SCENES.overReference(), "sibilance", {
        highBandDisabled: true,
        effectiveBandwidthHz: 8000,
    });
    run.eq(
        [by(narrow).sib.ref, by(narrow).sib.tip && by(narrow).sib.tip.id],
        [null, "angle-soften-de-ess"],
        "高频项关闭：齿音不显示参考（建议键由原生侧决定，卡片照给）",
    );
    const noVowel = withSection(SCENES.ready(), "sibilance", { vowelEvents: 0 });
    run.eq(by(noVowel).sib.ref, null, "没有元音可比（vowelEvents 为 0）：不显示参考，比值也不是测量值");

    const over = by(SCENES.overReference());
    run.eq(
        [over.plosive.ref.over, over.plosive.tip, over.sib.ref.over, over.sib.tip, over.mouth.ref.over, over.mouth.tip],
        [
            true,
            { id: "pop-filter-or-back-off", needsRec: true },
            true,
            { id: "angle-soften-de-ess", needsRec: false },
            true,
            { id: "back-off-a-little", needsRec: true },
        ],
        "超过参考值：各自的卡上有提示，建议键取自 advice",
    );
    run.eq(over.mouth.ref.vars.v, "6", "口水音有参考值时显示每分钟次数的参考");
    run.eq(headKey(SCENES.overReference()), "report.fx.ok.h", "三条提示都不进“最该改”：顶部仍是“可以开始录音了”");
    run.eq(
        by(SCENES.overReferenceHeadset()).plosive.tip.id + "|" + by(SCENES.overReferenceHeadset()).mouth.tip.id,
        "pop-filter|back-off-a-little.nodist",
        "耳机麦没有推荐距离：带“退后一点”的提示只说不涉及距离的部分",
    );
    const water = { ...SCENES.overReference(), advice: { ...SCENES.overReference().advice, click: "drink-water" } };
    run.eq(by(water).mouth.tip, { id: "drink-water", needsRec: false }, "口水音：喝点水");
    const stale = by(staled(SCENES.overReference(), "position"));
    run.eq(
        [stale.plosive.tip, stale.plosive.ref.over, stale.sib.tip],
        [null, false, null],
        "过期的结果：不比、不提示（建议只看当前的结果）",
    );
    const retest = by(
        withSection(SCENES.overReference(), "plosive", { reliability: "retest", retestReason: "too-short" }),
    );
    run.eq([retest.plosive.tip, retest.plosive.ref.over], [null, false], "要再测的结果：不提示");
    const o = SCENES.overReference();
    const withPrev = { ...o, previous: { plosive: withResult("plosive", { worstRatioDb: 11, overReference: true }) } };
    run.eq(
        core.cardOf("plosive", withPrev, withPrev.previous).tip,
        null,
        "上次（previous）的卡不带建议，建议只属于当前结果",
    );
    run.eq(
        core.cardsOf(withPrev).find((c) => c.id === "plosive").prev,
        { a: "+11.0", b: "+10.3" },
        "喷麦：上次 → 现在，只显示数值",
    );
    const sibPrev = (patch) => ({
        ...SCENES.ready(),
        steps: { ...SCENES.ready().steps, position: { ...SCENES.ready().steps.position, runs: 2 } },
        previous: { sibilance: withResult("sibilance", patch) },
    });
    const sibCard = (st) => core.cardsOf(st).find((c) => c.id === "sib");
    run.eq(sibPrev({ highOverVowelDb: -2.5 }).previous.sibilance.vowelEvents > 0, true, "上次也能和元音比");
    run.eq(
        sibCard(sibPrev({ highOverVowelDb: -2.5, highDb: -21 })).prev,
        { a: "−2.5", b: "−6.0" },
        "齿音：上次 → 现在比卡上的主数字，比元音",
    );
    run.eq(
        sibCard(sibPrev({ highDb: -21, highBandDisabled: true, midHighDb: -15 })).prev,
        undefined,
        "齿音：两次的主数字不是同一个量（一次比元音、一次只有 3-6 kHz 的电平），不写上次 → 现在",
    );
    const bothNarrow = withSection(sibPrev({ highBandDisabled: true, midHighDb: -15 }), "sibilance", {
        highBandDisabled: true,
        effectiveBandwidthHz: 8000,
    });
    run.eq(sibCard(bothNarrow).prev, { a: "−15.0", b: "−12.0" }, "齿音：两次都只有 3-6 kHz，比 3-6 kHz 的电平");
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

// ---- ④ 距离卡 ----
{
    const m = (name) => core.distanceModel(SCENES[name]());
    const first = m("maybeTooFar");
    run.eq(
        [first.st, first.kind, first.hint, first.ghost, first.pos, first.signs.length],
        ["fresh", "maybe-too-far", true, null, null, 0],
        "第一次：只有“可能离远了”的弱提示，没有标尺、没有三项读数",
    );
    const near = m("closer");
    run.eq(
        [near.kind, near.hint, near.ghost, near.read.key, near.read.vars.n],
        ["closer", false, 0.5, "flow.pr.e.closer", 2],
        "重测后更近：上次的位置是中间的虚影，句子写几项指向更近",
    );
    run.check(near.pos < near.ghost && m("farther").pos > m("farther").ghost, "更近的标记在虚影左边，更远的在右边");
    run.check(m("farther").pos - 0.5 > 0.5 - near.pos, "指向更远的项数越多，离虚影越远（3 项比 2 项远）");
    run.eq([m("noChange").pos, m("noChange").ghost], [0.5, 0.5], "没有明显变化：标记和虚影重合");
    run.eq(
        [m("conflicting").kind, m("conflicting").ghost, m("conflicting").pos],
        ["conflicting", 0.5, null],
        "几项指向不同方向：只有虚影，不画现在的标记",
    );
    run.eq(
        near.signs.map((x) => [x.key, x.sign, x.delta]),
        [
            ["dr", "closer", "+4.1"],
            ["plosive", "closer", "+7.5"],
            ["sib", "gain-changed", null],
        ],
        "三项读数各写自己的变化：D/R、喷麦、齿音；比得出来的带“现在减上次”，调过增益的齿音不比、不带数",
    );
    const noDelta = core.distanceModel(
        withSection(SCENES.closer(), "distance", { drDeltaDb: Number.NaN, plosiveDeltaDb: undefined }),
    );
    run.eq(
        noDelta.signs.map((x) => [x.sign, x.delta]),
        [
            ["closer", null],
            ["closer", null],
            ["gain-changed", null],
        ],
        "原生说更近却缺差值（NaN 或没有）：只写文字，不写 NaN",
    );
    run.eq(
        m("farther").signs.map((x) => x.delta),
        ["−3.6", "−8.2", "−6.0"],
        "挪远：差值带负号（U+2212）",
    );
    run.eq(
        [m("ready").signs.length, m("ready").kind, m("ready").hint],
        [0, "unknown", false],
        "没比过：不列三项读数，也没有弱提示",
    );
    const noEv = core.distanceModel(withSection(SCENES.closer(), "distance", { closeEvidence: 0 }));
    run.eq(
        [noEv.kind, noEv.read.key, noEv.pos],
        ["unknown", "flow.pr.e.unknown", null],
        "更近但项数不到 1：按还不够判断写，不画标记",
    );
    run.eq(core.distanceModel(staled(SCENES.closer(), "position")).st, "stale", "位置过期：灰");
    run.eq(core.distanceModel(staled(SCENES.closer(), "position")).pos, null, "位置过期：不画标记");
    const retest = core.distanceModel(
        withSection(SCENES.closer(), "distance", { reliability: "retest", retestReason: "ambiguous" }),
    );
    run.eq([retest.st, retest.pos, retest.signs.length], ["low", null, 0], "原生要再测：灰，没有标记");
    const none = core.distanceModel(buildSnapshot());
    run.eq([none.st, none.read.key], ["none", "report.dist.none"], "没测位置：写“还没测”");
    const old = core.distanceModel(withSection(SCENES.closer(), "distance", { kind: "too-close" }));
    run.eq([old.kind, old.pos], ["unknown", null], "旧的原生端的 too-close：不认识，按还不够判断写");
    run.eq(JSON.stringify(core.distanceModel(SCENES.closer())).match(/厘米/), null, "距离卡的标尺与读数不写距离数值");
    run.eq([m("closer").rec.vars.a, m("overReferenceHeadset").rec], ["15", null], "推荐距离取自快照；耳机麦没有");
    // 房间读数要再测：原生的 band 仍可能写着 normal，卡上不写档位也不写数
    const room = core.cardsOf(SCENES.lowConfidence()).find((c) => c.id === "room");
    run.eq(
        [room.word, room.nums.length, room.retest, room.tone],
        ["flow.w.unk", 0, true, "gray"],
        "房间读数要再测：卡上不写档位，也不写 T20",
    );
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
    const front = { word: "flow.dir.front" };
    const back = { word: "flow.dir.back" };
    run.eq(
        [m.cmd.key, m.cmd.vars.a, m.cmd.vars.b, m.words, m.wordsVars],
        ["flow.rm.dir.confirm", front, back, "flow.rm.dir.pair", { a: front, b: back }],
        "方向确认的测试中视图按叫法点出正面朝你与背面朝你",
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
for (const k of ["flow.cd.sib.bw", "flow.cd.sib.vowel", "flow.cd.sib.level", "flow.cd.mouth.note", "flow.dir.ring"]) {
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
    const UNGRADED = ["dr", "plosive", "sib", "mouth"];
    const greenUngraded = (c) => UNGRADED.includes(c.id) && (c.tone === "green" || c.word !== "flow.w.ungraded");
    const good = core.cardsOf(SCENES.ready());
    run.eq(good.some(greenUngraded), false, "正例：未分档的卡不是绿色，词也不是档位词");
    for (const id of UNGRADED) {
        const colored = good.map((c) => (c.id === id ? { ...c, tone: "green" } : c));
        run.eq(colored.some(greenUngraded), true, `反例：把${id}卡着成绿色会被查出`);
    }
    const worded = good.map((c) => (c.id === "plosive" ? { ...c, word: "flow.w.untested" } : c));
    run.eq(worded.some(greenUngraded), true, "反例：喷麦卡写了别的词（档位词）会被查出");
    const wrongRoom = {
        ...SCENES.veryBoomy(),
        advice: { room: "unknown", distance: "none", directionPrompt: "none" },
    };
    run.check(core.fixModel(wrongRoom).tone !== "green", "反例：fixes 说要改房间，advice 缺失时也不能给绿色");
    run.check(
        core.fixModel({ ...SCENES.ready(), fixes: ["gain"] }).tone !== "green",
        "反例：fixes 非空就不会是“可以录”",
    );
}

run.finish();
