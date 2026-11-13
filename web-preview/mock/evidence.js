// SPDX-License-Identifier: GPL-3.0-or-later
//
// 预览用的假证据波形：getEvidence 回执里 evidence 的形状按 EVIDENCE_* 表（经 fields() 建出，缺字段、多字段当场抛错）。
// 数值是合成的：每 1.1 秒一个像音节的包络，加几条三种事件；不代表任何算法结果，只给界面先做画布、缩放和事件标记。
// 和真插件一样，只有位置步有这三种事件，别的步骤 events 是空数组。上限与 thresholds.json 的 session 分组同名项相等（冒烟核对）。

import { EVIDENCE_ENVELOPE, EVIDENCE_EVENT, EVIDENCE_EVENT_KINDS, EVIDENCE_PACK } from "../../web/shared/bridge-api.js";
import { fields } from "./snapshot.js";

/** session.evidence_hop_min_s、evidence_cells_max、evidence_events_max 的占位值。 */
export const EVIDENCE_LIMITS = { hopMinS: 0.002, cellsMax: 8192, eventsMax: 200 };

const SYLLABLE_EVERY_S = 1.1;
const SYLLABLE_S = 0.35;
const FULL_SCALE = 1000; // 包络的单位：千分之一满刻度

function envelope(durationSec) {
    const hopSec = Math.max(EVIDENCE_LIMITS.hopMinS, durationSec / EVIDENCE_LIMITS.cellsMax);
    const cells = Math.min(EVIDENCE_LIMITS.cellsMax, Math.ceil(durationSec / hopSec));
    const min = [];
    const max = [];
    for (let i = 0; i < cells; i++) {
        const t = i * hopSec;
        const at = t % SYLLABLE_EVERY_S;
        const syllable = at < SYLLABLE_S ? Math.sin((Math.PI * at) / SYLLABLE_S) : 0;
        const peak = Math.round(FULL_SCALE * Math.min(1, 0.01 + 0.5 * syllable * (0.7 + 0.3 * Math.sin(t * 7.3))));
        max.push(peak);
        min.push(-Math.round(peak * 0.9));
    }
    return fields(EVIDENCE_ENVELOPE, { hopSec, min, max }, "evidence.envelope");
}

// 位置步：前三分之一的音节起音处标喷麦，中间三分之一标齿音，最后三分之一的停顿里标口水音
function events(stepId, durationSec) {
    if (stepId !== "position") return [];
    const [plosive, sibilance, mouth] = EVIDENCE_EVENT_KINDS;
    const out = [];
    const third = durationSec / 3;
    for (let t = 0.05; t < durationSec; t += SYLLABLE_EVERY_S * 2) {
        const kind = t < third ? plosive : t < 2 * third ? sibilance : mouth;
        const at = kind === mouth ? t + SYLLABLE_S + 0.2 : t;
        const durSec = kind === plosive ? 0.04 : kind === sibilance ? 0.15 : 0.003;
        const levelDb = kind === plosive ? 8 + (t % 5) : kind === sibilance ? -38 - (t % 4) : -42 - (t % 6);
        if (at < durationSec)
            out.push(fields(EVIDENCE_EVENT, { kind, timeSec: at, durSec, levelDb }, "evidence.events[]"));
    }
    return out.slice(0, EVIDENCE_LIMITS.eventsMax);
}

/**
 * @param {string} stepId
 * @param {number} rev 快照里这一步的 evidenceRev
 * @param {number} durationSec 这一步的时长；没有时按 6 秒
 */
export function synthEvidence(stepId, rev, durationSec) {
    const dur = durationSec > 0 ? durationSec : 6;
    return fields(
        EVIDENCE_PACK,
        { stepId, rev, durationSec: dur, envelope: envelope(dur), events: events(stepId, dur) },
        "evidence",
    );
}
