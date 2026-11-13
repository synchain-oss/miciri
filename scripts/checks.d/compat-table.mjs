// SPDX-License-Identifier: GPL-3.0-or-later
//
// 宿主兼容表（docs/DAW_COMPATIBILITY.md）的核对。表里每个“已测”的格子必须写版本、验证日期和证据，
// 日期超过 6 个月的格子视为未验证。判据：
//   - 硬检查（不过就红）：状态不是“未测 / Untested”的行，版本、日期、证据都不能空；日期要是真实存在的 ISO 日期
//     （YYYY-MM-DD）；每行 6 列；中英两张表的行数、各行是否已测、验证日期要一致；文件里至少有一张表；
//   - 过期（验证日期 + 6 个月 < 今天）默认只在输出里写 WARN，不判红：否则半年后毫不相干的 PR 会突然全红；
//     环境变量 MICIRI_COMPAT_STRICT=1 时过期也判红，发布前用它核对；
//   - “今天”默认取当前 UTC 日期，环境变量 MICIRI_TODAY=YYYY-MM-DD 可以指定（复现某一天的结果，或自测）。
// 用法：MICIRI_COMPAT_STRICT=1 node scripts/run-checks.mjs --only compat-table
// 判据在 evaluate，只吃文本和（今天、是否严格）。

import path from "node:path";
import { caseResult, readText } from "../lib/check-kit.mjs";

const FILE = "docs/DAW_COMPATIBILITY.md";
const COLUMNS = 6; // 宿主、平台与格式、状态、版本、验证日期、证据
const EXPIRY_MONTHS = 6;
const UNTESTED = /^(?:未测|未验证|untested|unverified)$/i;

/** YYYY-MM-DD 且真实存在的日期（UTC 零点）；不是就返回 null。 */
export function parseIsoDate(s) {
    const m = /^(\d{4})-(\d{2})-(\d{2})$/.exec(s ?? "");
    if (!m) return null;
    const [y, mo, d] = m.slice(1).map(Number);
    const dt = new Date(Date.UTC(y, mo - 1, d));
    return dt.getUTCFullYear() === y && dt.getUTCMonth() === mo - 1 && dt.getUTCDate() === d ? dt : null;
}

/** 加 n 个月；目标月没有这一天时落在该月最后一天（8 月 31 日加 6 个月是 2 月 28 日或 29 日）。 */
export function addMonths(dt, n) {
    const y = dt.getUTCFullYear();
    const m = dt.getUTCMonth() + n;
    const last = new Date(Date.UTC(y, m + 1, 0)).getUTCDate();
    return new Date(Date.UTC(y, m, Math.min(dt.getUTCDate(), last)));
}

const iso = (dt) => dt.toISOString().slice(0, 10);
const cells = (line) =>
    line
        .trim()
        .replace(/^\|/, "")
        .replace(/\|$/, "")
        .split("|")
        .map((c) => c.trim());

/** 找出所有以“宿主 / Host”开头的表头对应的表：[{ line, header, rows: [{ line, cells }] }]。 */
export function parseTables(text) {
    const lines = text.split(/\r?\n/);
    const tables = [];
    for (let i = 0; i < lines.length; i++) {
        if (!/^\|\s*(?:宿主|Host)\s*\|/.test(lines[i])) continue;
        const rows = [];
        let k = i + 2; // 跳过分隔行
        while (k < lines.length && lines[k].trim().startsWith("|")) {
            rows.push({ line: k + 1, cells: cells(lines[k]) });
            k++;
        }
        tables.push({ line: i + 1, header: cells(lines[i]), rows });
        i = k;
    }
    return tables;
}

/** today：UTC 零点的 Date；strict：过期也算错误。返回 { errors, warns }。 */
export function evaluate(text, { today, strict = false }) {
    const errors = [];
    const warns = [];
    const tables = parseTables(text);
    if (!tables.length) errors.push(`${FILE}：找不到兼容表（表头应以“| 宿主 |”或“| Host |”开头）`);
    const summary = []; // 每张表每行：是否已测、日期，用来核对中英两张表一致
    for (const t of tables) {
        if (t.header.length !== COLUMNS)
            errors.push(`第 ${t.line} 行：表头应有 ${COLUMNS} 列，实际 ${t.header.length}`);
        const tested = [];
        for (const r of t.rows) {
            if (r.cells.length !== COLUMNS) {
                errors.push(`第 ${r.line} 行：应有 ${COLUMNS} 列，实际 ${r.cells.length}`);
                continue;
            }
            const [host, platform, status, version, date, evidence] = r.cells;
            const name = `第 ${r.line} 行 ${host}（${platform}）`;
            if (!host || !status) errors.push(`${name}：宿主和状态不能空`);
            const isTested = Boolean(status) && !UNTESTED.test(status);
            tested.push(`${isTested}|${date}`);
            if (!isTested) continue;
            if (!version) errors.push(`${name}：已测的行要写宿主版本`);
            if (!evidence) errors.push(`${name}：已测的行要写证据`);
            const dt = parseIsoDate(date);
            if (!dt) {
                errors.push(`${name}：验证日期“${date}”不是真实存在的 YYYY-MM-DD 日期`);
                continue;
            }
            const until = addMonths(dt, EXPIRY_MONTHS);
            if (today > until) {
                const msg = `${name}：验证日期 ${date} 已超过 ${EXPIRY_MONTHS} 个月（有效至 ${iso(until)}），视为未验证，要重测`;
                (strict ? errors : warns).push(msg);
            }
        }
        summary.push({ line: t.line, tested });
    }
    for (const t of summary.slice(1)) {
        if (t.tested.join("\n") !== summary[0].tested.join("\n"))
            errors.push(`第 ${t.line} 行的表与第 ${summary[0].line} 行的表不一致：行数、各行是否已测、验证日期要相同`);
    }
    return { errors, warns };
}

export default {
    name: "compat-table",
    group: "docs",
    inputs: [],
    run(ctx) {
        const abs = path.join(ctx.root, FILE);
        let text;
        try {
            text = readText(abs);
        } catch {
            return { ok: false, lines: [`${FILE} 不存在或读不了：兼容表是公开文档的一部分`] };
        }
        const given = ctx.env.MICIRI_TODAY;
        const today = given ? parseIsoDate(given) : parseIsoDate(new Date().toISOString().slice(0, 10));
        if (!today) return { ok: false, lines: [`MICIRI_TODAY“${given}”不是 YYYY-MM-DD 日期`] };
        const { errors, warns } = evaluate(text, { today, strict: ctx.env.MICIRI_COMPAT_STRICT === "1" });
        return { ok: errors.length === 0, lines: [...errors, ...warns.map((w) => `WARN ${w}`)] };
    },
    selfTest() {
        const today = parseIsoDate("2026-11-12");
        const zhHead = "| 宿主 | 平台与格式 | 状态 | 版本 | 验证日期 | 证据 |\n|---|---|---|---|---|---|\n";
        const enHead =
            "| Host | Platform and format | Status | Version | Date | Evidence |\n|---|---|---|---|---|---|\n";
        const tested = (date = "2026-11-12", version = "7.82", evidence = "见下") =>
            `| REAPER | Windows，VST3 | 已测 | ${version} | ${date} | ${evidence} |\n`;
        const untested = "| Cubase | | 未测 | | | |\n";
        const zh = (...rows) => zhHead + rows.join("");
        // [说明, 应判红, 文本, { 今天, 严格 }, 期望的 WARN 数（只对不判红的样例）]
        const cases = [
            ["已测行齐全加未测行", false, zh(tested(), untested), {}, 0],
            ["未测行可以全空", false, zh(untested), {}, 0],
            ["英文表头加 Untested", false, enHead + "| Cubase | | Untested | | | |\n", {}, 0],
            [
                "中英两张表一致",
                false,
                zh(tested(), untested) +
                    "\n" +
                    enHead +
                    tested().replace("已测", "Tested") +
                    "| Cubase | | Untested | | | |\n",
                {},
                0,
            ],
            ["恰好满 6 个月不算过期", false, zh(tested("2026-05-12")), {}, 0],
            ["过期但不严格：只 WARN", false, zh(tested("2026-05-11")), {}, 1],
            [
                "月末取整：8 月 31 日加 6 个月是 2 月 28 日，当天不过期",
                false,
                zh(tested("2025-08-31")),
                { today: "2026-02-28", strict: true },
                0,
            ],
            ["有第二天才算过期（严格）", true, zh(tested("2025-08-31")), { today: "2026-03-01", strict: true }],
            ["严格模式下过期", true, zh(tested("2026-05-11")), { strict: true }],
            ["已测行缺版本", true, zh(tested("2026-11-12", "")), {}],
            ["已测行缺证据", true, zh(tested("2026-11-12", "7.82", "")), {}],
            ["已测行缺日期", true, zh(tested("")), {}],
            ["日期写成斜杠", true, zh(tested("2026/11/12")), {}],
            ["日期不存在", true, zh(tested("2026-02-30")), {}],
            ["少一列", true, zh("| REAPER | Windows | 已测 | 7.82 | 2026-11-12 |\n"), {}],
            ["表头列数不对", true, "| 宿主 | 状态 |\n|---|---|\n| Cubase | 未测 |\n", {}],
            ["没有表", true, "# 宿主兼容表\n", {}],
            [
                "中英两张表一个已测一个未测",
                true,
                zh(tested()) + "\n" + enHead + "| REAPER | Windows | Untested | | | |\n",
                {},
            ],
            ["中英两张表日期不同", true, zh(tested("2026-11-12")) + "\n" + enHead + tested("2026-11-11"), {}],
        ].map(([label, bad, text, opt, warns]) => {
            const { errors, warns: w } = evaluate(text, {
                today: parseIsoDate(opt.today ?? "2026-11-12") ?? today,
                strict: opt.strict,
            });
            return { label, bad, passed: bad ? errors.length > 0 : errors.length === 0 && w.length === warns };
        });
        return caseResult(cases);
    },
};
