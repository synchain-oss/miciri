// SPDX-License-Identifier: GPL-3.0-or-later
//
// 预览用的流程模拟：只推进“进度、阶段、拍手判定”，不测量任何东西。
// 冒烟测试手动调 advance(秒) 精确推进；预览页（autorun=1）用定时器每秒调一次。
// 阶段表和时长只是占位，真实的步骤编排在原生侧的 Session。

import { ENUM_STEP_ID } from "../../web/shared/bridge-api.js";
import { idleLive } from "./snapshot.js";
import { resultsFor } from "./results.js";

/** 位置步的阶段：[阶段名, 遍数, 每遍秒数]。 */
const POSITION_PHASES = [
    ["soft-clap", 3, 2],
    ["plosive", 2, 10],
    ["sibilant", 2, 10],
    ["natural", 1, 14],
];
/** 方向步：4 个方向各 3 秒，再确认一次。 */
const DIRECTION_PHASES = [
    ["direction", 4, 3],
    ["confirm", 1, 6],
];
const CLAP_PATTERN = ["valid", "too-soft", "valid", "noisy", "valid", "valid"];

function phasesOf(id, duration) {
    if (id === "position") return POSITION_PHASES;
    if (id === "direction") return DIRECTION_PHASES;
    return [["none", 1, duration]];
}

/**
 * @param {object} snap 假快照（会被原地修改）
 * @param {{push: ()=>void}} io push：把快照推给界面
 */
export function createFlowSim(snap, io) {
    let running = null;
    let elapsed = 0;

    const finish = (id) => {
        snap.steps[id].status = "done";
        snap.steps[id].progressSec = snap.steps[id].durationSec;
        Object.assign(snap.results, resultsFor(id));
        // 房间步结束后拍手记录保留（结果页的失败说明和槽位小图要用）
        const claps = id === "room" ? snap.live.claps : [];
        snap.live = { ...idleLive(), claps, notice: "success", noticeSec: 0 };
        running = null;
    };

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
            const heard = Math.min(CLAP_PATTERN.length, Math.floor(elapsed / 2));
            snap.live.claps = CLAP_PATTERN.slice(0, heard);
            if (snap.live.claps.filter((c) => c === "valid").length >= snap.live.clapsNeeded) return finish(id);
        }
        if (elapsed >= step.durationSec) finish(id);
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
            update();
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
            elapsed += sec;
            update();
            io.push();
        },
        /** 按已完成的步骤给出状态，供预览与测试直接跳到某个起点。 */
        complete(ids) {
            for (const id of ids) {
                if (!ENUM_STEP_ID.includes(id)) throw new Error(`mock：${id} 不是步骤名`);
                snap.steps[id].status = "done";
                Object.assign(snap.results, resultsFor(id));
            }
            io.push();
        },
    };
}
