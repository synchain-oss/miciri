// SPDX-License-Identifier: GPL-3.0-or-later
//
// docs/ALGORITHMS.md 里阈值表的生成：由 thresholds.json 逐组逐项渲染，夹在两行标记之间。
// 生成脚本（scripts/gen-algorithms.mjs）与检查（checks.d/algorithms-doc.mjs）共用这里的函数，
// 所以文档里的表和 thresholds.json 不会各说各话。纯函数，不读写文件。

export const DOC_FILE = "docs/ALGORITHMS.md";
export const BEGIN = "<!-- thresholds:begin 以下由 scripts/gen-algorithms.mjs 按 thresholds.json 生成，不要手改 -->";
export const END = "<!-- thresholds:end -->";

const STATE_LABEL = { synthetic: "合成验证", pending: "待标定", calibrated: "实录标定" };

/** 表格单元格：竖线转义，换行压成空格。 */
const cell = (s) =>
    String(s ?? "")
        .replace(/\|/g, "\\|")
        .replace(/\r?\n/g, " ")
        .trim();

/** 值：null 写“—”（还没有值），数组写成 [a, b]，其余照 JSON 的写法。 */
const showValue = (v) => (v === null ? "—" : Array.isArray(v) ? `[${v.join(", ")}]` : String(v));

/** 状态格：带日期的写“状态 日期”，待标定没有日期。 */
const showState = (e) => `${STATE_LABEL[e.state] ?? e.state}${e.date ? ` ${e.date}` : ""}`;

/** 组：thresholds.json 里除 schema_version 以外的顶层键，按文件里的先后。 */
export const groupsOf = (json) => Object.entries(json).filter(([k]) => k !== "schema_version");

/** 渲染阈值表：开头一行总数，之后每组一个小节、一张表。返回以换行结尾的文本。 */
export function renderThresholds(json) {
    const groups = groupsOf(json);
    const count = { synthetic: 0, pending: 0, calibrated: 0 };
    let total = 0;
    for (const [, entries] of groups)
        for (const e of Object.values(entries)) {
            total++;
            count[e.state] = (count[e.state] ?? 0) + 1;
        }
    const out = [
        `共 ${total} 项，分 ${groups.length} 组：合成验证 ${count.synthetic} 项、待标定 ${count.pending} 项、实录标定 ${count.calibrated} 项。`,
        "",
    ];
    for (const [name, entries] of groups) {
        out.push(`### \`${name}\``, "", "| 名称 | 值 | 状态 | 依据 | 说明 |", "|---|---|---|---|---|");
        for (const [key, e] of Object.entries(entries))
            out.push(
                `| \`${key}\` | ${cell(showValue(e.value))} | ${cell(showState(e))} | ${cell(e.source)} | ${cell(e.note)} |`,
            );
        out.push("");
    }
    return out.join("\n");
}

/** 文档里两行标记之间的文本（不含标记行本身）。标记缺了或重复了返回 null。 */
export function extractBlock(doc) {
    const a = doc.split(BEGIN);
    const b = doc.split(END);
    if (a.length !== 2 || b.length !== 2) return null;
    const from = doc.indexOf(BEGIN) + BEGIN.length;
    const to = doc.indexOf(END);
    return to < from ? null : doc.slice(from, to).replace(/^\r?\n/, "");
}

/** 把渲染好的表换进文档的两行标记之间；标记缺了或重复了抛错。 */
export function replaceBlock(doc, block) {
    if (extractBlock(doc) === null) throw new Error(`${DOC_FILE}：找不到成对的阈值表标记（各一行）`);
    const from = doc.indexOf(BEGIN) + BEGIN.length;
    return `${doc.slice(0, from)}\n${block}${doc.slice(doc.indexOf(END))}`;
}
