// SPDX-License-Identifier: GPL-3.0-or-later
//
// 预览用的结果夹具：每一步做完后写进快照 results 的区段。形状按桥契约的字段表建（fields 会核对键集），
// 数值只是占位的合理值，不代表任何算法结果。冒烟测试用 withResult() 改单个字段，造出各种档位。

import {
    RESULT_CLICK,
    RESULT_COMMON,
    RESULT_DIRECT_RATIO,
    RESULT_DIRECTION,
    RESULT_DISTANCE,
    RESULT_GAIN,
    RESULT_HUM,
    RESULT_NOISE,
    RESULT_PLOSIVE,
    RESULT_REVERB,
    RESULT_SIBILANCE,
} from "../../web/shared/bridge-api.js";
import { fields } from "./snapshot.js";

const common = (tone) => ({ tone, reliability: "trusted", retestReason: "none", stale: false });
const section = (name, table, values, tone) =>
    fields([...table, ...RESULT_COMMON], { ...values, ...common(tone) }, `results.${name}`);

const BUILDERS = {
    gain: () =>
        section(
            "gain",
            RESULT_GAIN,
            {
                samplePeakDbfsL: -9.1,
                samplePeakDbfsR: -9.1,
                truePeakDbfsL: -8.4,
                truePeakDbfsR: -8.4,
                loudnessLkfs: -20.1,
                digitalClipRunsL: 0,
                digitalClipRunsR: 0,
                flatPlateauPeaksL: 0,
                flatPlateauPeaksR: 0,
                band: "good",
                digitalClip: false,
                maybeFlatTop: false,
            },
            "green",
        ),
    noise: () =>
        section(
            "noise",
            RESULT_NOISE,
            { noiseDbfs: -57, noiseDbfsA: -62, snrDb: 41, noisePeakDbfs: -48, band: "fair" },
            "amber",
        ),
    hum: () =>
        section(
            "hum",
            RESULT_HUM,
            {
                fundamentalHz: 50,
                overFloorDb: 9,
                energySharePct: 18,
                highestHarmonicHz: 500,
                levelDbfs: -64.4,
                peakDbfs: -61.4,
                kind: "hum",
                slight: false,
            },
            "amber",
        ),
    reverb: () =>
        section(
            "reverb",
            RESULT_REVERB,
            {
                t20Sec: [0.66, 0.58, 0.51, 0.44],
                tMidSec: 0.62,
                bandPnrDb: [41, 40, 38, 37],
                bandR2: [0.98, 0.98, 0.97, 0.97],
                repeatMaxOverMin: 1.1,
                band: "boomy",
                gated: false,
            },
            "amber",
        ),
    directRatio: () =>
        section("directRatio", RESULT_DIRECT_RATIO, { drDb: 3.8, lowerBoundOnly: false, band: "low" }, "red"),
    plosive: () =>
        section(
            "plosive",
            RESULT_PLOSIVE,
            { worstRatioDb: 2, events: 6, severity: "light", lowSensitivity: false },
            "green",
        ),
    sibilance: () =>
        section(
            "sibilance",
            RESULT_SIBILANCE,
            {
                midHighDb: -12,
                highDb: -18,
                effectiveBandwidthHz: 16000,
                events: 8,
                severity: "light",
                highBandDisabled: false,
            },
            "green",
        ),
    click: () =>
        section(
            "click",
            RESULT_CLICK,
            {
                perMinute: 3,
                loudestRelDb: -24,
                loudestDbfs: -32.4,
                analysedSeconds: 15,
                count: 1,
                severity: "light",
            },
            "green",
        ),
    distance: () =>
        section(
            "distance",
            RESULT_DISTANCE,
            { closeEvidence: [false, false, false], farEvidence: [true, false], kind: "too-far" },
            "red",
        ),
    direction: () =>
        section(
            "direction",
            RESULT_DIRECTION,
            {
                levelDb: [0, -3, -9, -4],
                brightnessDb: [0, -1, -5, -2],
                currentIndex: 0,
                bestOtherAdvantageDb: 0,
                kind: "best",
            },
            "green",
        ),
};

/** 每一步做完后出现的结果区段。 */
export const STEP_SECTIONS = {
    gain: ["gain"],
    quiet: ["noise", "hum"],
    room: ["reverb"],
    position: ["directRatio", "plosive", "sibilance", "click", "distance"],
    direction: ["direction"],
};

/** @returns {object} 一份区段对象，键是区段名 */
export function resultsFor(stepId) {
    return Object.fromEntries((STEP_SECTIONS[stepId] || []).map((name) => [name, BUILDERS[name]()]));
}

/** 改一个区段里的若干字段（含 tone、stale 等公共字段），返回新对象。 */
export function withResult(name, patch) {
    const base = BUILDERS[name]();
    for (const k of Object.keys(patch)) {
        if (!(k in base)) throw new Error(`results.${name} 没有字段 ${k}`);
    }
    return { ...base, ...patch };
}
