// SPDX-License-Identifier: GPL-3.0-or-later
//
// docs/ALGORITHMS.md 里的阈值表必须和 thresholds.json 一致（docs-truth 的一部分）。表由 scripts/gen-algorithms.mjs 生成：
// 改了阈值而没重新生成，这里判红，并写出第一处不同在表里的第几行。文件缺了、标记缺了也判红。
// 判据在 compare，只吃文本和 json。

import fs from "node:fs";
import path from "node:path";
import { caseResult } from "../lib/check-kit.mjs";
import { BEGIN, DOC_FILE, END, extractBlock, renderThresholds, replaceBlock } from "../lib/algorithms-doc.mjs";

/** 返回错误说明列表；空表示一致。 */
export function compare(doc, json) {
    const have = extractBlock(doc);
    if (have === null) return [`${DOC_FILE}：找不到成对的阈值表标记（各一行）`];
    const want = renderThresholds(json);
    if (have === want) return [];
    const a = have.split("\n");
    const b = want.split("\n");
    let i = 0;
    while (i < a.length && i < b.length && a[i] === b[i]) i++;
    return [
        `${DOC_FILE}：阈值表和 thresholds.json 不一致（表里第 ${i + 1} 行起不同），运行 node scripts/gen-algorithms.mjs 重新生成`,
    ];
}

const SAMPLE = {
    schema_version: 1,
    demo: {
        frame_ms: { value: 100, state: "synthetic", source: "合成验证", date: "2026-11-02" },
        bands_hz: { value: [500, 1000], state: "pending", source: "规格起始值", date: null, note: "a | b" },
        later: { value: null, state: "pending", source: "待定", date: null },
    },
};

export function selfTest() {
    const doc = replaceBlock(`# t\n\n${BEGIN}\nold\n${END}\n`, renderThresholds(SAMPLE));
    const withDemo = (patch) => ({ ...SAMPLE, demo: { ...SAMPLE.demo, ...patch } });
    const frame = SAMPLE.demo.frame_ms;
    // [说明, 是不是坏样例, compare 有没有报错]：坏样例要报错才算过，正例不能报错
    const cases = [
        ["表和数据一致", false, compare(doc, SAMPLE).length > 0],
        [
            "数据里改了一个值，文档没重新生成",
            true,
            compare(doc, withDemo({ frame_ms: { ...frame, value: 50 } })).length > 0,
        ],
        ["数据里多了一项，文档没重新生成", true, compare(doc, withDemo({ extra: { ...frame, value: 1 } })).length > 0],
        ["有人手改了表里的一格", true, compare(doc.replace("| 100 |", "| 101 |"), SAMPLE).length > 0],
        ["文档里没有标记", true, compare("# t\n", SAMPLE).length > 0],
        ["标记各出现两次", true, compare(doc + doc, SAMPLE).length > 0],
        [
            "重新生成一次，文档不变，仍与数据一致",
            false,
            compare(replaceBlock(doc, renderThresholds(SAMPLE)), SAMPLE).length > 0,
        ],
    ].map(([label, bad, flagged]) => ({ label, bad, passed: flagged === bad }));
    // 说明里的竖线要转义，否则表会多出一列
    cases.push({ label: "说明里的竖线被转义", bad: false, passed: renderThresholds(SAMPLE).includes("a \\| b") });
    return caseResult(cases);
}

export default {
    name: "algorithms-doc",
    group: "docs",
    inputs: [],
    run(ctx) {
        const docPath = path.join(ctx.root, DOC_FILE);
        if (!fs.existsSync(docPath)) return { ok: false, lines: [`缺 ${DOC_FILE}`] };
        const json = JSON.parse(fs.readFileSync(path.join(ctx.root, "thresholds.json"), "utf8"));
        const errors = compare(fs.readFileSync(docPath, "utf8"), json);
        return { ok: errors.length === 0, lines: errors };
    },
    selfTest,
};
