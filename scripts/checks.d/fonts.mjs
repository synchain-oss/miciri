// SPDX-License-Identifier: GPL-3.0-or-later
//
// 界面字体门禁。web/fonts/ 里的四个 woff2 是 OFL-1.1 的子集，插件离线，页面缺字或字体对不上就是方块或回退，
// 许可上也有硬要求，这些都靠人眼审 diff 看不出来，所以直接读 woff2 的 name 与 cmap 表核对（零依赖：woff2 的
// name、cmap、OS/2、fvar 表不做变换，Node 自带的 brotli 就够解出来）。判据：
//   1. 四个字体都在、都能解析；web/ 下不许有这四个之外的字体文件（没有登记许可与来源）；
//   2. name 表：族名（有排版族名 16 就看它，否则看 1）与 PostScript 名（6）等于登记值；署名记录 0、13、14 都在；
//      署名记录（0、7、13、14）以外不得出现上游的保留字体名（Plex 与 Source，OFL-1.1 第 3 条，大小写不敏感）；
//   3. nameID 0 的版权行去掉 Copyright、(c)、© 前缀后，与 REUSE.toml 里该文件的登记逐字相同；
//   4. web/ 下 js、mjs、html、css 去掉注释后的每个非 ASCII 字符，四个字体合起来至少有一个有字形；
//   5. 每个 @font-face 指向 web/fonts/ 里的登记文件，族名等于登记值，声明的字重区间盖住内嵌的字重；四个字体都有声明；
//   6. 单个 woff2 ≤ 1.5 MiB，合计 ≤ 2 MiB：它们编进每个插件二进制，字符集放宽之前先想想这个代价。
// 期望值（文件名、族名、PostScript 名、保留字体名）与 scripts/fetch_fonts.py 的 OUTPUTS 各存一份，改字体时两边一起改。
// 注释剥离与 fetch_fonts.py 是同一套算法，重跑字体后字符覆盖由这里把关。重新生成见 web/fonts/README.md。

import fs from "node:fs";
import path from "node:path";
import zlib from "node:zlib";
import { caseResult } from "../lib/check-kit.mjs";

/** 登记的四个字体。reserved 是上游的保留字体名，没有就是 null。 */
export const FONTS = [
    { file: "SpaceGrotesk.woff2", family: "Space Grotesk", ps: "SpaceGrotesk-SemiBold", reserved: null },
    { file: "MiciriSans.woff2", family: "Miciri Sans", ps: "MiciriSans-Regular", reserved: "Plex" },
    { file: "MiciriMono.woff2", family: "Miciri Mono", ps: "MiciriMono-Regular", reserved: "Plex" },
    { file: "NotoSansSC.woff2", family: "Noto Sans SC", ps: "NotoSansSC-Light", reserved: "Source" },
];
export const MAX_FILE = 1536 * 1024;
export const MAX_TOTAL = 2048 * 1024;
/** 署名记录：版权、商标、许可证声明与网址。随分发逐字保留，不参与保留字体名复核。 */
const ATTRIBUTION = [0, 7, 13, 14];
const FONT_EXT = /\.(?:woff2?|ttf|otf|eot)$/i;

// ---- WOFF2 解析：只取 name、cmap、OS/2、fvar -----------------------------------
// WOFF2 表目录的已知标签，下标就是目录项低 6 位（63 表示后面跟 4 字节的自定义标签）。
const KNOWN_TAGS =
    "cmap head hhea hmtx maxp name OS/2 post cvt_ fpgm glyf loca prep CFF_ VORG EBDT EBLC gasp hdmx kern LTSH PCLT VDMX vhea vmtx BASE GDEF GPOS GSUB EBSC JSTF MATH CBDT CBLC COLR CPAL SVG_ sbix acnt avar bdat bloc bsln cvar fdsc feat fmtx fvar gvar hsty just lcar mort morx opbd prop trak Zapf Silf Glat Gloc Feat Sill"
        .split(" ")
        .map((t) => t.replace("_", " "));

function readTables(buf) {
    if (buf.length < 48 || buf.toString("latin1", 0, 4) !== "wOF2") throw new Error("不是 WOFF2 文件");
    const numTables = buf.readUInt16BE(12);
    const compressed = buf.readUInt32BE(20);
    let pos = 48;
    const base128 = () => {
        let v = 0;
        for (let i = 0; i < 5; i++) {
            if (pos >= buf.length) throw new Error("表目录被截断");
            const b = buf[pos++];
            v = v * 128 + (b & 0x7f);
            if (!(b & 0x80)) return v;
        }
        throw new Error("表目录里的长度字段过长");
    };
    const dir = [];
    for (let i = 0; i < numTables; i++) {
        if (pos >= buf.length) throw new Error("表目录被截断");
        const flags = buf[pos++];
        let tag = KNOWN_TAGS[flags & 0x3f];
        if ((flags & 0x3f) === 63) {
            tag = buf.toString("latin1", pos, pos + 4);
            pos += 4;
        }
        const version = flags >> 6;
        const length = base128();
        // glyf 与 loca 的变换版本 0 才是做了变换；其他表的版本 0 是未变换
        const transformed = tag === "glyf" || tag === "loca" ? version === 0 : version !== 0;
        dir.push({ tag, length: transformed ? base128() : length, transformed });
    }
    const data = zlib.brotliDecompressSync(buf.subarray(pos, pos + compressed));
    const tables = {};
    let off = 0;
    for (const t of dir) {
        tables[t.tag] = t.transformed ? null : data.subarray(off, off + t.length);
        off += t.length;
    }
    return tables;
}

function parseName(t) {
    const count = t.readUInt16BE(2);
    const strings = t.readUInt16BE(4);
    const out = [];
    for (let i = 0; i < count; i++) {
        const o = 6 + 12 * i;
        const [platform, , , id, length, offset] = [0, 2, 4, 6, 8, 10].map((k) => t.readUInt16BE(o + k));
        const raw = Buffer.from(t.subarray(strings + offset, strings + offset + length));
        if (raw.length !== length) throw new Error("name 表被截断");
        out.push({ id, value: platform === 1 ? raw.toString("latin1") : raw.swap16().toString("utf16le") });
    }
    return out;
}

/** 码位集合：只收 Unicode 子表（格式 4 与 12），字形为 0 的不算有字形。 */
function parseCmap(t) {
    const set = new Set();
    const subtables = t.readUInt16BE(2);
    for (let i = 0; i < subtables; i++) {
        const platform = t.readUInt16BE(4 + 8 * i);
        const encoding = t.readUInt16BE(6 + 8 * i);
        const base = t.readUInt32BE(8 + 8 * i);
        if (!(platform === 0 || (platform === 3 && (encoding === 1 || encoding === 10)))) continue;
        const format = t.readUInt16BE(base);
        if (format === 4) {
            const segs = t.readUInt16BE(base + 6) / 2;
            const ends = base + 14;
            const starts = ends + 2 * segs + 2;
            const deltas = starts + 2 * segs;
            const ranges = deltas + 2 * segs;
            for (let s = 0; s < segs; s++) {
                const end = t.readUInt16BE(ends + 2 * s);
                const start = t.readUInt16BE(starts + 2 * s);
                const delta = t.readUInt16BE(deltas + 2 * s);
                const range = t.readUInt16BE(ranges + 2 * s);
                for (let c = start; c <= end && c < 0xffff; c++) {
                    const gid =
                        range === 0 ? (c + delta) & 0xffff : t.readUInt16BE(ranges + 2 * s + range + 2 * (c - start));
                    if (gid !== 0) set.add(c);
                }
            }
        } else if (format === 12) {
            const groups = t.readUInt32BE(base + 12);
            for (let g = 0; g < groups; g++) {
                const o = base + 16 + 12 * g;
                const start = t.readUInt32BE(o);
                const end = t.readUInt32BE(o + 4);
                const glyph = t.readUInt32BE(o + 8);
                for (let c = start; c <= end; c++) if (glyph + (c - start) !== 0) set.add(c);
            }
        } else {
            throw new Error(`cmap 子表格式 ${format} 不认识`);
        }
    }
    return set;
}

/** 内嵌的字重区间：有 fvar 的 wght 轴就取轴的范围，否则是 OS/2 的 usWeightClass。 */
function parseWeight(tables) {
    const fvar = tables.fvar;
    if (fvar) {
        const first = fvar.readUInt16BE(4);
        const count = fvar.readUInt16BE(8);
        const size = fvar.readUInt16BE(10);
        for (let i = 0; i < count; i++) {
            const o = first + size * i;
            if (fvar.toString("latin1", o, o + 4) === "wght")
                return [fvar.readInt32BE(o + 4) / 65536, fvar.readInt32BE(o + 12) / 65536];
        }
    }
    const w = tables["OS/2"].readUInt16BE(4);
    return [w, w];
}

/** 读一个 woff2：返回 { names: [{ id, value }], cmap: Set<码位>, weight: [低, 高] }；读不出来就抛错。 */
export function readFont(buf) {
    const tables = readTables(buf);
    for (const tag of ["name", "cmap", "OS/2"]) if (!tables[tag]) throw new Error(`缺 ${tag} 表（或被变换，读不了）`);
    return { names: parseName(tables.name), cmap: parseCmap(tables.cmap), weight: parseWeight(tables) };
}

// ---- 判据 ------------------------------------------------------------------
const values = (info, id) => [...new Set(info.names.filter((r) => r.id === id).map((r) => r.value))];
const stripCopyright = (s) => s.trim().replace(/^(?:Copyright\s*)?(?:\(c\)|©)?\s*/i, "");

/** 一个字体的 name 表：登记名、署名记录、保留字体名与 REUSE 登记。reuseLine 是 REUSE.toml 里该文件的版权行（没有就是 undefined）。 */
export function nameProblems(spec, info, reuseLine) {
    const out = [];
    const at = (msg) => out.push(`${spec.file}：${msg}`);
    const family = values(info, 16).length ? 16 : 1;
    const gotFamily = values(info, family);
    if (gotFamily.length !== 1 || gotFamily[0] !== spec.family) {
        at(`nameID ${family} 应为 ${JSON.stringify(spec.family)}，实际 ${JSON.stringify(gotFamily)}`);
    }
    const gotPs = values(info, 6);
    if (gotPs.length !== 1 || gotPs[0] !== spec.ps)
        at(`nameID 6 应为 ${JSON.stringify(spec.ps)}，实际 ${JSON.stringify(gotPs)}`);
    for (const [id, what] of [
        [0, "版权"],
        [13, "许可证声明"],
        [14, "许可证网址"],
    ]) {
        if (!values(info, id).some((v) => v.trim())) at(`缺 nameID ${id}（${what}），OFL-1.1 第 2 条要求随分发物保留`);
    }
    if (spec.reserved) {
        const needle = spec.reserved.toLowerCase();
        for (const r of info.names) {
            if (!ATTRIBUTION.includes(r.id) && r.value.toLowerCase().includes(needle)) {
                at(
                    `nameID ${r.id} 含保留字体名 ${JSON.stringify(spec.reserved)}：${JSON.stringify(r.value)}（OFL-1.1 第 3 条）`,
                );
            }
        }
    }
    const copyright = values(info, 0)[0];
    if (reuseLine === undefined) at("REUSE.toml 里没有这个字体文件的块");
    else if (copyright !== undefined && stripCopyright(copyright) !== reuseLine.trim()) {
        at(`REUSE.toml 的版权行 ${JSON.stringify(reuseLine)} 与 nameID 0 ${JSON.stringify(copyright)} 不一致`);
    }
    return out;
}

/** 必需字符里四个字体都没有字形的（调用方给出每个字符是否有字形）。 */
export function coverageProblems(required, have) {
    const missing = [...required].filter((c) => !have.has(c.codePointAt(0)));
    if (!missing.length) return [];
    const list = missing
        .sort()
        .map((c) => `${c}(U+${c.codePointAt(0).toString(16).toUpperCase().padStart(4, "0")})`)
        .join(" ");
    return [
        `${missing.length} 个界面用到的字符四个字体都没有字形，运行期会落到系统字体或方块：${list}。重跑 scripts/fetch_fonts.py`,
    ];
}

/** @font-face 声明：faces 是 parseFontFaces 的结果（附 css 路径），infos 是 { 文件名: readFont 的结果 }。 */
export function cssProblems(faces, infos) {
    const out = [];
    const seen = new Set();
    for (const f of faces) {
        const at = (msg) => out.push(`${f.css}：@font-face ${JSON.stringify(f.family ?? "?")} ${msg}`);
        if (!f.family || !f.urls.length || !f.weight) {
            at("缺 font-family、src 或 font-weight");
            continue;
        }
        for (const url of f.urls) {
            const target = path.posix.normalize(path.posix.join(path.posix.dirname(f.css), url));
            const spec = FONTS.find((s) => target === `web/fonts/${s.file}`);
            if (!spec) {
                at(`的 src ${url} 不是 web/fonts/ 里登记的字体`);
                continue;
            }
            seen.add(spec.file);
            if (f.family !== spec.family) at(`的族名应为 ${JSON.stringify(spec.family)}（${spec.file} 的登记名）`);
            const have = infos[spec.file]?.weight;
            if (have && (f.weight[0] > have[0] || f.weight[1] < have[1])) {
                at(
                    `声明的字重 ${f.weight.join("–")} 没有盖住 ${spec.file} 内嵌的 ${have[0] === have[1] ? have[0] : have.join("–")}`,
                );
            }
        }
    }
    for (const s of FONTS) if (!seen.has(s.file)) out.push(`没有任何 @font-face 引用 ${s.file}`);
    return out;
}

export function sizeProblems(sizes) {
    const out = [];
    let total = 0;
    for (const [file, size] of Object.entries(sizes)) {
        total += size;
        if (size > MAX_FILE) out.push(`${file}：${size} 字节，单个上限 ${MAX_FILE}`);
    }
    if (total > MAX_TOTAL) out.push(`四个字体合计 ${total} 字节，上限 ${MAX_TOTAL}`);
    return out;
}

/** web/ 下的字体文件必须是登记的那四个。paths 是仓内相对路径。 */
export function strayProblems(paths) {
    return paths
        .filter((p) => FONT_EXT.test(p) && !FONTS.some((s) => p === `web/fonts/${s.file}`))
        .map((p) => `${p}：不是登记的界面字体；新字体先补许可、来源与 REUSE.toml 的块，再改本检查的登记表`);
}

/** REUSE.toml 里 { 路径: 版权行 }：只认 [[annotations]] 块里单字符串的 SPDX-FileCopyrightText，后面的块盖前面的。 */
export function reuseCopyrights(text) {
    const out = {};
    for (const block of text.split(/^\[\[annotations\]\]\s*$/m).slice(1)) {
        const c = /^SPDX-FileCopyrightText\s*=\s*"((?:[^"\\]|\\.)*)"/m.exec(block);
        const p = /^path\s*=\s*(\[[^\]]*\]|"[^"]*")/m.exec(block);
        if (!c || !p) continue;
        for (const m of p[1].matchAll(/"([^"]+)"/g)) out[m[1]] = c[1].replace(/\\"/g, '"');
    }
    return out;
}

/** 去掉注释的 CSS 里的 @font-face 声明。 */
export function parseFontFaces(css, cssPath) {
    const text = css.replace(/\/\*[\s\S]*?\*\//g, " ");
    return [...text.matchAll(/@font-face\s*\{([^}]*)\}/g)].map((m) => {
        const body = m[1];
        const w = /font-weight\s*:\s*(\d+)(?:\s+(\d+))?/.exec(body);
        return {
            css: cssPath,
            family: /font-family\s*:\s*["']([^"']+)["']/.exec(body)?.[1],
            urls: [...body.matchAll(/url\(\s*["']?([^"')]+?)["']?\s*\)/g)].map((u) => u[1]),
            weight: w ? [Number(w[1]), Number(w[2] ?? w[1])] : null,
        };
    });
}

// ---- 扫描 web/ 的非 ASCII 字符（与 scripts/fetch_fonts.py 同一套算法）---------------------
const REGEX_PREV = new Set([..."(,=:[!&|?{};+-*%~^<>\n"]);
const REGEX_WORDS = new Set([
    "return",
    "typeof",
    "instanceof",
    "in",
    "of",
    "new",
    "delete",
    "void",
    "case",
    "do",
    "else",
    "yield",
    "await",
]);

/** 去掉 JS 注释，保留字符串、模板串和正则字面量。 */
export function stripJsComments(src) {
    let out = "";
    let i = 0;
    let prev = "\n";
    let word = "";
    const n = src.length;
    while (i < n) {
        const c = src[i];
        const d = i + 1 < n ? src[i + 1] : "";
        if (c === "/" && d === "/") {
            const e = src.indexOf("\n", i);
            i = e < 0 ? n : e;
        } else if (c === "/" && d === "*") {
            const e = src.indexOf("*/", i + 2);
            i = e < 0 ? n : e + 2;
        } else if (c === '"' || c === "'" || c === "`") {
            let k = i + 1;
            while (k < n && src[k] !== c && !(c !== "`" && src[k] === "\n")) k += src[k] === "\\" ? 2 : 1;
            out += src.slice(i, k + 1);
            i = k + 1;
            prev = c;
            word = "";
        } else if (c === "/" && (REGEX_PREV.has(prev) || REGEX_WORDS.has(word))) {
            let k = i + 1;
            let inClass = false;
            while (k < n && src[k] !== "\n") {
                if (src[k] === "\\") {
                    k += 2;
                    continue;
                }
                if (src[k] === "[") inClass = true;
                else if (src[k] === "]") inClass = false;
                else if (src[k] === "/" && !inClass) break;
                k++;
            }
            out += src.slice(i, k + 1);
            i = k + 1;
            prev = "/";
            word = "";
        } else {
            out += c;
            if (c === "\n") {
                prev = "\n";
                word = "";
            } else if (!/\s/.test(c)) {
                prev = c;
                word = /[\p{L}\p{N}_]/u.test(c) ? word + c : "";
            }
            i++;
        }
    }
    return out;
}

const unescapeJs = (s) =>
    s
        .replace(/\\u\{([0-9a-fA-F]+)\}/g, (_, h) => String.fromCodePoint(parseInt(h, 16)))
        .replace(/\\u([0-9a-fA-F]{4})/g, (_, h) => String.fromCharCode(parseInt(h, 16)))
        .replace(/\\x([0-9a-fA-F]{2})/g, (_, h) => String.fromCharCode(parseInt(h, 16)));

const stripCssComments = (s) => s.replace(/\/\*[\s\S]*?\*\//g, " ");
const stripHtml = (s) =>
    s
        .replace(/<!--[\s\S]*?-->/g, " ")
        .replace(/(<script\b[^>]*>)([\s\S]*?)(<\/script>)/gi, (_, a, b, c) => a + stripJsComments(b) + c)
        .replace(/(<style\b[^>]*>)([\s\S]*?)(<\/style>)/gi, (_, a, b, c) => a + stripCssComments(b) + c);
const UNUSABLE = /[\p{Cc}\p{Cs}\p{Co}\p{Cn}]/u;

/** 一个文件去掉注释后的非 ASCII 字符集合。非 js、mjs、html、css 的文件返回空集。 */
export function scanChars(file, text) {
    const body = /\.m?js$/.test(file)
        ? stripJsComments(text)
        : /\.html$/.test(file)
          ? stripHtml(text)
          : /\.css$/.test(file)
            ? stripCssComments(text)
            : "";
    const out = new Set();
    for (const ch of unescapeJs(body)) if (ch.codePointAt(0) > 0x7e && !UNUSABLE.test(ch)) out.add(ch);
    return out;
}

/** web/ 下全部文件的相对路径（正斜杠）。字符扫描跳过逐字收录的 JUCE 前端脚本（web/js/juce/）和字体目录，其余文件都扫；字体文件另查。 */
function walk(root, dir = "web") {
    const out = [];
    for (const e of fs.readdirSync(path.join(root, dir), { withFileTypes: true })) {
        const rel = `${dir}/${e.name}`;
        if (e.isDirectory()) out.push(...walk(root, rel));
        else out.push(rel);
    }
    return out;
}

// ---- 入口 ------------------------------------------------------------------
export function checkTree(root) {
    const errors = [];
    const files = fs.existsSync(path.join(root, "web")) ? walk(root) : [];
    errors.push(...strayProblems(files));

    const reuse = reuseCopyrights(fs.readFileSync(path.join(root, "REUSE.toml"), "utf8"));
    const infos = {};
    const sizes = {};
    for (const spec of FONTS) {
        const abs = path.join(root, "web", "fonts", spec.file);
        if (!fs.existsSync(abs)) {
            errors.push(`${spec.file}：文件不存在（生成方法见 web/fonts/README.md）`);
            continue;
        }
        const buf = fs.readFileSync(abs);
        sizes[spec.file] = buf.length;
        try {
            infos[spec.file] = readFont(buf);
        } catch (e) {
            errors.push(`${spec.file}：读不出来：${e.message}`);
            continue;
        }
        errors.push(...nameProblems(spec, infos[spec.file], reuse[`web/fonts/${spec.file}`]));
    }
    errors.push(...sizeProblems(sizes));

    const faces = files
        .filter((f) => f.endsWith(".css"))
        .flatMap((f) => parseFontFaces(fs.readFileSync(path.join(root, f), "utf8"), f));
    errors.push(...cssProblems(faces, infos));

    if (Object.keys(infos).length === FONTS.length) {
        const have = new Set(Object.values(infos).flatMap((i) => [...i.cmap]));
        const required = new Set();
        for (const f of files) {
            if (f.startsWith("web/js/juce/") || f.startsWith("web/fonts/")) continue;
            for (const ch of scanChars(f, fs.readFileSync(path.join(root, f), "utf8"))) required.add(ch);
        }
        errors.push(...coverageProblems(required, have));
    }
    return errors;
}

export function selfTest(ctx) {
    const root = ctx?.root;
    const t = (label, bad, problems) => ({ label, bad, passed: bad ? problems.length > 0 : problems.length === 0 });

    // name 表：以仓内真实字体为底，拷贝后改坏一处
    const real = readFont(fs.readFileSync(path.join(root, "web", "fonts", "MiciriSans.woff2")));
    const sans = FONTS[1];
    const noto = FONTS[3];
    const mutate = (info, id, value, add = false) => ({
        ...info,
        names: add ? [...info.names, { id, value }] : info.names.map((r) => (r.id === id ? { ...r, value } : r)),
    });
    const drop = (info, id) => ({ ...info, names: info.names.filter((r) => r.id !== id) });
    const line = stripCopyright(values(real, 0)[0]);
    const ok = (info = real) => nameProblems(sans, info, line);

    // 坏的 woff2 必须报错而不是静默当成空
    const throws = (buf) => {
        try {
            readFont(buf);
            return [];
        } catch (e) {
            return [e.message];
        }
    };
    const whole = fs.readFileSync(path.join(root, "web", "fonts", "SpaceGrotesk.woff2"));

    const face = (over = {}) => ({
        css: "web/shared/tokens.css",
        family: "Miciri Sans",
        urls: ["../fonts/MiciriSans.woff2"],
        weight: [300, 600],
        ...over,
    });
    const allFaces = () => [
        face({ family: "Space Grotesk", urls: ["../fonts/SpaceGrotesk.woff2"], weight: [400, 700] }),
        face(),
        face({ family: "Miciri Mono", urls: ["../fonts/MiciriMono.woff2"], weight: [400, 500] }),
        face({ family: "Noto Sans SC", urls: ["../fonts/NotoSansSC.woff2"], weight: [300, 700] }),
    ];
    const infos = {
        "SpaceGrotesk.woff2": { weight: [600, 600] },
        "MiciriSans.woff2": { weight: [400, 400] },
        "MiciriMono.woff2": { weight: [400, 400] },
        "NotoSansSC.woff2": { weight: [300, 700] },
    };
    const swap = (i, over) => allFaces().map((f, k) => (k === i ? { ...f, ...over } : f));
    const scan = (file, text) => [...scanChars(file, text)].join("");
    const toml = (c) =>
        `version = 1\n\n[[annotations]]\npath = ["web/fonts/MiciriSans.woff2", "x"]\nSPDX-FileCopyrightText = "${c}"\n`;

    const cases = [
        ["仓内的 Miciri Sans 干净", false, ok()],
        [
            "解析：真实 woff2 读出码位与名字",
            false,
            real.cmap.has(0x41) && real.names.length > 5 ? [] : ["读出来的表不对"],
        ],
        [
            "解析：不是 woff2",
            true,
            throws(Buffer.from("not a font at all, just text padding..........................")),
        ],
        ["解析：截断的 woff2", true, throws(whole.subarray(0, 120))],
        ["族名被改回上游名", true, ok(mutate(real, 1, "IBM Plex Sans"))],
        ["PostScript 名含保留字体名", true, ok(mutate(real, 6, "IBMPlexSans-Regular"))],
        ["全名 nameID 4 含保留字体名", true, ok(mutate(real, 4, "Plex Sans Regular"))],
        ["版本串 nameID 5 残留保留字体名", true, ok(mutate(real, 5, "Plex Version 3.201"))],
        ["多出一条含保留字体名的排版族名", true, ok(mutate(real, 16, "IBM Plex Sans", true))],
        ["商标声明 nameID 7 里的 Plex 不算", false, ok(mutate(real, 7, "IBM Plex(r) is a trademark of IBM Corp."))],
        ["缺 nameID 13 许可证声明", true, ok(drop(real, 13))],
        ["缺 nameID 0 版权", true, ok(drop(real, 0))],
        ["版权行与 REUSE 登记不一致", true, nameProblems(sans, real, "2001 Nobody")],
        ["REUSE.toml 里没有这个字体", true, nameProblems(sans, real, undefined)],
        ["Noto 的族名含保留字体名 Source", true, nameProblems(noto, mutate(drop(real, 16), 1, "Source Sans"), line)],
        [
            "版权行去前缀：(c) 与 Copyright",
            false,
            [stripCopyright("(c) 2014 Adobe"), stripCopyright("Copyright 2014 Adobe")].filter(
                (s) => s !== "2014 Adobe",
            ),
        ],
        [
            "REUSE 解析：数组路径与转义引号",
            false,
            reuseCopyrights(toml('2019 IBM \\"x\\"'))["web/fonts/MiciriSans.woff2"] === '2019 IBM "x"' &&
            reuseCopyrights(toml("a"))["x"] === "a"
                ? []
                : ["解析结果不对"],
        ],
        ["字符：全部有字形", false, coverageProblems(new Set(["中", "A"]), new Set([0x4e2d, 0x41]))],
        ["字符：一个字四个字体都没有", true, coverageProblems(new Set(["中", "ꙮ"]), new Set([0x4e2d]))],
        ["扫描：JS 行注释与块注释里的字不算", false, scan("a.js", "// 注释☃\n/* 块☃ */ const a = 1;")],
        ["扫描：字符串里的字要算", true, scan("a.js", 'const a = "☃";').split("")],
        ["扫描：字符串里的 // 不当注释", true, scan("a.js", 'const u = "http://☃";').split("")],
        ["扫描：转义形式的字符要算", true, scan("a.js", "const a = '\\u2603';").split("")],
        ["扫描：正则字面量里的 // 之后的字要算", true, scan("a.js", "const r = /[//☃]/;").split("")],
        [
            "扫描：HTML 注释与 CSS 注释里的字不算",
            false,
            scan("a.html", "<!-- ☃ --><style>/* ☃ */</style><script>// ☃\n</script>"),
        ],
        ["扫描：HTML 正文里的字要算", true, scan("a.html", "<p>☃</p>").split("")],
        ["CSS：四个都对", false, cssProblems(allFaces(), infos)],
        ["CSS：族名与登记名不符", true, cssProblems(swap(1, { family: "Rinne Sans" }), infos)],
        ["CSS：指向未登记的文件", true, cssProblems(swap(1, { urls: ["../fonts/Other.woff2"] }), infos)],
        ["CSS：目录不对", true, cssProblems(swap(1, { urls: ["fonts/MiciriSans.woff2"] }), infos)],
        ["CSS：字重区间没盖住内嵌字重", true, cssProblems(swap(0, { weight: [700, 900] }), infos)],
        ["CSS：Noto 的区间比内嵌窄", true, cssProblems(swap(3, { weight: [400, 700] }), infos)],
        ["CSS：缺 font-weight", true, cssProblems(swap(2, { weight: null }), infos)],
        ["CSS：有一个字体没人引用", true, cssProblems(allFaces().slice(0, 3), infos)],
        [
            "CSS：解析 @font-face 并忽略注释里的声明",
            false,
            (() => {
                const f = parseFontFaces(
                    '/* @font-face { font-family: "X"; } */\n@font-face { font-family: "Miciri Mono"; src: url("../fonts/MiciriMono.woff2") format("woff2"); font-weight: 400 500; }',
                    "web/shared/tokens.css",
                );
                return f.length === 1 &&
                    f[0].family === "Miciri Mono" &&
                    f[0].weight.join() === "400,500" &&
                    f[0].urls[0] === "../fonts/MiciriMono.woff2"
                    ? []
                    : ["解析结果不对"];
            })(),
        ],
        ["大小：在上限内", false, sizeProblems({ "a.woff2": MAX_FILE, "b.woff2": MAX_TOTAL - MAX_FILE })],
        ["大小：单个超限", true, sizeProblems({ "a.woff2": MAX_FILE + 1 })],
        ["大小：合计超限", true, sizeProblems({ "a.woff2": MAX_FILE, "b.woff2": MAX_FILE })],
        ["多出一个未登记的字体文件", true, strayProblems(["web/fonts/MiciriSans.woff2", "web/fonts/Extra.woff2"])],
        [
            "只有登记的字体与别的文件",
            false,
            strayProblems(["web/fonts/MiciriSans.woff2", "web/fonts/README.md", "web/shared/base.css"]),
        ],
    ].map(([label, bad, problems]) => t(label, bad, problems));
    return caseResult(cases);
}

export default {
    name: "fonts",
    group: "compliance",
    inputs: [],
    run(ctx) {
        const errors = checkTree(ctx.root);
        return { ok: errors.length === 0, lines: errors };
    },
    selfTest,
};
