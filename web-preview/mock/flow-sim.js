// SPDX-License-Identifier: GPL-3.0-or-later
//
// 预览用的流程模拟：只推进“进度、阶段、拍手判定”，不测量任何东西。
// 冒烟测试手动调 advance(秒) 精确推进；预览页（autorun=1）用定时器每秒调一次。
// 阶段表、时长和最低量只是占位，真实的步骤编排在原生侧的 Session。
//
// 两种分段，和插件一样（见 docs/contracts/BRIDGE.md 的“一段的开始与结束”）：
// - 手动开始（缺省，插件由界面驱动时就是这样）：第一段随 begin 开始，之后每段待开始（live.waiting），
//   startPhase 才开始；收够这一段的最低量（假的：按时间线性增长）后 endPhase 可以提前结束，到时长上限照样自动结束。
//   待开始时 advance 不推进。轻拍不按遍计时：每 2 秒听到一下，按 TAP_PATTERN（setTaps 可换）给判定，
//   凑够 tapsNeeded 下有效就自己进字表；到上限（阶段表的第 5 项）有效的不够 2 下就退回待开始重拍。
// - 自动推进（phases: "auto"）：各段首尾相接，按时长推进，startPhase、endPhase 回 no-phase。
//
// 接通是一轮观察窗（CONNECT_SEC，和插件一样长），只按时长推进；一轮结束时把 setInput 给的信号状态写进快照，
// 没给就不动（冒烟多半直接改快照里的 signalState）。

import { ENUM_STEP_ID } from "../../web/shared/bridge-api.js";
import { idleLive } from "./snapshot.js";
import { resultsFor } from "./results.js";

/**
 * 位置步的阶段：[阶段名, 遍数, 每遍秒数, 手动开始时的最低量（见 NEED）, 手动开始时一直听、逐下判定的上限秒数]。
 * 第 5 项只有轻拍有（插件里是 session.soft_clap_max_s）。
 */
const POSITION_PHASES = [
    ["soft-clap", 3, 2, ["claps", 2, 0.6], 20],
    ["plosive", 2, 10, ["syllables", 6, 0.9]],
    ["sibilant", 2, 10, ["syllables", 6, 0.9]],
    ["natural", 1, 14, ["voice-seconds", 8, 0.7]],
];
/** 方向步：4 个方向各 3 秒，再确认一次。 */
const DIRECTION_PHASES = [
    ["direction", 4, 3],
    ["confirm", 1, 6],
];
/** 接通的一轮观察多长（插件里是 no_signal.low_flat_window_s）。 */
const CONNECT_SEC = 6;
const CLAP_PATTERN = ["valid", "too-soft", "valid", "noisy", "valid", "valid"];
/** 手动分段的轻拍：每 2 秒一下的判定；上限和 D/R 要的最少有效下数取插件的起始值。 */
const TAP_PATTERN = ["clipped", "valid", "unmeasured", "valid", "valid"];
const MIN_CLEAN_TAPS = 2;

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
    if (id === "position")
        return POSITION_PHASES.map(([name, rounds, per, need, listen]) => ({
            ...one(name, listen || rounds * per, need),
            taps: !!listen,
        }));
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
    // 下一轮接通观察会看到的信号状态（ENUM_SIGNAL_STATE 之一），null 时不改快照
    let input = null;
    let tapPattern = TAP_PATTERN;

    // 做完一步：次数与证据版本各加 1（作废、取消不加，和真插件一样）
    const done = (id) => {
        snap.steps[id].status = "done";
        snap.steps[id].runs += 1;
        snap.steps[id].evidenceRev += 1;
    };

    const finish = (id) => {
        done(id);
        snap.steps[id].progressSec = snap.steps[id].durationSec;
        if (id === "connect" && input) {
            snap.connect.signalState = input;
            const heard = input === "ok" ? "signal" : "silent";
            snap.connect.channelStatusL = heard;
            snap.connect.channelStatusR = snap.device.layout === "stereo" ? heard : "unknown";
        }
        Object.assign(snap.results, resultsFor(id));
        // 房间步结束后拍手记录保留（结果页的失败说明和槽位小图要用）
        const claps = id === "room" ? snap.live.claps : [];
        snap.live = { ...idleLive(), claps, notice: "success", noticeSec: 0 };
        running = null;
    };

    const roomClaps = (sec) => CLAP_PATTERN.slice(0, Math.min(CLAP_PATTERN.length, Math.floor(sec / 2)));
    const validOf = (claps) => claps.filter((c) => c === "valid").length;
    const tapsAt = (sec) => tapPattern.slice(0, Math.min(tapPattern.length, Math.floor(sec / 2)));

    const update = () => {
        const id = running;
        const step = snap.steps[id];
        const duration = id === "connect" ? CONNECT_SEC : step.durationSec;
        step.progressSec = Math.min(elapsed, step.durationSec);
        const phases = phasesOf(id, duration);
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
        if (elapsed >= duration) finish(id);
    };

    // ---- 手动开始 ----
    const heardOf = (s) => {
        const [kind, , rate] = s.need;
        if (running === "room") return validOf(snap.live.claps);
        if (s.taps) return validOf(snap.live.taps);
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
        snap.live.taps = [];
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
        if (s.taps) {
            snap.live.taps = tapsAt(inSeg);
            const valid = validOf(snap.live.taps);
            if (valid >= snap.live.tapsNeeded) return closeSegment();
            if (inSeg >= s.sec && valid < MIN_CLEAN_TAPS) {
                // 和插件一样：削波的几下本来够数就说“拍轻一点”，否则是拍得太少
                const clipped = snap.live.taps.filter((t) => t === "clipped").length;
                elapsed -= inSeg;
                inSeg = 0;
                reached = false;
                waiting = true;
                snap.live.taps = [];
                snap.live.notice = valid + clipped >= MIN_CLEAN_TAPS ? "clap-clipped" : "redo";
                return showSegment();
            }
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
        /** 手动分段的轻拍每 2 秒一下的判定（ENUM_TAP_STATE），换成别的序列试到上限不够的情形。 */
        setTaps(pattern) {
            tapPattern = pattern;
        },
        /** 下一轮接通观察结束时快照里的信号状态（例如 "ok"）；null 时不改。 */
        setInput(state) {
            input = state;
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
