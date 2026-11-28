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
const { BANDS, buildSnapshot, PLAN_SEC, recommendedDistanceOf } = await at("web-preview/mock/snapshot.js");
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
    t20BandsHz: T("reverb", "bands_hz"),
    drResolutionDb: T("distance", "dr_resolution_db"),
    plosiveResolutionDb: T("distance", "plosive_resolution_db"),
    sibilanceResolutionDb: T("distance", "sibilance_resolution_db"),
    plosiveReferenceDb: T("plosive", "reference_max_db"),
    sibilanceReferenceDb: T("sibilance", "reference_max_db"),
    clickReferencePerMin: T("click", "reference_max_per_min"),
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
// 接通的状态只看信号判断：接通观察每做完一轮都把接通记成 done，不管听到的是什么
const sig = (s, signalState, connectStatus = "done") => {
    s.connect.signalState = signalState;
    s.steps.connect.status = connectStatus;
    return s;
};
run.eq(flow.initialView(snap()), "connect", "没有任何结果时落在接通页");
run.eq(flow.initialView(sig(snap(), "ok")), "connect", "接通通过了、后面一步都没做：仍落在接通页");
run.eq(flow.initialView(sig(done(snap(), ["gain", "quiet"]), "ok")), "room", "落在第一个没做完的步骤");
run.eq(
    flow.initialView(sig(done(snap(), ["gain"]), "very-low-noise")),
    "quiet",
    "信号极低（不挡开始）也算通过：落在第一个没做完的步骤",
);
for (const bad of ["all-zero-from-start", "all-zero-after-signal", "no-callback", "waiting"]) {
    run.eq(flow.initialView(sig(snap(), bad)), "connect", `接通做完一轮但信号是 ${bad}：再开窗落在接通页`);
    run.eq(
        flow.initialView(sig(done(snap(), ["gain", "quiet"]), bad)),
        "connect",
        `做过几步、信号现在是 ${bad}：再开窗也落在接通页`,
    );
}
const connectState = (signalState, connectStatus) => flow.stepState(sig(snap(), signalState, connectStatus), "connect");
run.eq(connectState("ok"), "done", "步骤条：信号正常的接通打勾");
run.eq(connectState("very-low-noise"), "done", "步骤条：信号极低也算通过");
run.eq(connectState("ok", "idle"), "done", "步骤条：信号正常就算通过，不看接通步自己的状态");
for (const bad of ["all-zero-from-start", "all-zero-after-signal", "no-callback"])
    run.eq(connectState(bad), "fail", `步骤条：${bad} 是失败，不打勾`);
run.eq(connectState("waiting", "idle"), "todo", "步骤条：还没听到声音是未做");
run.eq(
    flow.stepState(sig(done(snap(), ["gain"]), "waiting", "idle"), "connect"),
    "todo",
    "步骤条：后面的步骤做过，接通也不因此打勾",
);
const connectSum = (signalState, device = {}, c = {}) => {
    const s = sig(snap(), signalState);
    Object.assign(s.device, device);
    Object.assign(s.connect, c);
    return flow.peekSegments(s, "connect").map((g) => g.key);
};
run.eq(connectSum("ok"), ["flow.sum.connect"], "接通摘要：信号正常");
run.eq(connectSum("all-zero-from-start"), ["flow.dg.zero.t"], "接通摘要：全零时写诊断，不写信号正常");
run.eq(connectSum("all-zero-after-signal"), ["flow.dg.zero2.t"], "接通摘要：先有后零");
run.eq(connectSum("no-callback"), ["flow.dg.nocb.t"], "接通摘要：没收到音频");
run.eq(connectSum("very-low-noise"), ["flow.dg.low.t"], "接通摘要：信号极低");
run.eq(connectSum("waiting"), ["flow.cn.wait"], "接通摘要：还没听到声音");
run.eq(
    connectSum("ok", { layout: "stereo" }, { channelPrompt: "one-silent" }),
    ["flow.dg.st1.t"],
    "接通摘要：立体声一路无声写声道提示",
);
run.eq(
    connectSum("ok", { layout: "stereo" }, { channelPrompt: "identical" }),
    ["flow.sum.connect"],
    "接通摘要：两路相同不算问题，写信号正常",
);
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
run.eq(flow.remoteModel(running("connect", {})), null, "接通观察不进测试中视图");
run.eq(
    flow.remoteModel(running("connect", {}, {}), { justFinished: "gain" })?.struct,
    "success",
    "成功屏期间后台开了一轮接通观察：成功屏照常显示",
);
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
run.check(!pos("soft-clap").slots, "自动推进的轻拍按遍画点，不画逐下的槽位");
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

// ---- 手动分段：待开始给“开始”、不倒数；进行中给“结束”和还差什么；够了写“够了”；重来退回待开始 ----
{
    const wait = flow.remoteModel(
        running("position", { phase: "plosive", round: 1, rounds: 1, waiting: true, need: "syllables", needLeft: 6 }),
    );
    run.check(
        wait.go === true && wait.count === null && !wait.end && wait.struct === "position:plosive:wait",
        "待开始：有“开始”，不倒数，没有“结束”",
    );
    run.check(!!wait.words && wait.sub2.key === "flow.rm.wait", "待开始：先给出念的字，再请用户点开始");
    const NEEDS = {
        seconds: ["quiet", "flow.rm.need.seconds"],
        "voice-seconds": ["gain", "flow.rm.need.voice"],
        syllables: ["position", "flow.rm.need.syllables"],
    };
    for (const [need, [step, key]] of Object.entries(NEEDS)) {
        const m = flow.remoteModel(
            running(step, {
                phase: step === "position" ? "plosive" : "none",
                need,
                needLeft: 2.2,
                canEnd: false,
                phaseDurationSec: 8,
                phaseElapsedSec: 1,
            }),
        );
        run.check(
            m.end && m.end.can === false && m.need.key === key && m.need.vars.n === 3 && m.count === 7,
            `进行中（${need}）：“结束”不可用，还差的量向上取整，倒计时照旧`,
        );
    }
    const ready = flow.remoteModel(running("gain", { need: "voice-seconds", needLeft: 0, canEnd: true }));
    run.check(ready.end.can === true && ready.need.key === "flow.rm.need.ready", "收够之后：“结束”可用，写“够了”");
    run.eq(
        pos("plosive", { rounds: 1, need: "syllables", needLeft: 0, canEnd: true }).need.key,
        "flow.rm.need.ready",
        "字表不会自己结束：收够了仍写“够了”",
    );
    const soft = pos("soft-clap", { rounds: 1, need: "claps", needLeft: 2 });
    run.check(!soft.dots && soft.aux.key === "flow.rm.part", "手动分段的轻拍是一段：不画遍数的点，写第几部分");
    run.check(
        pos("plosive", { rounds: 1, need: "syllables", needLeft: 6 }).aux.key === "flow.rm.part",
        "手动分段的字表是一段：写第几部分，不写第 1 遍 / 共 1 遍",
    );
    const room = flow.remoteModel(running("room", { ...roomLive, need: "claps", needLeft: 1 }));
    run.check(!room.aux, "手动分段的房间步：不另写还差几下");
    run.eq(room.need, { key: "flow.rm.count", vars: { n: 2, total: 4 } }, "房间步只写有效几下 / 凑够几下自动结束");
    const room3 = flow.remoteModel(
        running("room", { ...roomLive, claps: ["valid", "valid", "noisy", "valid"], need: "claps", canEnd: true }),
    );
    run.check(
        room3.end.can === true && room3.need.key === "flow.rm.count" && room3.need.vars.n === 3,
        "房间步第 3 下有效：“结束”可以按，但只写计数，不写“够了”（第 4 下就自动结束）",
    );

    // 手动分段的轻拍：一直听，每一下都有反馈，凑够 tapsNeeded 下有效自己往下走
    const tap = (extra) => pos("soft-clap", { rounds: 1, need: "claps", needLeft: 2, tapsNeeded: 3, ...extra });
    const tap0 = tap({ taps: [] });
    run.eq(tap0.slots, ["wait", "wait", "wait"], "轻拍刚开始：画出要凑的三下");
    run.check(tap0.slotsKind === "taps" && tap0.cmd.vars.n === 3, "轻拍的槽位单独排版，要拍几下取自 tapsNeeded");
    run.eq(tap0.need, { key: "flow.rm.count", vars: { n: 0, total: 3 } }, "轻拍写有效 0 / 3");
    const tap2 = tap({ taps: ["clipped", "valid", "unmeasured"] });
    run.eq(
        tap2.slots,
        ["clip", "valid", "miss", "wait", "wait"],
        "削波的一下写“拍轻一点”，量不出的写“没量出”，都不算数",
    );
    run.eq(tap2.need.vars, { n: 1, total: 3 }, "只数有效的");
    const tapReady = tap({ taps: ["valid", "valid"], canEnd: true, needLeft: 0 });
    run.check(
        tapReady.end.can === true && tapReady.need.key === "flow.rm.count",
        "轻拍收够两下：“结束”能按，仍只写计数",
    );
    run.eq(
        flow.tapSlots({ taps: Array(10).fill("clipped"), tapsNeeded: 3 }),
        [...Array(5).fill("clip"), "wait", "wait", "wait"],
        "一行最多 8 个：拍多了只留最近的几下，要凑的几下总在",
    );
    const few = flow.remoteModel(
        running("position", { phase: "soft-clap", notice: "redo", waiting: true, need: "claps", tapsNeeded: 3 }),
    );
    run.check(
        few.cmd.key === "flow.rm.clap.again" && few.sub.key === "flow.rm.clap.few" && few.go === true,
        "轻拍到上限有效的不够：请用户再拍一组，说明原因，等“开始”",
    );
    run.eq(
        flow.remoteModel(running("gain", { notice: "redo", waiting: true, need: "voice-seconds" })).cmd.key,
        "flow.rm.redo.gen",
        "别的部分的 redo 仍用通用的“要重来”",
    );
    const redo = flow.remoteModel(
        running("position", { phase: "soft-clap", notice: "clap-clipped", noticeSec: 0, waiting: true, need: "claps" }),
    );
    run.check(
        redo.tone === "red" && redo.go === true && redo.count === null && redo.sub2.key === "flow.rm.wait.redo",
        "手动分段的重来：红色，有“开始”，不倒数",
    );
    run.eq(redo.struct, "notice:clap-clipped:wait", "手动分段的重来：结构与倒数的重来不同（换按钮时重建）");
    const auto = flow.remoteModel(running("gain", { phaseDurationSec: 8, phaseElapsedSec: 3 }));
    run.check(!auto.go && !auto.end && !auto.need, "自动推进（need 是 none）：没有开始、结束");
    run.eq(flow.manualPhase({ need: "none", waiting: false }), false, "need 是 none 且不在等：自动推进");
}

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
// 电流声的说明以“高出本底 X dB”为主，占噪声能量的百分比放在括号里作参考
{
    const strings = (await at("web/app/flow/flow-strings.js")).default;
    for (const [slight, key] of [
        [false, "flow.qr.hum.e"],
        [true, "flow.qr.humweak.e"],
    ]) {
        const m = R("quiet", ["quiet"], (s) => {
            s.results.hum = { ...s.results.hum, kind: "hum", slight, overFloorDb: 9.4, energySharePct: 18 };
        });
        run.eq(
            [m.expl.key, m.expl.vars.v, m.expl.vars.p],
            [key, meter.fmtDb(9.4), 18],
            `${key}：带上高出本底的 dB 与占比`,
        );
        for (const lang of ["zh", "en"]) {
            const text = strings[lang][key];
            run.check(
                text.includes("{v}") && text.includes("{p}") && text.indexOf("{v}") < text.indexOf("{p}"),
                `${key}（${lang}）：先写高出本底的数，占比在后面作参考`,
            );
        }
    }
}
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

// 位置结果页：四张卡都不分档；距离只有前后对比与一条弱提示；推荐距离取自快照
{
    const dist = (kind, patch = {}, advice = {}) =>
        R("position", ["position"], (s) => {
            s.results.distance = { ...s.results.distance, kind, ...patch };
            s.advice = { ...s.advice, ...advice };
        });
    const kinds = ["unknown", "conflicting", "maybe-too-far", "closer", "farther", "no-change"];
    run.eq(
        kinds.map((k) => dist(k, { closeEvidence: 2, farEvidence: 2 }).expl.key),
        kinds.map((k) => `flow.pr.e.${k}`),
        "位置结果页：每一种距离情形各有一句说明",
    );
    run.eq(
        [dist("closer", { closeEvidence: 2 }).expl.vars.n, dist("farther", { farEvidence: 1 }).expl.vars.n],
        [2, 1],
        "更近、更远：写指向它的项数，取自快照",
    );
    run.eq(dist("no-change").acts, [], "没有明显变化：不给动作");
    run.eq(
        dist("maybe-too-far", {}, { distance: "move-closer" }).acts.map((a) => a.key),
        ["flow.pr.a.closer"],
        "可能离远了：动作跟着原生侧的 move-closer",
    );
    run.eq(dist("maybe-too-far").acts, [], "不给 move-closer 时界面不自己加动作");
    run.eq(
        R("position", ["position"], (s) => {
            s.results.plosive = { ...s.results.plosive, overReference: true };
            s.advice = { ...s.advice, plosive: "constructor", sibilance: "__proto__" };
        }).acts,
        [],
        "原生端给了原型上的名字当建议：当作没有建议，不报错",
    );
    run.eq(
        ["closer", "farther", "no-change"].map((k) => dist(k, {}, { distance: "move-closer" }).acts.length),
        [0, 0, 0],
        "move-closer 只在“可能离远了”时出现：别的情形即使收到也不加动作",
    );
    run.eq(
        [
            dist("closer", { closeEvidence: undefined }).expl.key,
            dist("farther", { farEvidence: Number.NaN }).expl.key,
            dist("farther", { farEvidence: 0 }).expl.key,
            dist("closer", { closeEvidence: 1 }).expl.key,
        ],
        ["flow.pr.e.unknown", "flow.pr.e.unknown", "flow.pr.e.unknown", "flow.pr.e.closer"],
        "更近、更远的项数缺失、不是数或为 0：按“还不够判断”写，不露出占位符，也不写“0 项指向它”；1 项起照写",
    );
    // 超过参考值的轻提示按卡的顺序列在动作里；各项建议只来自 advice，不影响距离那一格
    const tips = R("position", ["position"], (s) => {
        s.results.plosive = { ...s.results.plosive, overReference: true };
        s.results.click = { ...s.results.click, overReference: true };
        s.advice = { ...s.advice, plosive: "pop-filter-or-back-off", click: "drink-water" };
    });
    run.eq(
        tips.acts.map((a) => a.key),
        ["flow.tip.pop-filter-or-back-off", "flow.tip.drink-water"],
        "位置结果页：喷麦、口水音的轻提示按卡的顺序列在动作里",
    );
    const both = R("position", ["position"], (s) => {
        s.advice = { ...s.advice, distance: "move-closer", sibilance: "angle-soften-de-ess" };
        s.results.distance = { ...s.results.distance, kind: "maybe-too-far" };
    });
    run.eq(
        both.acts.map((a) => a.key),
        ["flow.pr.a.closer", "flow.tip.angle-soften-de-ess"],
        "可能离远了的动作在前，轻提示在后",
    );
    run.eq(
        R("position", ["position"], (s) => {
            s.results.distance = { ...s.results.distance, kind: "too-close" };
        }).expl.key,
        "flow.pr.e.unknown",
        "不认识的距离情形（旧的原生端）：按“还不够判断”写，不留键名",
    );
    run.eq(
        R("position", ["position"], (s) => {
            delete s.results.distance;
        }).expl.key,
        "flow.pr.e.unknown",
        "没有距离结果：按“还不够判断”写",
    );
    const p = R("position", ["position"]);
    run.eq(
        p.cards.map((c) => [c.id, c.word, c.tone]),
        ["dr", "plosive", "sib", "mouth"].map((id) => [id, "flow.w.ungraded", "none"]),
        "位置：四张卡都是“未分档”、无色",
    );
}

// 推荐距离：数值全取自快照，耳机麦没有
{
    const rd = (patch) => flow.recDistance(snap(patch));
    run.eq(
        rd({}),
        { key: "flow.rd.r", vars: { a: "15", min: "10", max: "30" } },
        "电容、USB、不确定：约一个数，再写不小于、不大于",
    );
    run.eq(
        rd({ micType: "dynamic", recommendedDistance: recommendedDistanceOf("dynamic") }),
        { key: "flow.rd", vars: { a: "5–10" } },
        "动圈：约一段，范围与它相同时不重复写",
    );
    run.eq(rd({ micType: "headset", recommendedDistance: null }), null, "耳机麦（null）：不显示");
    run.eq(rd({ recommendedDistance: undefined }), null, "旧的原生端没有这一项：不显示");
    run.eq(
        rd({ recommendedDistance: { aroundLowCm: 20, aroundHighCm: 20, minCm: 12, maxCm: 40 } }),
        { key: "flow.rd.r", vars: { a: "20", min: "12", max: "40" } },
        "数值随快照走，不是写死的 15/10/30",
    );
    run.eq(
        rd({ recommendedDistance: { aroundLowCm: 15, aroundHighCm: 15, minCm: 10 } }),
        null,
        "字段不全（缺 maxCm）：不显示，不写 undefined",
    );
    run.eq(flow.prepModel(snap(), "position").rec, rd({}), "位置步的准备页带推荐距离");
    run.eq(flow.prepModel(snap(), "gain").rec, null, "其他步的准备页没有推荐距离");
    run.eq(
        flow.prepModel(snap({ micType: "headset", recommendedDistance: null }), "position").rec,
        null,
        "耳机麦：准备页不显示",
    );
    run.eq(R("position", ["position"]).rec, rd({}), "位置结果页带推荐距离");
}

// 叠卡摘要：位置步写读数，不写档位词
{
    const sum = flow.peekSegments(done(snap(), ["position"]), "position").map((g) => [g.key, g.vars.n]);
    run.eq(
        sum,
        [
            ["flow.sum.dr", "+3.8"],
            ["flow.sum.plosive", "+2.0"],
            ["flow.sum.sib", "−6.0"],
            ["flow.sum.mouth", "3"],
        ],
        "位置摘要：D/R、喷麦、齿音、口水音只写读数；齿音写卡上的主数字，6-12 kHz 比元音",
    );
    const narrow = done(snap(), ["position"]);
    narrow.results.sibilance = { ...narrow.results.sibilance, highBandDisabled: true };
    const seg = flow.peekSegments(narrow, "position")[2];
    run.eq([seg.key, seg.vars.n], ["flow.sum.sib.lv", "−12.0"], "高频项关闭：齿音摘要写 3-6 kHz 的电平");
}
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
    // 有读数但原生要再测（confidence 为 retest）：band 仍可能写着 normal，不写档位、不写房间建议
    const unsure = done(snap(), ["room"]);
    unsure.results.reverb = {
        ...unsure.results.reverb,
        band: "normal",
        tMidSec: 0.35,
        gated: false,
        reliability: "retest",
        retestReason: "low-pnr",
    };
    const um = flow.resultModel(unsure, "room");
    run.eq(
        [um.cards[0].word, um.cards[0].nums.length, um.cards[0].retest, um.cards[0].why, um.expl.key],
        ["flow.w.unk", 0, true, "flow.cf.why.tail", "flow.rf.e.retest"],
        "房间读数要再测：卡上不写档位与 T20，写原因，说明句只说不够确定",
    );
    run.check(
        !um.acts.some((a) => a.key === "flow.rr.a.ok") && um.ctas[0].retest === "room",
        "房间读数要再测：不给“房间正常”的动作，主按钮重测房间",
    );
    const unsure2 = done(snap(), ["room"]);
    unsure2.results.reverb = { ...unsure2.results.reverb, reliability: "retest", retestReason: "ambiguous" };
    run.eq(
        flow.resultModel(unsure2, "room").cards[0].why,
        "flow.cf.why.unsure",
        "原生要再测但没给我们认识的原因：写通用的“读数不够确定”，不编具体原因",
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
lv = meter.advance(lv, -8, 16);
run.check(lv.db === -8 && lv.peakDb === -8, "液面瞬时升到峰值，峰值线跟着");
const fall = meter.advance(lv, -80, 100);
run.check(fall.db < -8 && fall.db > -80 && fall.peakDb === -8, "回落有速度，峰值保持");
const held = meter.advance({ ...fall, heldMs: 3000 }, -80, 100);
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

// ---- 流程模拟：手动分段（缺省）与自动推进 ----
{
    const s = snap();
    const sim = createFlowSim(s, { push: () => {} });
    sim.begin("position");
    run.check(!s.live.waiting && s.live.need === "claps" && !s.live.canEnd, "模拟·手动：第一段随开始直接开始");
    run.eq(sim.endPhase(), "not-enough", "模拟·手动：没收够不能结束");
    run.eq(sim.startPhase(), "no-phase", "模拟·手动：进行中不能再开始");
    sim.advance(8);
    run.eq(s.live.taps, ["clipped", "valid", "unmeasured", "valid"], "模拟·手动：轻拍每一下都有判定");
    run.check(s.live.canEnd && s.live.needLeft === 0, "模拟·手动：收够之后可以结束");
    sim.advance(2);
    run.check(
        s.live.waiting && s.live.phase === "plosive" && s.live.rounds === 1 && s.live.taps.length === 0,
        "模拟·手动：轻拍凑够三下有效自己往下走，下一段（字表合成一段）等开始",
    );
    const before = s.steps.position.progressSec;
    sim.advance(30);
    run.check(s.live.waiting && s.steps.position.progressSec === before, "模拟·手动：待开始时不推进");
    run.eq(sim.startPhase(), "ok", "模拟·手动：开始下一段");
    sim.advance(7);
    run.eq(sim.endPhase(), "ok", "模拟·手动：结束");
    run.check(s.live.waiting && s.live.phase === "sibilant", "模拟·手动：结束后下一段等开始");
    sim.startPhase();
    sim.advance(20);
    run.check(s.live.waiting && s.live.phase === "natural", "模拟·手动：到上限自动结束");
    sim.cancel();
    // 轻拍到上限有效的不够：退回待开始重拍；削波的几下本来够数时说“拍轻一点”
    for (const [taps, notice] of [
        [["valid"], "redo"],
        [["clipped", "valid"], "clap-clipped"],
    ]) {
        sim.setTaps(taps);
        sim.begin("position");
        sim.advance(20);
        run.check(
            s.live.waiting && s.live.phase === "soft-clap" && s.live.notice === notice && s.live.taps.length === 0,
            `模拟·手动：轻拍到上限有效的不够，退回待开始重拍（${notice}）`,
        );
        sim.cancel();
    }
    const a = snap();
    const auto = createFlowSim(a, { push: () => {} }, { phases: "auto" });
    auto.begin("position");
    run.eq(auto.endPhase(), "no-phase", "模拟·自动：没有可以结束的段");
    auto.advance(6);
    run.check(!a.live.waiting && a.live.phase === "plosive" && a.live.need === "none", "模拟·自动：段与段首尾相接");
    // 接通是一轮观察窗：按时长结束；setInput 给了信号状态才写进快照
    const c = snap();
    const obs = createFlowSim(c, { push: () => {} });
    obs.begin("connect");
    obs.advance(5);
    run.eq(obs.runningStep(), "connect", "模拟·接通：一轮观察没到时长还在跑");
    obs.advance(1);
    run.check(obs.runningStep() === null && c.connect.signalState === "waiting", "模拟·接通：没给输入，信号状态不变");
    obs.setInput("ok");
    obs.begin("connect");
    obs.advance(6);
    run.eq(c.connect.signalState, "ok", "模拟·接通：给了输入，一轮结束时写进快照");
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
