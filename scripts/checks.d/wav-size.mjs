// SPDX-License-Identifier: GPL-3.0-or-later
//
// 音频文件的大小与位置限制。公开仓只放合成夹具（生成脚本，或很短的 wav）：真实录音不进仓库，
// 也不进 issue；总量也要小，免得每个人的克隆都背着它。判据：
//   - 只许 .wav，其他音频扩展名（flac、mp3、ogg、m4a、aiff 等）在仓里一律不许出现；
//   - .wav 只能放在 tests/fixtures/ 或 tests/golden/ 下；
//   - 单个 ≤ 200 KB，全仓合计 < 5 MB。
// 判据在 wavProblems，只吃路径与字节数。

import fs from "node:fs";
import path from "node:path";
import { caseResult, trackedFiles } from "../lib/check-kit.mjs";

export const MAX_FILE = 200 * 1024;
export const MAX_TOTAL = 5 * 1024 * 1024;
const WAV_HOME = /^tests\/(?:fixtures|golden)\//;
const AUDIO = /\.(?:wav|flac|mp3|ogg|oga|opus|m4a|aac|aif|aiff|wma|mp2|caf)$/i;

/** files：[{ path, size }]（仓内相对路径，正斜杠）。返回问题列表。 */
export function wavProblems(files) {
    const errors = [];
    let total = 0;
    for (const f of files) {
        if (!AUDIO.test(f.path)) continue;
        if (!/\.wav$/i.test(f.path)) {
            errors.push(`${f.path}：只许放 .wav 合成夹具，不收其他音频格式`);
            continue;
        }
        total += f.size;
        if (!WAV_HOME.test(f.path)) errors.push(`${f.path}：.wav 只能放在 tests/fixtures/ 或 tests/golden/ 下`);
        if (f.size > MAX_FILE) errors.push(`${f.path}：${f.size} 字节，单个上限 ${MAX_FILE}`);
    }
    if (total >= MAX_TOTAL) errors.push(`.wav 合计 ${total} 字节，上限 ${MAX_TOTAL} 字节（不含）`);
    return errors;
}

export default {
    name: "wav-size",
    group: "compliance",
    inputs: [],
    run(ctx) {
        const files = trackedFiles(ctx.root)
            .filter((p) => AUDIO.test(p))
            .map((p) => {
                const abs = path.join(ctx.root, p);
                return { path: p, size: fs.existsSync(abs) ? fs.statSync(abs).size : 0 };
            });
        const errors = wavProblems(files);
        return { ok: errors.length === 0, lines: errors };
    },
    selfTest() {
        const w = (p, size) => ({ path: p, size });
        const many = Array.from({ length: 30 }, (_, i) => w(`tests/fixtures/s${i}.wav`, 190 * 1024));
        const cases = [
            ["没有音频", false, wavProblems([w("README.md", 10)])],
            [
                "夹具目录下的小 wav",
                false,
                wavProblems([w("tests/fixtures/sine.wav", 4096), w("tests/golden/pink.wav", 90000)]),
            ],
            ["恰好 200 KB", false, wavProblems([w("tests/fixtures/a.wav", MAX_FILE)])],
            ["单个超过 200 KB", true, wavProblems([w("tests/fixtures/a.wav", MAX_FILE + 1)])],
            [
                "合计到 5 MB",
                true,
                wavProblems(Array.from({ length: 5 }, (_, i) => w(`tests/fixtures/${i}.wav`, MAX_TOTAL / 5))),
            ],
            ["三十个 190 KB 的 wav 合计超限", true, wavProblems(many)],
            ["wav 放在 web 下", true, wavProblems([w("web/assets/a.wav", 100)])],
            ["大写扩展名也算", true, wavProblems([w("docs/A.WAV", 100)])],
            ["mp3", true, wavProblems([w("tests/fixtures/a.mp3", 100)])],
            ["flac", true, wavProblems([w("tests/fixtures/a.flac", 100)])],
        ].map(([label, bad, errors]) => ({ label, bad, passed: bad ? errors.length > 0 : errors.length === 0 }));
        return caseResult(cases);
    },
};
