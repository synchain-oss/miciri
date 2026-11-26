// SPDX-License-Identifier: GPL-3.0-or-later
//
// 预览用的流程模拟：只推进“进度、阶段、拍手判定”，不测量任何东西。
// 冒烟测试手动调 advance(秒) 精确推进；预览页（autorun=1）用定时器每秒调一次。
// 阶段表、时长和最低量只是占位，真实的步骤编排在原生侧的 Session。
//
// 两种分段，和插件一样（见 docs/contracts/BRIDGE.md 的“一段的开始与结束”）：
// - 手动开始（缺省，插件由界面驱动时就是这样）：第一段随 begin 开始，之后每段待开始（live.waiting），
//   startPhase 才开始；收够这一段的最低量（假的：按时间线性增长）后 endPhase 可以提前结束，到时长上限照样自动结束。
//   待开始时 advance 不推进。
// - 自动推进（phases: "auto"）：各段首尾相接，按时长推进，startPhase、endPhase 回 no-phase。

import { ENUM_STEP_ID } from "../../web/shared/bridge-api.js";
import { idleLive } from "./snapshot.js";
import { resultsFor } from "./results.js";

/** 位置步的阶段：[阶段名, 遍数, 每遍秒数, 手动开始时的最低量（见 NEED）]。 */
const POSITION_PHASES = [
    ["soft-clap", 3, 2, ["claps", 2, 0.6]],
    ["plosive", 2, 10, ["syllables", 6, 0.9]],
    ["sibilant", 2, 10, ["syllables", 6, 0.9]],
    ["natural", 1, 14, ["voice-seconds", 8, 0.7]],
];
/** 方向步：4 个方向各 3 秒，再确认一次。 */
const DIRECTION_PHASES = [
    ["direction", 4, 3],
    ["confirm", 1, 6],
];
const CLAP_PATTERN = ["valid", "too-soft", "valid", "noisy", "valid", "valid"];

/**
 * 手动开始时各段的最低量：[单位, 量, 每秒长多少]。和插件同样的单位；量取插件的起始值，增长速度是随便取的。
 * 房间步的拍手数按 CLAP_PATTERN 里有效的下数算；位置步各阶段的写在 POSITION_PHASES 里。
 */
const NEED = {
    gain: ["voice-seconds", 3, 0.7],
    quiet: ["seconds", 4, 1],
    room: ["claps", 3, 0],
    direction: ["voice-seconds", 1.5, 0.7],
    confirm: ["voice-seconds", 1.5, 0.7],
};

function phasesOf(id, duration) {
    if (id === "position") return POSITION_PHASES;
    if (id === "direction") return DIRECTION_PHASES;
    return [["none", 1, duration]];
}

/** 手动开始的段表：位置步每个阶段一段，方向步每个方向、确认每一遍一段。 */
function segmentsOf(id, duration) {
    const one = (phase, sec, need) => ({ phase, round: 1, rounds: 1, sec, need });
    if (id === "position") return POSITION_PHASES.map(([name, rounds, per, need]) => one(name, rounds * per, need));
    if (id === "direction") {
        const dirs = Array.from({ length: 4 }, (_, i) => ({
            phase: "direction",
            round: i + 1,
            rounds: 4,
            sec: 3,
            need: NEED.direction,
        }));
        const confirm = [1, 2].map((r) => ({ phase: "confirm", round: r, rounds: 2, sec: 3, need: NEED.confirm }));
        return [...dirs, ...confirm];
    }
    return [one("none", duration, NEED[id])];
}

/**
 * @param {object} snap 假快照（会被原地修改）
 * @param {{push: ()=>void}} io push：把快照推给界面
 * @param {{phases?: "manual"|"auto"}} [o]
 */
export function createFlowSim(snap, io, o = {}) {
    const manual = o.phases !== "auto";
    let running = null;
    let elapsed = 0;
    // 手动开始
    let segments = [];
    let seg = 0;
    let inSeg = 0;
    let waiting = false;
    let reached = false;

    // 做完一步：次数与证据版本各加 1（作废、取消不加，和真插件一样）
    const done = (id) => {
        snap.steps[id].status = "done";
        snap.steps[id].runs += 1;
        snap.steps[id].evidenceRev += 1;
    };

    const finish = (id) => {
        done(id);
        snap.steps[id].progressSec = snap.steps[id].durationSec;
        Object.assign(snap.results, resultsFor(id));
        // 房间步结束后拍手记录保留（结果页的失败说明和槽位小图要用）
        const claps = id === "room" ? snap.live.claps : [];
        snap.live = { ...idleLive(), claps, notice: "success", noticeSec: 0 };
        running = null;
    };

    const roomClaps = (sec) => CLAP_PATTERN.slice(0, Math.min(CLAP_PATTERN.length, Math.floor(sec / 2)));
    const validOf = (claps) => claps.filter((c) => c === "valid").length;

    const update = () => {
        const id = running;
        const step = snap.steps[id];
        step.progressSec = Math.min(elapsed, step.durationSec);
        const phases = phasesOf(id, step.durationSec);
        let rest = elapsed;
        for (const [name, rounds, perRound] of phases) {
            const total = rounds * perRound;
            if (rest < total || name === phases[phases.length - 1][0]) {
                const round = Math.min(rounds, Math.floor(rest / perRound) + 1);
                Object.assign(snap.live, {
                    phase: name,
                    round,
                    rounds,
                    phaseElapsedSec: rest - (round - 1) * perRound,
                    phaseDurationSec: perRound,
                    directionIndex: name === "direction" ? round - 1 : 0,
                });
                break;
            }
            rest -= total;
        }
        if (id === "room") {
            snap.live.claps = roomClaps(elapsed);
            if (validOf(snap.live.claps) >= snap.live.clapsNeeded) return finish(id);
        }
        if (elapsed >= step.durationSec) finish(id);
    };

    // ---- 手动开始 ----
    const heardOf = (s) => {
        const [kind, , rate] = s.need;
        if (running === "room") return validOf(snap.live.claps);
        const v = inSeg * rate;
        return kind === "claps" || kind === "syllables" ? Math.floor(v) : v;
    };
    const showSegment = () => {
        const s = segments[seg];
        const [kind, min] = s.need;
        const heard = waiting ? 0 : heardOf(s);
        if (!waiting && heard >= min) reached = true;
        Object.assign(snap.live, {
            phase: s.phase,
            round: s.round,
            rounds: s.rounds,
            phaseElapsedSec: inSeg,
            phaseDurationSec: s.sec,
            // 确认的第二遍假定背面更响
            directionIndex: s.phase === "direction" ? s.round - 1 : s.phase === "confirm" && s.round === 2 ? 2 : 0,
            waiting,
            canEnd: !waiting && reached,
            need: kind,
            needLeft: reached ? 0 : Math.max(0, min - heard),
            heard,
        });
    };
    const closeSegment = () => {
        seg += 1;
        inSeg = 0;
        reached = false;
        if (seg >= segments.length) return finish(running);
        waiting = true;
        showSegment();
    };
    const advanceManual = (sec) => {
        if (waiting) return;
        const s = segments[seg];
        const step = sec === Infinity ? s.sec - inSeg : Math.min(sec, s.sec - inSeg);
        inSeg += step;
        elapsed += step;
        snap.steps[running].progressSec = Math.min(elapsed, snap.steps[running].durationSec);
        if (running === "room") {
            snap.live.claps = roomClaps(inSeg);
            if (validOf(snap.live.claps) >= snap.live.clapsNeeded) return finish(running);
        }
        if (inSeg >= s.sec) {
            closeSegment();
            // 剩下的时间落在待开始里，丢掉（和插件一样，待开始时的音频不算）
            return;
        }
        showSegment();
    };

    return {
        runningStep: () => running,
        begin(id) {
            if (running) return false;
            running = id;
            elapsed = 0;
            snap.steps[id].status = "running";
            snap.steps[id].progressSec = 0;
            snap.steps[id].invalidReason = "none";
            snap.live = idleLive();
            if (manual && id !== "connect") {
                segments = segmentsOf(id, snap.steps[id].durationSec);
                seg = 0;
                inSeg = 0;
                waiting = false; // 第一段随这一步开始
                reached = false;
                showSegment();
            } else update();
            io.push();
            return true;
        },
        cancel() {
            if (!running) return;
            snap.steps[running].status = "idle";
            snap.steps[running].progressSec = 0;
            snap.live = idleLive();
            running = null;
            io.push();
        },
        advance(sec) {
            if (!running) return;
            if (manual && running !== "connect") advanceManual(sec);
            else {
                elapsed += sec;
                update();
            }
            io.push();
        },
        /** 桥的 startPhase：回桥的原因（"ok" 或 BRIDGE_REASONS 之一）。 */
        startPhase() {
            if (!running || !manual || !waiting) return "no-phase";
            waiting = false;
            snap.live.notice = "none";
            showSegment();
            io.push();
            return "ok";
        },
        /** 桥的 endPhase：没收够回 not-enough，没有正在进行、可以结束的一段回 no-phase。 */
        endPhase() {
            if (!running || !manual || waiting || running === "connect") return "no-phase";
            if (!reached) return "not-enough";
            closeSegment();
            io.push();
            return "ok";
        },
        /** 按已完成的步骤给出状态，供预览与测试直接跳到某个起点。 */
        complete(ids) {
            for (const id of ids) {
                if (!ENUM_STEP_ID.includes(id)) throw new Error(`mock：${id} 不是步骤名`);
                done(id);
                Object.assign(snap.results, resultsFor(id));
            }
            io.push();
        },
    };
}
