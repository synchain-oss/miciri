// SPDX-License-Identifier: GPL-3.0-or-later
//
// 预览用的假快照：形状完全按 docs/contracts/BRIDGE.md 与 web/shared/bridge-api.js 的字段表。
// 每个对象都经 fields() 建出：表里有而值缺、值有而表里没有，都当场抛错，所以契约追加字段后，
// 这里不跟着改就会在预览和冒烟里立刻红，而不是悄悄漏字段。
// 数值只是占位的合理值，不代表任何算法结果；没做过的步骤没有 results 的键，与契约一致。

import {
    BRIDGE_API_VERSION,
    ENUM_STEP_ID,
    SNAPSHOT_BANDS,
    SNAPSHOT_CONNECT,
    SNAPSHOT_ADVICE,
    SNAPSHOT_DEVICE,
    SNAPSHOT_LIVE,
    SNAPSHOT_ROOT,
    SNAPSHOT_STEP,
} from "../../web/shared/bridge-api.js";

/** 各步的计划纯测试时长（秒）：idle 时也给出，准备页写“约 N 秒”。数值只是预览占位。 */
export const PLAN_SEC = { connect: 0, gain: 8, quiet: 8, room: 20, position: 60, direction: 25 };

/** 档位边界的占位值，与 thresholds.json 同名项相等（冒烟逐项核对，不会悄悄漂移）。 */
export const BANDS = {
    tpTooQuietDbfs: -18,
    tpQuietDbfs: -12,
    tpLoudDbfs: -6,
    tpTooLoudDbfs: -3,
    targetPeakDbfs: -9,
    snrBadBelowDb: 35,
    snrGoodAboveDb: 45,
    t20DryBelowS: 0.25,
    t20NormalBelowS: 0.45,
    t20BoomyBelowS: 0.7,
    drLowBelowDb: 5,
    drHighAboveDb: 12,
    clipAdviceDropDb: 6,
    t20BandsHz: [500, 1000, 2000, 4000],
};

/** 没有步骤在跑时的 live。 */
export function idleLive() {
    return fields(
        SNAPSHOT_LIVE,
        {
            phase: "none",
            round: 0,
            rounds: 0,
            phaseElapsedSec: 0,
            phaseDurationSec: 0,
            claps: [],
            clapsNeeded: 4,
            clapsMax: 8,
            directionIndex: 0,
            notice: "none",
            noticeSec: 0,
            clapDetails: [],
        },
        "live",
    );
}

/** 按字段表建对象，键序与表一致；缺值或多值抛错。 */
export function fields(keys, values, what) {
    const given = Object.keys(values);
    const missing = keys.filter((k) => !given.includes(k));
    const extra = given.filter((k) => !keys.includes(k));
    if (missing.length || extra.length) {
        throw new Error(`${what}：与字段表不符，缺 [${missing.join(", ")}]，多 [${extra.join(", ")}]`);
    }
    return Object.fromEntries(keys.map((k) => [k, values[k]]));
}

/**
 * @param {{lang?: string, uiScale?: number, layout?: "mono"|"stereo"}} [o]
 * @returns 一份「刚打开插件、什么都没测」的快照
 */
export function buildSnapshot(o = {}) {
    const steps = Object.fromEntries(
        ENUM_STEP_ID.map((id) => [
            id,
            fields(
                SNAPSHOT_STEP,
                {
                    status: "idle",
                    progressSec: 0,
                    durationSec: PLAN_SEC[id],
                    invalidReason: "none",
                    evidenceRev: 0,
                    runs: 0,
                },
                `steps.${id}`,
            ),
        ]),
    );
    return fields(
        SNAPSHOT_ROOT,
        {
            apiVersion: BRIDGE_API_VERSION,
            lang: o.lang || "zh",
            uiScale: o.uiScale || 1,
            guideSeen: o.guideSeen !== false,
            micType: "unknown",
            device: fields(
                SNAPSHOT_DEVICE,
                {
                    sampleRate: 48000,
                    bufferSize: 512,
                    layout: o.layout || "mono",
                    hostName: "Preview",
                    offlineRender: false,
                },
                "device",
            ),
            connect: fields(
                SNAPSHOT_CONNECT,
                {
                    signalState: "waiting",
                    callbackAgeSec: 0,
                    allZero: false,
                    noiseDbfs: -90,
                    channelStatusL: "unknown",
                    channelStatusR: "unknown",
                    channelPrompt: "none",
                    channelPromptDismissed: false,
                    analysedChannel: "left",
                },
                "connect",
            ),
            steps,
            results: {},
            bands: fields(SNAPSHOT_BANDS, { ...BANDS }, "bands"),
            live: idleLive(),
            fixes: [],
            advice: fields(SNAPSHOT_ADVICE, { room: "unknown", distance: "none", directionPrompt: "none" }, "advice"),
            previous: {},
        },
        "snapshot",
    );
}
