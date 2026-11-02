// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from synchain-oss/rinne; see upstream/ci.json
//
// 公开文本的隐私与卫生规则：全仓只有这一张表。check-privacy（工作树、提交区间、PR 文本）、
// check-commits（提交消息、PR 文本、分支名）和两个 git hook 都从这里取规则；豁免与自测夹具也在这里。
//
// 表里只放通用规则。个人信息针（个人邮箱、本机用户名与路径片段、其他账号名、项目代号）不进公开仓，
// 由 loadPersonalNeedles() 按调用方显式给出的来源读入，见那里的说明。
//
// 针和夹具一律用 j() 由片段拼出：本文件自己也在被扫范围内，源码里一旦出现完整形态，
// 就只能给自己开豁免，而豁免正是藏东西的地方。夹具与规则各拼各的，互不引用，
// 这样把规则改坏时夹具不会跟着一起变（否则自测永远是绿的）。

import fs from "node:fs";
import { decodeText, j, localSetting } from "./check-kit.mjs";

const BS = "\\\\"; // 正则源码里的一个反斜杠
// 路径分隔符：1–4 个反斜杠（手写、JSON 转义、JSON 套 JSON）、正斜杠、URL 编码
const SEP = `(?:${BS}{1,4}|/|%5[Cc]|%2[Ff])`;
// 盘符（含 URL 编码的冒号）、MSYS 与 WSL 的挂载前缀、UNC 管理共享（\\host\c$\）
const DRIVE = `(?:[A-Za-z](?::|%3[Aa])${SEP}|/(?:mnt/)?[A-Za-z]/|${BS}{2,4}[^\\\\/\\s]+${SEP}[A-Za-z]\\$${SEP})`;
const NAME = "([^\\\\/\\s\"'`|;*?%]+)"; // 路径里的用户名段
const MAIL_HOSTS = [
    "gmail|googlemail|outlook|hotmail|live|msn|yahoo|icloud|me|mac|proton|protonmail|pm|gmx|yandex|mail",
    "qq|foxmail|163|126|yeah|sina|sohu|aliyun",
].join("|");
const MAIL_TLDS = "com|net|org|cn|me|ch|de|fr|ru|jp|co\\.uk|co\\.jp|com\\.cn";
// 编号的边界：下划线也算分隔（卡号写进 snake_case 文件名、日志名时也要抓到），所以不用 \b
const L = "(?<![A-Za-z0-9])";
const R = "(?![A-Za-z0-9])";

/** 逐字收录的第三方文本：只对上游原文里会出现的邮箱、编号与章节号豁免，私有目录名与卡号照判。 */
export const VERBATIM_DIRS = ["third_party/", "LICENSES/", "web/js/juce/"];
/** 只有 C/C++ 源文件对 commentsOnly 规则只看注释与字符串字面量（滤波器系数、矩阵元一类的标识符太像编号）。 */
export const CODE_FILE = /\.(?:c|cc|cpp|h|hpp|mm)$/;

const placeholder = (u) => /^[<$]/.test(u) || u === "...";
const SECTION_OK = /GPL|OFL|Apache|section|IEC|ISO|IEEE|ITU|AES|EBU|RFC/i;
// 章节号只在许可证与标准条款里放行（「GPLv3 」「IEC 61672 」加章节号），或同一文件有以该章节号开头的标题
const sectionAllowed = (m, line, ctx) =>
    SECTION_OK.test(line.slice(Math.max(0, m.index - 12), m.index)) ||
    !!ctx.headings?.has(m[0].replace(/\D*(\d+).*/, "$1"));
// ---- 放行上下文 ------------------------------------------------------------------------
//
// 注意：现有规则表里没有规则带 contextAware 旗标，下面的区间函数当前不参与任何判定，整理规则表时连同 scan 里的
// 区间分支一并删除。
//
// 两处编号形态不是编号，是编码的巧合：SVG path 数据里的坐标与 base64 载荷里的字节。
// 放行只在这两种语法上下文里生效，别处照判。
//
// 放行范围要窄，四个条件都收紧：
//   path 数据：引号里的整段，且整段是合法的 SVG path —— 首命令是 moveto、每个命令的参数个数合法、
//     只由命令字母与数字组成。J、D、U、R、W、E 不是 path 命令，「真的像编号」的形态在合法 path 里站不住。
//     引号只在三种语法位置里算数：d 属性、path(…) 函数、路径数组（每个元素都得是 path 数据）。
//   base64：data URI 里 ;base64, 之后连续的 base64 字符。
//   两种上下文都不跨行：跨行的形态一律不放行。

// SVG path 的命令字母与各自的参数个数（弧的 7 个参数、闭合命令不要参数）
const PATH_CMD = "MmZzLlHhVvCcSsQqTtAa";
const PATH_ARITY = { M: 2, L: 2, H: 1, V: 1, C: 6, S: 4, Q: 4, T: 2, A: 7, Z: 0 };
const BASE64_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";
const MAX_PATH_LEN = 512; // path 数据很短；上限挡住引号配对错位时把整段文件当 path 的情形

/**
 * 一段文本是不是 SVG path 数据：命令字母开头、参数个数合法、只由命令字母与数字组成。
 * 首命令必须是 moveto（SVG 规范要求路径以 moveto 开头）：这样「T04 12」这类以别的命令开头的
 * 形态站不住，不会被当成 path 放行。参数个数不对（只有一个数的）不算 path —— 那是编号。
 * movetoFirst 是可关的判据，自测要能单独把它关掉来对照。
 */
function isPathData(text, { movetoFirst = true } = {}) {
    const s = text.trim();
    if (!s || s.length > MAX_PATH_LEN) return false;
    if (movetoFirst && s[0] !== "M" && s[0] !== "m") return false;
    if (!PATH_CMD.includes(s[0])) return false;
    const num = /[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?/y;
    const toks = [];
    let i = 0;
    while (i < s.length) {
        const c = s[i];
        if (c === "," || /\s/.test(c)) {
            i++;
            continue;
        }
        if (PATH_CMD.includes(c)) {
            toks.push(c);
            i++;
            continue;
        }
        num.lastIndex = i;
        const m = num.exec(s);
        if (!m) return false;
        toks.push(0); // 参数
        i += m[0].length;
    }
    let k = 0;
    while (k < toks.length) {
        const cmd = toks[k];
        if (!cmd) return false; // 数字开头，不是 path
        const arity = PATH_ARITY[cmd.toUpperCase()];
        k++;
        let n = 0;
        while (k < toks.length && toks[k] === 0) {
            n++;
            k++;
        }
        if (arity === 0 ? n !== 0 : n === 0 || n % arity !== 0) return false;
    }
    return true;
}

/** 下一个同种引号（跳过被转义的那个）；没有就是 -1。 */
const nextQuote = (text, i) => {
    const q = text[i];
    for (let k = i + 1; k < text.length; k++) {
        if (text[k] === "\\") {
            k++;
            continue;
        }
        if (text[k] === q) return k;
    }
    return -1;
};

/** 与 text[i] 处的方括号配对的右括号；引号里的括号不算；没有就是 -1。 */
const closeBracket = (text, i) => {
    let depth = 0;
    for (let k = i; k < text.length; k++) {
        const c = text[k];
        if (c === "'" || c === '"') {
            const q = nextQuote(text, k);
            if (q < 0) return -1;
            k = q;
            continue;
        }
        if (c === "[") depth++;
        else if (c === "]" && --depth === 0) return k;
    }
    return -1;
};

/** 数组里按顶层逗号切开的元素；每项记下去掉空白后的起止（含引号）。 */
const arrayItems = (text, from, to) => {
    const items = [];
    let depth = 0;
    let start = from;
    for (let k = from; k <= to; k++) {
        const c = text[k];
        if (c === "'" || c === '"') {
            const q = nextQuote(text, k);
            if (q < 0) return items;
            k = q;
            continue;
        }
        if (c === "(" || c === "[") depth++;
        else if (c === ")" || c === "]") depth--;
        else if (c === "," && depth === 0) {
            items.push({ start, end: k });
            start = k + 1;
        }
    }
    items.push({ start, end: to + 1 });
    // 去掉元素两头的空白，剩下的才是引号包的整段
    return items
        .map((it) => {
            let s = it.start;
            let e = it.end;
            while (s < e && /\s/.test(text[s])) s++;
            while (e > s && /\s/.test(text[e - 1])) e--;
            return { start: s, end: e };
        })
        .filter((it) => it.end > it.start);
};

/** 引号里的内容是不是 path 数据；是就把内容区间记进 spans。 */
const quotedPathSpan = (text, quotePos, spans, opts) => {
    const close = nextQuote(text, quotePos);
    if (close < 0) return false;
    if (!isPathData(text.slice(quotePos + 1, close), opts)) return false;
    spans.push({ from: quotePos + 1, to: close });
    return true;
};

/**
 * 一行里 SVG path 上下文的字符区间（下标相对这一行）。
 * 只认三种语法位置：d 属性（HTML 的 d="…" 与脚本对象里的 d: '…'）、path(…) 函数、路径数组（每个元素都得是 path 数据）。
 * dColon 是可关的判据（对象写法 d: '…'），自测要能单独把它关掉来对照。
 */
export function pathSpans(line, opts) {
    const spans = [];
    if (!line.includes("'") && !line.includes('"')) return spans;
    const dAttr = opts?.dColon === false ? /\bd[ \t]*=[ \t]*(['"])/g : /\bd[ \t]*[=:][ \t]*(['"])/g;
    // d 属性与 path(…) 函数：引号里的内容必须是 path 数据
    for (const re of [dAttr, /\bpath[ \t]*\([ \t]*(['"])/g]) {
        for (const m of line.matchAll(re)) {
            quotedPathSpan(line, m.index + m[0].length - 1, spans, opts);
        }
    }
    // 路径数组：['M…', 'M…']，每个元素都得是 path 数据，否则整段不放行
    let i = 0;
    while (i < line.length) {
        if (line[i] !== "[") {
            i++;
            continue;
        }
        const close = closeBracket(line, i);
        if (close < 0) break;
        const here = [];
        let all = true;
        for (const it of arrayItems(line, i + 1, close - 1)) {
            const raw = line.slice(it.start, it.end);
            const q = raw[0];
            if ((q !== "'" && q !== '"') || raw[raw.length - 1] !== q) {
                all = false;
                break;
            }
            if (!quotedPathSpan(line, it.start, here, opts)) {
                all = false;
                break;
            }
            // 元素必须整个就是一个引号串：'M…' + '…' 这种拼接首尾也是引号，但不是 path 数组的元素
            if (opts?.wholeElement !== false && here[here.length - 1].to !== it.end - 1) {
                all = false;
                break;
            }
        }
        if (all) spans.push(...here);
        i = close + 1;
    }
    return spans;
}

/** 一行里 base64 上下文的字符区间：data URI 里 ;base64, 之后连续的 base64 字符。 */
export function base64Spans(line) {
    const spans = [];
    for (const m of line.matchAll(/data:[^\s"'<>]*?;base64,/gi)) {
        let end = m.index + m[0].length;
        while (end < line.length && BASE64_CHARS.includes(line[end])) end++;
        spans.push({ from: m.index + m[0].length, to: end });
    }
    return spans;
}

/** 一行里放行上下文的字符区间：path 数据与 base64 载荷。 */
export const contextSpans = (line, opts) => [...pathSpans(line, opts), ...base64Spans(line)];

// 规则的旗标：verbatimOk 逐字第三方目录里豁免；commentsOnly C/C++ 里只看注释与字符串；
// binary 二进制文件也扫（只放与格式无关、误报极低的规则）；group "ai" 在 fork 档只告警。
const rule = (id, src, flags, extra = {}) => ({ id, re: new RegExp(src, `g${flags}`), ...extra });
const V = { verbatimOk: true };
const B = { binary: true };
const VC = { ...V, commentsOnly: true };
const VB = { ...V, ...B };
const AI = { group: "ai", binary: true };

export const RULES = [
    rule("section", j("§", "\\s?\\d+(?:\\.\\d+)*"), "", { ...V, allow: sectionAllowed }),
    rule("plan-section", j("\\b\\d{2}\\s?", "§\\s?\\d"), "", V),
    rule("decision", j("(?:决", "策|裁", "决)\\s*[A-Z]?\\d"), ""),
    // 私有计划包的文件：编号文档（前面不是路径或日期，避免误伤 docs/01-overview.md、2026-11-05-x.md）、
    // plan/ 下的任何文件、状态与交接文件
    rule(
        "plan-file",
        j(
            "(?<![\\w/\\\\-])0\\d-[a-z][a-z-]*\\.md|",
            L,
            "(?:pl",
            "an[\\\\/][\\w.-]+\\.(?:md|json|js|mjs)|CHECK",
            "POINT\\.md|CLAUDE-DESIGN-HAND",
            "OFF\\.md|car",
            "ds\\.json)",
        ),
        "",
    ),
    // 私有目录与私有仓名
    rule("private-name/scratch", j(L, "_scr", "atch", R), "i"),
    rule("private-name/ref", j(L, "_re", "f[\\\\/]"), "i"),
    rule("private-name/plan-repo", j(L, "master", "[-_]?plan", R), "i"),
    rule("private-name/site-repo", j("DLsnows[\\\\/]Syn", "chain", R), "i"),
    rule("private-name/workspace", j(L, "loop", "back", SEP, "(?:docs|plan|wt|_\\w+)"), "i"),
    rule("private-name/miciri-workspace", j(L, "check", "Voc", R), "i"),
    rule("private-name/research", j(L, "_rese", "arch", R), "i"),
    // 本机路径与主机名：逐字第三方文本里也不豁免，二进制文件里也扫
    rule("win-user-path", j(DRIVE, "Us", "ers", SEP, NAME), "i", {
        ...B,
        allow: (m) => placeholder(m[1]) || /^(?:Public|Default|All)$/i.test(m[1]),
    }),
    rule("home-path", j("(?<![\\w:])/(?:Us", "ers|ho", "me)/", NAME), "", {
        ...B,
        allow: (m) => placeholder(m[1]) || m[1] === "runner" || m[1] === "Shared",
    }),
    rule(
        "personal-email",
        j("[A-Za-z0-9._%+-]+", "@(?:[A-Za-z0-9-]+\\.)*(?:", MAIL_HOSTS, ")\\.(?:", MAIL_TLDS, ")\\b"),
        "i",
        VB,
    ),
    rule("hostname", j("\\b(?:DESK", "TOP|LAP", "TOP)-(?=[A-Z0-9]*\\d)[A-Z0-9]{5,}\\b"), "i", B),
    rule("mac-host", j("\\b[\\w-]*(?:Mac", "Book|iMac|Mac-?mini|Mac-?Studio)[\\w-]*\\.loc", "al\\b"), "i", B),
    // AI 署名（按 git 尾注的写法：键前可有空白，冒号前也可有空白）
    rule("ai-coauthor", j("^\\s*co-authored", "-by\\s*:.*(?:cla", "ude|anthr", "opic)"), "i", AI),
    rule("ai-session", j("^\\s*cla", "ude-session\\s*:"), "i", AI),
    rule("ai-generated", j("generated (?:with|by) \\[?cla", "ude"), "i", AI),
    rule("ai-email", j("noreply@anthr", "opic\\.com"), "i", AI),
    rule("ai-url", j("cla", "ude\\.ai/code|cla", "ude\\.com/cla", "ude-code"), "i", AI),
    rule("ai-robot", "\\u{1F916}", "u", AI),
    // 写错的许可证标识：JUCE 部分是 AGPLv3 only
    rule("wrong-license", j("AGPL-3\\.0", "-or-later"), "i"),
];

const escapeRe = (s) => s.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
const personalRule = (needles) => ({
    id: "personal",
    re: new RegExp(needles.map(escapeRe).join("|"), "gi"),
    binary: true,
    needles,
});

/**
 * C/C++：只保留注释、字符串与字符字面量（含跨行的原始字符串 R"x(…)x"），其余字符换成空格，列号不变；
 * 块注释跨行。#error / #warning 的文字与 #if 0 … #endif 整段原样保留：那里写的是给人看的文字，不是代码。
 */
export function cVisible(text) {
    let state = "code";
    let rawEnd = "";
    let if0 = 0; // #if 0 区段内的嵌套深度
    return text.split(/\r?\n/).map((line) => {
        if (if0) {
            if (/^\s*#\s*if/.test(line)) if0++;
            else if (/^\s*#\s*endif\b/.test(line)) if0--;
            else if (if0 === 1 && /^\s*#\s*(?:else|elif)\b/.test(line)) if0 = 0;
            return line;
        }
        if (state === "str" || state === "chr") state = "code"; // 普通字符串不跨行
        if (state === "code" && /^\s*#\s*(?:if\s+0\b|error\b|warning\b)/.test(line)) {
            if (/^\s*#\s*if/.test(line)) if0 = 1;
            return line;
        }
        let out = "";
        for (let i = 0; i < line.length; i++) {
            const c = line[i];
            if (state === "block" || state === "raw") {
                const end = state === "block" ? "*/" : rawEnd;
                if (line.startsWith(end, i)) {
                    out += end;
                    i += end.length - 1;
                    state = "code";
                } else {
                    out += c;
                }
                continue;
            }
            if (state === "str" || state === "chr") {
                if (c === "\\" && i + 1 < line.length) {
                    out += line.slice(i, i + 2);
                    i++;
                    continue;
                }
                if (c === (state === "str" ? '"' : "'")) state = "code";
                out += c;
                continue;
            }
            const rest = line.slice(i);
            const afterWord = /\w/.test(line[i - 1] || "");
            const raw = afterWord ? null : /^(?:u8|[uUL])?R"([^()\\\s]{0,16})\(/.exec(rest);
            if (rest.startsWith("//")) {
                out += rest;
                break;
            } else if (rest.startsWith("/*") || raw) {
                const open = raw ? raw[0] : "/*";
                out += open;
                i += open.length - 1;
                state = raw ? "raw" : "block";
                rawEnd = raw ? `)${raw[1]}"` : "";
            } else if (c === '"' || (c === "'" && !afterWord)) {
                // 数字分隔符 1'000 前面是数字，不当字符字面量
                out += c;
                state = c === '"' ? "str" : "chr";
            } else {
                out += " ";
            }
        }
        return out;
    });
}

/** 以章节号开头的 Markdown 标题里的号码：同一文件内引用这些章节时放行。 */
export const headingsOf = (text) =>
    new Set([...text.matchAll(new RegExp(j("^#{1,6}\\s*", "§\\s?(\\d+)"), "gm"))].map((m) => m[1]));

/**
 * 扫一段文本。ctx.path 给出时按文件处理（逐字目录豁免、C/C++ 的 commentsOnly），
 * 否则按 PR / 提交文本处理，没有任何路径豁免。返回 [{ rule, line, col, match }]。
 */
export function scan(text, ctx = {}, opts = {}) {
    const {
        rules = RULES,
        personal = [],
        verbatimDirs = VERBATIM_DIRS,
        codeFile = CODE_FILE,
        contextSpans: spansOf = contextSpans,
    } = opts;
    const p = ctx.path || "";
    const verbatim = !!ctx.path && verbatimDirs.some((d) => p.startsWith(d));
    const visible = ctx.path && codeFile.test(p) ? cVisible(text) : null;
    const all = personal.length ? [...rules, personalRule(personal)] : rules;
    const aware = all.some((r) => r.contextAware);
    const out = [];
    text.split(/\r?\n/).forEach((line, i) => {
        // 放行上下文按行算，一行只算一次：C/C++ 里算的是保留注释与字符串后的那份，行长与原行一致
        const spanCache = new Map();
        const envFor = (src) => {
            if (!aware) return undefined;
            let spans = spanCache.get(src);
            if (spans === undefined) {
                spans = spansOf(src);
                spanCache.set(src, spans);
            }
            return { spans };
        };
        for (const r of all) {
            if (r.verbatimOk && verbatim) continue;
            const src = r.commentsOnly && visible ? visible[i] : line;
            const env = envFor(src);
            for (const m of src.matchAll(r.re)) {
                if (r.allow?.(m, src, ctx, env)) continue;
                out.push({ rule: r, line: i + 1, col: m.index + 1, match: m[0] });
            }
        }
    });
    return out;
}

const TEXT_RUN = /[^\x00-\x08\x0e-\x1f\x7f\ufffd]{4,}/g;
const ASCII_RUN = /[\x20-\x7e]{4,}/g;

/**
 * 扫一个文件的原始字节。能按 BOM 或 UTF-8 解码的当文本扫；二进制（PNG 的文本块、WAV 的 BWF 头、
 * PDF 的作者字段都可能带本机信息）只跑 binary 规则，取 UTF-8 与两种对齐的 UTF-16LE 可打印串。
 * 二进制命中的 line 为 0。
 */
export function scanBlob(buf, ctx = {}, opts = {}) {
    const text = decodeText(buf);
    if (text !== null) return scan(text, { headings: headingsOf(text), ...ctx }, opts);
    const rules = (opts.rules || RULES).filter((r) => r.binary);
    const views = [
        buf.toString("utf8").match(TEXT_RUN),
        buf.toString("utf16le").match(ASCII_RUN),
        buf.subarray(1).toString("utf16le").match(ASCII_RUN),
    ];
    const seen = new Set();
    const out = [];
    for (const view of views) {
        for (const f of scan((view || []).join("\n"), ctx, { ...opts, rules })) {
            const key = `${f.rule.id}\0${f.match.toLowerCase()}`;
            if (seen.has(key)) continue;
            seen.add(key);
            out.push({ ...f, line: 0, col: 0 });
        }
    }
    return out;
}

/** 打码：个人针只给序号；其余只留前两个字符。CI 日志公开且长期保留，回显原文等于替它发布一次。 */
export function redact(f) {
    if (f.rule.needles) {
        const n = f.rule.needles.findIndex((x) => x.toLowerCase() === f.match.toLowerCase());
        return `个人针 #${n + 1}`;
    }
    return f.match.slice(0, 2) + "*".repeat(Math.max(0, f.match.length - 2));
}

/** 单行文本（路径、ref 名）里命中的片段整段换成 *，用于必须原样回显名字的地方。 */
export function mask(text, personal = []) {
    const hide = new Set();
    for (const f of scan(text, {}, { personal })) {
        if (f.line === 1) for (let k = 0; k < f.match.length; k++) hide.add(f.col - 1 + k);
    }
    return text
        .split("")
        .map((c, k) => (hide.has(k) ? "*" : c))
        .join("");
}

/**
 * 公开文本与针表的输入关：拒绝 UTF-16（会让每条针都变成夹着 NUL 的串，永远匹配不上）
 * 和 NUL、控制字符；去掉 UTF-8 BOM。buf 可以是 Buffer 或字符串。
 */
export function plainText(buf, label) {
    const b = Buffer.isBuffer(buf) ? buf : Buffer.from(String(buf), "utf8");
    if ((b[0] === 0xff && b[1] === 0xfe) || (b[0] === 0xfe && b[1] === 0xff)) {
        throw new Error(`${label} 是 UTF-16，先转成 UTF-8`);
    }
    const bom = b[0] === 0xef && b[1] === 0xbb && b[2] === 0xbf ? 3 : 0;
    const text = b.subarray(bom).toString("utf8");
    const bad = /[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]/.exec(text);
    if (bad) {
        // 只报位置与字符码，不回显内容（终端里粘过来的 ANSI 颜色码就是常见的来源）
        const before = text.slice(0, bad.index);
        const code = bad[0].charCodeAt(0).toString(16).toUpperCase().padStart(4, "0");
        const where = `第 ${before.split("\n").length} 行，字节偏移 ${bom + Buffer.byteLength(before)}`;
        throw new Error(`${label} 含控制字符 U+${code}（${where}）`);
    }
    return text;
}

const parseNeedles = (s) =>
    s
        .split(/\r?\n/)
        .map((l) => l.trim())
        .filter((l) => l && !l.startsWith("#"));

/**
 * 个人针的来源由调用方显式给出（--needles），不看环境变量猜自己在不在 CI：
 *   local        （默认；hook、gates、本机手动运行）MICIRI_PRIVACY_LOCAL 或 git config miciri.privacyLocal
 *                指向的仓外文件，未配置、读不到、没有针都判红。
 *   ci           CI 上同仓 PR 与 push 事件：环境变量 PRIVACY_NEEDLES（每行一条），缺失或为空判红。
 *   ci-optional  CI 默认：有就用，没有就打印注记、只跑通用规则（缺 secret 不判红；fork PR 与 dependabot 也读不到它）。
 */
export const NEEDLE_MODES = ["local", "ci", "ci-optional"];

export function needlesMode(argv) {
    const i = argv.indexOf("--needles");
    if (i < 0) return "local";
    if (!NEEDLE_MODES.includes(argv[i + 1])) throw new Error(`--needles 只能是 ${NEEDLE_MODES.join(" / ")}`);
    return argv[i + 1];
}

export function loadPersonalNeedles({ env = process.env, cwd = process.cwd(), mode = "local" } = {}) {
    if (!NEEDLE_MODES.includes(mode)) throw new Error(`未知的个人针来源 ${mode}`);
    if (mode === "local") {
        const file = localSetting("MICIRI_PRIVACY_LOCAL", "miciri.privacyLocal", { env, cwd });
        if (!file) throw new Error("未配置个人针：设置 MICIRI_PRIVACY_LOCAL 或 git config miciri.privacyLocal");
        if (!fs.existsSync(file)) throw new Error("MICIRI_PRIVACY_LOCAL 指向的个人针文件不存在");
        const needles = parseNeedles(plainText(fs.readFileSync(file), "个人针文件"));
        if (!needles.length) throw new Error("个人针文件里没有任何针");
        return { needles, notes: [] };
    }
    const needles = parseNeedles(plainText(env.PRIVACY_NEEDLES || "", "PRIVACY_NEEDLES"));
    if (needles.length) return { needles, notes: [] };
    if (mode === "ci") throw new Error("--needles ci：PRIVACY_NEEDLES 缺失或为空（同仓 PR 与 push 事件必须提供）");
    return { needles, notes: ["::notice::PRIVACY_NEEDLES 未提供（可选 secret），本次只跑通用规则"] };
}

// ---- 自测夹具 ------------------------------------------------------------------------

/** 只用于自测的假针。真实的个人针永远不进仓库。 */
export const FAKE_NEEDLE = "miciri-selftest-fake-needle-7c1e";
const ROBOT = String.fromCodePoint(0x1f916);

/** 每条规则一个标准坏样例（PR / 提交文本形态）。下面按旗标把它们组合进逐字目录、C/C++ 与二进制夹具。 */
const SAMPLES = {
    section: j("详见 §", "4.2 的说明"),
    "plan-section": j("见 0", "6 §", "3"),
    decision: j("按决", "策 R5"),
    "plan-file": j("见 0", "4-execution.md"),
    "private-name/scratch": j("_scr", "atch/notes"),
    "private-name/ref": j("_re", "f/upstream"),
    "private-name/plan-repo": j("scvb-master", "plan"),
    "private-name/site-repo": j("DLsnows/Syn", "chain"),
    "private-name/workspace": j("loop", "back/plan/x"),
    "private-name/miciri-workspace": j("check", "Voc/plan"),
    "private-name/research": j("_rese", "arch/03-ci.md"),
    "win-user-path": j("D:", "\\", "Us", "ers", "\\", "someone", "\\", "proj"),
    "home-path": j("/Us", "ers/someone/Music"),
    "personal-email": j("someone", "@", "gmail", ".com"),
    hostname: j("DESK", "TOP-AB12CD3"),
    "mac-host": j("someones-Mac", "Book-Pro.loc", "al"),
    "ai-coauthor": j("Co-Authored", "-By: Cla", "ude <x@example.com>"),
    "ai-session": j("Cla", "ude-Session: abc"),
    "ai-generated": j("Generated with [Cla", "ude Code]"),
    "ai-email": j("noreply@anthr", "opic.com"),
    "ai-url": j("https://cla", "ude.ai/code"),
    "ai-robot": `${ROBOT} done`,
    "wrong-license": j("AGPL-3.0", "-or-later"),
    personal: `日志里出现 ${FAKE_NEEDLE}`,
};

// [应命中的规则 id 或 id 数组（null = 必须完全干净）, 文本或 Buffer, 路径（省略 = PR 或提交文本）]
export const CASES = [
    ["decision", j("决", "策 K1")],
    ["decision", j("用户决", "策 D", "4 已定")],
    // 章节号与文件名
    [null, "按 GPLv3 §6(d) 提供对应源码；JUCE 部分适用 AGPLv3（GPLv3 §13）；IEC 61672 §5.2"],
    [null, j("## §", "3 分支\n见 §", "3 的说明"), "CLAUDE.md"],
    [null, "docs/validation/2026-11-05-notes.md 与 docs/01-overview.md"],
    ["private-name/plan-repo", j("scvb_master", "plan")],
    ["private-name/plan-repo", j("scvb-master", "_plan")],
    ["plan-file", j("见 CHECK", "POINT.md")],
    ["plan-file", j("CLAUDE-DESIGN-HAND", "OFF.md")],
    ["plan-file", j("见 car", "ds.json")],
    ["plan-file", j("见 0", "7-roadmap.md")],
    ["plan-file", j("pl", "an/04-execution.md")],
    ["private-name/workspace", j("loop", "back", "\\", "plan", "\\", "x")],
    [null, "audio loopback device"],
    // 本机路径
    ["win-user-path", j("c:/", "us", "ers/", "someone")],
    ["win-user-path", j("C:", "\\\\", "Us", "ers", "\\\\", "someone")],
    ["win-user-path", j("C:", "\\\\\\\\", "Us", "ers", "\\\\\\\\", "someone")],
    ["win-user-path", j("/c/", "Us", "ers/", "someone")],
    ["win-user-path", j("/mnt/c/", "Us", "ers/", "someone")],
    ["win-user-path", j("\\\\", "localhost", "\\", "c$", "\\", "Us", "ers", "\\", "someone")],
    ["win-user-path", j("C%3A%5C", "Us", "ers%5C", "someone%5Cx")],
    [null, j("C:", "\\", "Us", "ers", "\\", "<user>", "\\", "x 与 C:/", "Us", "ers/%USERNAME%/x")],
    [
        null,
        j("C:", "\\", "Us", "ers", "\\", "Public", "\\", "Documents", "\\", "VST3 Presets 与 C:/", "Us", "ers/Default"),
    ],
    [null, "C:\\Program Files\\Common Files\\VST3"],
    ["home-path", j("/ho", "me/someone/.config")],
    [null, j("/Us", "ers/runner/work、/Us", "ers/Shared/x、/ho", "me/runner/work")],
    // 邮箱
    ["personal-email", j("someone", "@", "vip.", "qq", ".com")],
    ["personal-email", j("someone", "@", "googlemail", ".com")],
    ["personal-email", j("someone", "@", "outlook", ".co.uk")],
    ["personal-email", j("someone", "@", "proton", ".me")],
    [null, "联系 contact@synchain.ca；noreply@github.com；49699333+dependabot[bot]@users.noreply.github.com"],
    // 主机名与 AI 署名的变体
    ["hostname", j("lap", "top-z9y8x7w")],
    [null, "desktop-friendly layout；settings.local.json"],
    ["ai-coauthor", j("Co-authored", "-by : Cla", "ude <x@example.com>")],
    ["ai-coauthor", j("  Co-authored", "-by: Cla", "ude <x@example.com>")],
    ["ai-generated", j("Generated by Cla", "ude Code")],
    ["ai-url", j("https://cla", "ude.com/cla", "ude-code")],
    [null, "JUCE 部分：AGPL-3.0-only"],
    [null, "【重要】修复 D 合约越界；【红旗】已清零；【建议】挂账"],
    ["personal", `日志里出现 ${FAKE_NEEDLE.toUpperCase()}`],
    ["private-name/research", j("见 _rese", "arch/x.md")],
    ["private-name/miciri-workspace", j("cd check", "Voc && ls")],
    [null, "the research notes; a Voc-less checklist"],
    ["plan-section", j("见 0", "2 §", "7"), "docs/x.md"],
    ["decision", j("按决", "策 7"), "docs/x.md"],
    [
        "win-user-path",
        Buffer.concat([Buffer.from([0xff, 0xfe]), Buffer.from(SAMPLES["win-user-path"], "utf16le")]),
        "docs/log.txt",
    ],
    // 文件编码：UTF-16 文本按 BOM 解码后全规则照扫
];

/** 由标准样例按旗标生成的夹具：每条规则单独一条，再按逐字目录、C/C++、二进制三种处境各组合一次。 */
function sampleCases() {
    const ids = [...RULES.map((r) => r.id), "personal"];
    const missing = ids.filter((id) => !(id in SAMPLES));
    const flag = (id, k) => RULES.find((r) => r.id === id)?.[k] ?? (id === "personal" && k === "binary");
    const pick = (k, on) => ids.filter((id) => !!flag(id, k) === on);
    const text = (list) => list.map((id) => SAMPLES[id]).join("\n");
    // 二进制夹具：样例夹在 NUL 与高位字节之间，像 PNG 文本块或 WAV 头里的字段
    const blob = (list) => Buffer.concat(list.flatMap((id) => [Buffer.from([0, 0x89, 0]), Buffer.from(SAMPLES[id])]));
    return [
        ...missing.map((id) => [`缺标准样例：${id}`, "", undefined]),
        ...ids.map((id) => [id, SAMPLES[id]]),
        [pick("verbatimOk", false), text(pick("verbatimOk", false)), "third_party/notices/README.md"],
        [null, text(pick("verbatimOk", true)), "third_party/notices/upstream.txt"],
        // 放进 C++ 代码里时去掉 URL 里的 //，否则那一行余下的部分会被当成注释
        [pick("commentsOnly", false), text(pick("commentsOnly", false)).replaceAll("://", ":"), "src/core/Strings.cpp"],
        [null, text(pick("commentsOnly", true)), "src/core/Code.cpp"],
        [pick("binary", true), blob(pick("binary", true)), "docs/shot.png"],
    ];
}

/** 跑夹具；opts 可以替换规则或豁免（自测用）。返回每条夹具的结果。 */
export function runCases(opts = {}) {
    return [...sampleCases(), ...CASES].map(([want, input, path]) => {
        const o = { personal: [FAKE_NEEDLE], ...opts };
        const found = Buffer.isBuffer(input)
            ? scanBlob(input, { path }, o)
            : scan(input, path ? { path, headings: headingsOf(input) } : {}, o);
        const ids = new Set(found.map((f) => f.rule.id));
        const wants = want === null ? [] : [].concat(want);
        const passed = want === null ? ids.size === 0 : wants.every((w) => ids.has(w));
        const shown = Buffer.isBuffer(input) ? `<${input.length} 字节>` : JSON.stringify(input).slice(0, 48);
        return { label: `${want === null ? "干净" : wants.join(",")}：${shown}`, bad: want !== null, passed };
    });
}
