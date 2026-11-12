// SPDX-License-Identifier: GPL-3.0-or-later
//
// 流程页与左栏输入卡的纯逻辑冒烟（不要浏览器）：
//   ① 假后端的档位边界与 thresholds.json 逐项相等（界面的区间、目标区、文案里的数都从快照来，所以占位值不能漂）；
//   ② 步骤表：耳机麦没有方向步；“轻拍 3 次”是位置步表里独立的一行，源码里只有这一处提到它，删掉这一行就是删掉它；
//   ③ 接通页诊断、测试中视图（含五种重来、成功屏）、拍手槽位、结果页的数据模型；
//   ④ 数字随快照走：改 bands，区间条、目标区和建议里的数跟着变；
//   ⑤ 左栏：弹道、信号药丸的优先级、结果标记；
//   ⑥ 反例：每一类检查都有一个会变红的坏样例。
//
// 用法：node web-preview/tests/smoke-flow-core.mjs [仓库根]

import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";
import { createRun, repoRoot } from "./lib/harness.mjs";
import { listFiles, readText } from "./lib/files.mjs";

const ROOT = repoRoot();
const run = createRun("流程页逻辑");
const at = (rel) => import(pathToFileURL(path.join(ROOT, rel)).href);
const flow = await at("web/app/flow/flow-core.js");
const meter = await at("web/app/meter/meter-core.js");
const { BANDS, buildSnapshot, PLAN_SEC } = await at("web-preview/mock/snapshot.js");
const { resultsFor } = await at("web-preview/mock/results.js");
const { createFlowSim } = await at("web-preview/mock/flow-sim.js");
const thresholds = JSON.parse(readText(ROOT, "thresholds.json"));

// ---- ① 假后端的档位边界 = thresholds.json ----
const T = (section, name) => thresholds[section][name].value;
const WANT = {
    tpTooQuietDbfs: T("gain", "tp_too_quiet_below_dbfs"),
    tpQuietDbfs: T("gain", "tp_quiet_below_dbfs"),
    tpLoudDbfs: T("gain", "tp_loud_above_dbfs"),
    tpTooLoudDbfs: T("gain", "tp_too_loud_above_dbfs"),
    targetPeakDbfs: T("gain", "target_peak_dbfs"),
    clipAdviceDropDb: T("gain", "clip_advice_drop_db"),
    snrBadBelowDb: T("noise", "snr_bad_below_db"),
    snrGoodAboveDb: T("noise", "snr_good_above_db"),
    t20DryBelowS: T("reverb", "dry_below_s"),
    t20NormalBelowS: T("reverb", "normal_below_s"),
    t20BoomyBelowS: T("reverb", "boomy_below_s"),
    drLowBelowDb: T("direct_ratio", "low_below_db"),
    drHighAboveDb: T("direct_ratio", "high_above_db"),
    t20BandsHz: T("reverb", "bands_hz"),
};
const sorted = (o) =>
    Object.fromEntries(
        Object.keys(o)
            .sort()
            .map((k) => [k, o[k]]),
    );
run.eq(sorted(BANDS), sorted(WANT), "假后端的档位边界应与 thresholds.json 同名项逐项相等");
run.eq(Object.keys(WANT).sort(), Object.keys(BANDS).sort(), "bands 的每一项都要在对拍表里");

// ---- 造快照的小工具 ----
const snap = (patch = {}) => ({ ...buildSnapshot(), ...patch });
const withSteps = (s, status) => {
    for (const [id, st] of Object.entries(status)) s.steps[id] = { ...s.steps[id], status: st };
    return s;
};
const done = (s, ids) => {
    for (const id of ids) {
        s.steps[id].status = "done";
        Object.assign(s.results, resultsFor(id));
    }
    return s;
};

// ---- ② 步骤表 ----
run.eq(flow.visibleSteps(snap()), flow.STEP_IDS, "默认显示全部 6 步");
run.eq(flow.visibleSteps(snap({ micType: "headset" })).includes("direction"), false, "耳机麦没有方向步");
run.eq(flow.initialView(snap()), "connect", "没有任何结果时落在接通页");
run.eq(flow.initialView(done(snap(), ["gain", "quiet"])), "room", "落在第一个没做完的步骤");
run.eq(flow.neighbour(snap(), "direction", 1), "report", "最后一步的下一页是报告");
run.eq(flow.neighbour(snap({ micType: "headset" }), "position", 1), "report", "耳机麦时位置步之后直接是报告");
run.eq(flow.neighbour(snap(), "connect", -1), null, "第一页没有上一页");

const clapRows = flow.POSITION_PHASES.filter((p) => p.id === "soft-clap");
run.eq(clapRows.length, 1, "位置步表里“轻拍 3 次”只有独立的一行");
// 界面代码（flow、meter）里只有位置步表一处；预览的流程模拟（web-preview/mock）模拟的是原生侧自己的阶段表，
// 不属于界面代码，但也扫进来，免得它悄悄多出一处而没人发现
const flowSrc = listFiles(ROOT, ["web/app/flow", "web/app/meter", "web-preview/mock"], [".js"])
    .filter((f) => !f.endsWith("-strings.js"))
    .map((f) => ({ f, src: readText(ROOT, f) }));
// 数“提到它的代码行”：一行表项里 id 和词条键各出现一次，只算一行；注释不算
const NL = String.fromCharCode(10);
const isComment = (l) => ["//", "*", "/*"].some((c) => l.trimStart().startsWith(c));
const codeLines = (src) => src.split(NL).filter((l) => !isComment(l));
const mentions = flowSrc.flatMap(({ f, src }) =>
    codeLines(src)
        .filter((l) => l.includes("soft-clap"))
        .map(() => f),
);
run.eq(
    [...mentions].sort(),
    ["web-preview/mock/flow-sim.js", "web/app/flow/flow-core.js"],
    "界面代码里只有位置步表那一行提到 soft-clap；预览模拟另有它自己的一行（模拟原生侧）",
);
run.eq(flow.prepModel(snap(), "position").how.length, flow.POSITION_PHASES.length, "准备页按表逐行列出阶段");

// ---- ③ 接通页诊断 ----
const conn = (c, device = {}) => {
    const s = snap();
    Object.assign(s.connect, c);
    Object.assign(s.device, device);
    return flow.connectModel(s);
};
run.eq(conn({ signalState: "waiting" }).mode, "first", "还没有声音是首次接通页");
run.eq(conn({ signalState: "ok" }).mode, "ok", "信号正常、单声道：正常接通页");
const nocb = conn({ signalState: "no-callback" });
run.check(
    nocb.mode === "nocb" && nocb.diag.tone === "red" && nocb.blocked && nocb.redBlock,
    "没收到音频：红色诊断，开始按钮不可用",
);
run.eq(conn({ signalState: "all-zero-from-start" }).mode, "zero", "全零（一开始）");
run.eq(conn({ signalState: "all-zero-after-signal" }).mode, "zero2", "全零（出过声之后）");
run.eq(conn({ signalState: "very-low-noise" }).diag.tone, "amber", "信号极低是琥珀色，仍可开始");
const st = { layout: "stereo" };
run.eq(conn({ signalState: "ok", channelPrompt: "one-silent" }, st).mode, "st1", "立体声一路无声");
run.eq(conn({ signalState: "ok", channelPrompt: "one-silent-host" }, st).mode, "st1fl", "宿主把单声道当立体声");
run.eq(conn({ signalState: "ok", channelPrompt: "antiphase" }, st).mode, "anti", "两路反相");
run.eq(conn({ signalState: "ok", channelPrompt: "identical" }, st).mode, "same", "两路完全相同");
run.eq(
    conn({ signalState: "ok", channelPrompt: "identical", channelPromptDismissed: true }, st).mode,
    "ok",
    "“不再提醒”之后回到正常接通页",
);
run.eq(conn({ signalState: "ok", channelPrompt: "one-silent" }).mode, "ok", "单声道时不显示声道提示");
run.eq(conn({ signalState: "ok" }, { offlineRender: true }).blocked, true, "宿主离线渲染时不能开始");

// ---- ③ 测试中视图 ----
const running = (id, live, steps = {}) => {
    const s = snap();
    s.steps[id].status = "running";
    Object.assign(s.steps[id], steps);
    Object.assign(s.live, live);
    return s;
};
run.eq(flow.remoteModel(snap()), null, "没有步骤在跑时没有测试中视图");
const gainRun = flow.remoteModel(running("gain", { phaseDurationSec: 8, phaseElapsedSec: 3 }));
run.check(
    gainRun.tone === "violet" && gainRun.count === 5 && gainRun.zone && gainRun.bar,
    "增益：紫色，倒计时来自阶段，显示目标区",
);
const success = flow.remoteModel(done(snap(), ["gain"]), { justFinished: "gain" });
run.check(success.tone === "green" && success.icon === "check", "刚完成：绿色成功屏");
const NOTICES = {
    spoiled: "quiet",
    "clap-clipped": "position",
    "too-quiet": "position",
    gated: "room",
    redo: "gain",
};
for (const [notice, step] of Object.entries(NOTICES)) {
    const m = flow.remoteModel(
        running(step, { notice, noticeSec: 3, phase: step === "position" ? "plosive" : "none" }),
    );
    run.check(
        m.tone === "red" && m.count === 3 && m.sub2 && m.struct === `notice:${notice}`,
        `重来（${notice}）：红色，带重新开始的倒计时`,
    );
}
const roomLive = {
    claps: ["valid", "too-soft", "valid", "noisy"],
    clapsNeeded: 4,
    clapsMax: 8,
    phaseDurationSec: 20,
    phaseElapsedSec: 8,
};
const roomRun = flow.remoteModel(running("room", roomLive));
run.eq(
    roomRun.slots,
    ["valid", "soft", "valid", "noisy", "wait", "wait", "wait", "wait"],
    "房间：槽位按每一下的判定，没拍的补成待拍",
);
run.eq(roomRun.aux.vars.n, 2, "房间：还差几下 = 需要的 − 有效的");
run.eq(
    flow.clapSlots({ claps: ["truncated", "valid"], clapsMax: 3 }),
    ["gate", "valid", "wait"],
    "被门限截断的一下显示为门限槽位",
);
const pos = (phase, extra = {}) =>
    flow.remoteModel(
        running("position", { phase, round: 1, rounds: 3, phaseDurationSec: 10, phaseElapsedSec: 2, ...extra }),
    );
run.check(pos("soft-clap").dots.total === 3, "位置·轻拍：三个点");
run.check(
    pos("plosive", { rounds: 2 }).words && pos("plosive", { rounds: 2 }).aux.key === "flow.rm.round",
    "位置·喷麦：念的字和第几遍",
);
run.check(pos("natural", { rounds: 1 }).aux.key === "flow.rm.part", "位置·自然说话：第几部分");
const dirRun = flow.remoteModel(
    running("direction", { phase: "direction", directionIndex: 1, rounds: 4, phaseDurationSec: 3, phaseElapsedSec: 1 }),
);
run.eq(dirRun.dots, { total: 4, on: 1, cur: 2 }, "方向：四个点，第 2 个进行中");
run.check(
    flow.remoteModel(running("direction", { phase: "confirm", phaseDurationSec: 6, phaseElapsedSec: 0 })).struct ===
        "direction:confirm",
    "方向·确认阶段",
);

// ---- ③ 结果页 ----
const R = (id, ids, edit) => {
    const s = done(snap(), ids);
    if (edit) edit(s);
    return flow.resultModel(s, id);
};
const gainOk = R("gain", ["gain"]);
run.check(
    gainOk.cards[0].tone === "green" && gainOk.ctas.length === 1 && gainOk.ctas[0].primary,
    "增益合适：只有“下一步”",
);
const gainClip = R("gain", ["gain"], (s) => {
    s.results.gain = { ...s.results.gain, digitalClip: true, band: "too-loud", tone: "red" };
});
run.check(gainClip.cards[0].note && gainClip.ctas[0].retest === "gain", "增益削波：带说明，主按钮是重测");
run.eq(
    R("quiet", ["quiet"]).cards.map((c) => c.id),
    ["noise", "hum"],
    "安静：底噪与电流声两张卡",
);
const roomOk = R("room", ["room"]);
run.eq(roomOk.viz.kind, "bands", "房间：区间条");
run.eq(roomOk.bars.length, BANDS.t20BandsHz.length, "房间：每个频段一根小条，频段来自 bands");
const roomFail = R("room", ["room"], (s) => {
    s.steps.room.status = "invalid";
    s.results.reverb = { ...s.results.reverb, gated: true, tMidSec: null };
    s.live.claps = ["valid", "too-soft", "truncated"];
    s.live.clapsNeeded = 4;
});
run.check(roomFail.viz.kind === "slots" && roomFail.cards[0].none, "房间失败：没有读数，显示拍手槽位");
const dirRes = R("direction", ["direction"], (s) => {
    s.results.direction = { ...s.results.direction, levelDb: [-6, -2, 0, -3], currentIndex: 1 };
});
run.eq(dirRes.viz.rel, [-2, 0, -3, -6], "方向雷达：把当前方向转到第一位，再取相对最响的差");
run.eq(R("position", ["position"]).cards.length, 4, "位置：四张结果卡");
run.eq(flow.resultModel(snap(), "gain"), null, "没有结果时结果页没有数据");

// 评审补的边界：旧的原生端没有 bands 或 live 时不进测试中视图；已完成的房间结果读数为空时摘要卡不崩；
// “有效拍手不够”的原因里要有数字（原因词条里有 {need} 占位符）
{
    const noLive = running("gain", {});
    delete noLive.live;
    run.eq(flow.remoteModel(noLive), null, "快照没有 live：不进测试中视图");
    const noBands = running("gain", {});
    delete noBands.bands;
    run.eq(flow.remoteModel(noBands), null, "快照没有 bands：不进测试中视图");
    const gated = done(snap(), ["room"]);
    gated.results.reverb = { ...gated.results.reverb, gated: true, tMidSec: null };
    let seg = null;
    let threw = false;
    try {
        seg = flow.peekSegments(gated, "room");
    } catch {
        threw = true;
    }
    run.check(!threw && seg.length === 1, "房间已完成但读数为空：摘要卡不抛错");
    const few = done(snap(), ["room"]);
    few.results.reverb = {
        ...few.results.reverb,
        gated: true,
        tMidSec: null,
        reliability: "retest",
        retestReason: "few-claps",
    };
    few.steps.room.status = "invalid";
    few.live.clapsNeeded = 4;
    const fm = flow.resultModel(few, "room");
    run.check(
        fm.cards[0].why === "flow.cf.why.fewclaps" && fm.cards[0].need === 4,
        "房间失败：原因里的“需要几次”取自 live.clapsNeeded",
    );
}

{
    const bare = snap();
    delete bare.bands;
    run.check(
        !flow.prepModel(bare, "gain").items.includes("flow.ms.target"),
        "快照没有 bands：增益准备页不列目标区一条",
    );
    run.check(flow.prepModel(snap(), "gain").items.includes("flow.ms.target"), "有 bands 时增益准备页列出目标区");
}

// ---- ④ 数字随快照走 ----
const alt = {
    ...BANDS,
    tpQuietDbfs: -20,
    tpLoudDbfs: -10,
    tpTooQuietDbfs: -30,
    tpTooLoudDbfs: -4,
    targetPeakDbfs: -15,
};
const viz1 = flow.gainViz(BANDS, -8.4);
const viz2 = flow.gainViz(alt, -8.4);
run.check(JSON.stringify(viz1.segs) !== JSON.stringify(viz2.segs), "改 bands，增益区间条的边界跟着变");
run.eq(viz2.segs.find((s) => s[3] === "flow.w.fit").slice(0, 2), [-20, -10], "“合适”一段正好是 bands 里的下限到上限");
const lowAt = (bands) => {
    const s = done(snap(), ["gain"]);
    s.bands = bands;
    s.results.gain = { ...s.results.gain, band: "quiet", tone: "amber", truePeakDbfsL: -14.6, truePeakDbfsR: -14.6 };
    return flow.resultModel(s, "gain").acts[0].vars.n;
};
run.check(lowAt(BANDS) !== lowAt(alt), "改目标峰值，“要调多少 dB”的数跟着变");
run.eq(flow.t20Viz(BANDS, 0.62).dom[0], 0, "T20 区间条从 0 起");
run.eq(flow.snrViz(alt, 40).segs.length, 3, "信噪比区间条三段");

// ---- ⑤ 左栏 ----
run.eq(meter.ratio(-80), 0, "刻度底是 −80 dB");
run.eq(meter.ratio(0), meter.ratio(0, -80) * 1, "0 dB 在刻度上的位置");
run.check(meter.ratio(6) === 1 && meter.ratio(Number.NaN) === 0, "超过上限画满，非数值当作底");
let lv = meter.rest();
lv = meter.advance(lv, -10, -8, 16);
run.check(lv.db === -10 && lv.peakDb === -8, "电平瞬时升到目标，峰值取更高者");
const fall = meter.advance(lv, -80, -80, 100);
run.check(fall.db < -10 && fall.db > -80 && fall.peakDb === -8, "回落有速度，峰值保持");
const held = meter.advance({ ...fall, heldMs: 3000 }, -80, -80, 100);
run.check(held.peakDb < -8, "保持时间过后峰值才回落");
run.eq(meter.fmtDb(-6.04), "−6.0", "负号用 U+2212");
run.eq(meter.fmtDb(-0.04), "0.0", "舍入到 0 不带负号");
run.eq(meter.targetZone(BANDS), { lo: -12, hi: -6 }, "目标区取自 bands");
run.eq(meter.targetZone(null), null, "没有 bands 就不画目标区");
const pill = (c, device) => {
    const s = snap();
    Object.assign(s.connect, c);
    Object.assign(s.device, device);
    return meter.signalPill(s);
};
run.eq(pill({ signalState: "no-callback" }).tone, "red", "药丸：没收到音频是红色");
run.eq(pill({ signalState: "very-low-noise" }).tone, "amber", "药丸：信号极低是琥珀色");
run.eq(pill({ signalState: "waiting" }).tone, "gray", "药丸：等你出声是灰色");
run.check(
    pill({ signalState: "ok", channelPrompt: "one-silent", analysedChannel: "right" }, st).act,
    "药丸：声道提示可点",
);
run.check(
    pill({ signalState: "no-callback", channelPrompt: "one-silent" }, st).tone === "red",
    "药丸：红色压过声道提示",
);
run.eq(pill({ signalState: "ok" }).tone, "green", "药丸：信号正常是绿色");
const marks = meter.markList(done(snap(), ["quiet", "position"]));
run.check(marks.length > 0 && marks.every((m) => Number.isFinite(m.mean ?? m.at ?? 0)), "结果标记：测完后管上有标记");
run.eq(meter.markList(snap()), [], "没有结果时没有标记");

// ---- 流程模拟：时间到了才出结果，取消回到空闲 ----
{
    const s = snap();
    let pushes = 0;
    const sim = createFlowSim(s, { push: () => pushes++ });
    run.eq(sim.begin("gain"), true, "模拟：开始增益");
    run.eq(sim.begin("quiet"), false, "模拟：已有步骤在跑时再开始回 busy");
    sim.advance(PLAN_SEC.gain - 1);
    run.eq(s.steps.gain.status, "running", "模拟：时间没到仍在跑");
    sim.advance(1);
    run.check(s.steps.gain.status === "done" && !!s.results.gain, "模拟：时间到了出结果");
    sim.begin("room");
    sim.advance(20);
    run.check(s.live.claps.length > 0, "模拟：房间步结束后拍手记录保留");
    sim.begin("position");
    sim.cancel();
    run.check(s.steps.position.status === "idle" && sim.runningStep() === null, "模拟：取消回到空闲");
    run.check(pushes > 0, "模拟：每次变化都推送");
}

// ---- ⑥ 反例：检查要能变红 ----
run.check(
    JSON.stringify({ ...BANDS, tpQuietDbfs: -13 }) !== JSON.stringify(WANT),
    "反例：边界漂一点就对不上 thresholds.json",
);
run.check(
    codeLines(["a soft-clap", "b soft-clap"].join(NL)).filter((l) => l.includes("soft-clap")).length !== 1,
    "反例：两行代码提到 soft-clap 会被查出",
);
run.check(codeLines(["// soft-clap", " * soft-clap"].join(NL)).length === 0, "正例：注释里提到 soft-clap 不算");
run.check(flow.visibleSteps(snap({ micType: "condenser" })).includes("direction"), "反例：非耳机麦应当有方向步");
run.check(
    flow.connectModel({ ...snap(), connect: { ...snap().connect, signalState: "ok" } }).diag === null,
    "反例：信号正常时不应有诊断",
);
run.check(fs.existsSync(path.join(ROOT, "web/app/flow/flow-core.js")), "自检：被测文件存在");

run.finish();
