// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from synchain-oss/rinne; see upstream/ci.json
//
// 工具钉版文件：每个文件恰好一行版本号；同一工具在多处出现时版本必须一致
// （.clang-format 头注释、requirements-tools.txt 与 .clang-format-version 对拍）。
// 构建用的钉版文件（JUCE、pluginval）与工具钉版一样是必需的，缺一个就判红。mac 的 pluginval 包 sha256 也在这里：
// 它登记的是 .pluginval-version 所指那个 tag 的包，两者对不上就判红。
// 新增钉版依赖（例如 Catch2、pocketfft）的那一张卡，同一个 PR 在这里加它的判据。

import fs from "node:fs";
import path from "node:path";
import { caseResult } from "../lib/check-kit.mjs";

const SEMVER = /^\d+\.\d+\.\d+$/;
const CATCH2_FILE = "cmake/MiciriDeps.cmake";
const COMMIT = /^[0-9a-f]{40}$/;

export const PINS = {
    ".gitleaks-version": SEMVER,
    ".clang-format-version": SEMVER,
    ".prettier-version": SEMVER,
    ".actionlint-version": SEMVER,
    ".node-version": SEMVER,
    ".juce-commit": /^[0-9a-f]{40}$/,
    ".juce-version": SEMVER,
    ".pluginval-version": /^v?\d+\.\d+\.\d+$/,
    "scripts/ci/mac/pluginval-macos.sha256": /^v\d+\.\d+\.\d+ [0-9a-f]{64}$/,
};

/** requirements-tools.txt 的条目：续行接回一行、去掉注释行，{ name（pip 的规范名）, version, hashes }。 */
export function requirementEntries(text) {
    return text
        .replace(/\\\r?\n/g, " ")
        .split(/\r?\n/)
        .filter((l) => l.trim() && !l.trim().startsWith("#"))
        .map((l) => {
            const m = /^\s*([A-Za-z0-9._-]+)(?:\[[^\]]*\])?\s*(?:==\s*(\S+))?/.exec(l);
            const hashes = [...l.matchAll(/--hash=sha256:(\S+)/g)].map((h) => h[1]);
            return { name: (m?.[1] || "").toLowerCase().replace(/[-_.]+/g, "-"), version: m?.[2] || "", hashes };
        });
}

/**
 * requirements-tools.txt 的写法：pip 的 --require-hashes 要求每个条目都带哈希，续行符必须是行尾最后一个字符。
 * 只认两种行：`名字==版本 \` 与 `    --hash=sha256:<64 位十六进制>`（后面还有哈希时行尾带 `\`），其余只许空行和注释。
 * 只数哈希个数、不看行结构的话，字面量 `\n`、漏掉的续行符都会混过去，到 CI 上 pip 才报错。
 */
export function requirementShapeErrors(text) {
    const errors = [];
    let open = false;
    text.split(/\r?\n/).forEach((raw, i) => {
        const at = `第 ${i + 1} 行`;
        if (!raw.trim() || raw.trim().startsWith("#")) {
            if (open) errors.push(`${at}：续行符后面不能是空行或注释`);
            open = false;
            return;
        }
        const cont = raw.endsWith("\\");
        const body = raw.replace(/ ?\\$/, "");
        const isReq = /^[A-Za-z0-9._-]+(?:\[[^\]]*\])?==\S+$/.test(body);
        const isHash = /^ +--hash=sha256:[0-9a-f]{64}$/.test(body);
        if (!isReq && !isHash) errors.push(`${at}：不是「名字==版本 \\」或「    --hash=sha256:<64 位十六进制>」的写法`);
        else if (isReq && open) errors.push(`${at}：上一行的续行符后面不该是新条目`);
        else if (isReq && !cont) errors.push(`${at}：条目后面要带哈希，行尾缺续行符`);
        else if (isHash && !open) errors.push(`${at}：哈希行必须接在条目或另一个哈希之后`);
        open = cont;
    });
    if (open) errors.push("文件以续行符结尾");
    return errors;
}

/** read(rel) 返回文件文本或 null。 */
export function checkPins(read) {
    const errors = [];
    const value = {};
    for (const [file, re] of Object.entries(PINS)) {
        const text = read(file);
        if (text === null) {
            errors.push(`缺 ${file}`);
            continue;
        }
        value[file] = text.trim();
        if (!/^[^\r\n]+\n?$/.test(text) || !re.test(value[file])) errors.push(`${file} 必须恰好一行：${re.source}`);
    }
    const cf = value[".clang-format-version"];
    if (!new RegExp(`^# clang-format ${cf?.replace(/\./g, "\\.")}\\b`, "m").test(read(".clang-format") || "")) {
        errors.push(".clang-format 头注释里的版本与 .clang-format-version 不一致");
    }
    // mac 的 pluginval 包按 .pluginval-version 的 tag 下载，sha256 登记里写的 tag 必须原样就是它
    const macPluginval = /^(\S+) [0-9a-f]{64}$/.exec(value["scripts/ci/mac/pluginval-macos.sha256"] || "");
    if (macPluginval && macPluginval[1] !== value[".pluginval-version"]) {
        errors.push(
            `scripts/ci/mac/pluginval-macos.sha256 登记的是 ${macPluginval[1]}，与 .pluginval-version 的 ${value[".pluginval-version"] || "（没有）"} 不一致`,
        );
    }
    for (const e of requirementShapeErrors(read("requirements-tools.txt") || ""))
        errors.push(`requirements-tools.txt ${e}`);
    const req = requirementEntries(read("requirements-tools.txt") || "");
    const entry = (name) => req.find((e) => e.name === name);
    if (entry("clang-format")?.version !== cf) errors.push("requirements-tools.txt 的 clang-format 版本不一致");
    if (!SEMVER.test(entry("reuse")?.version || "")) errors.push("requirements-tools.txt 须钉住 reuse 的版本");
    // Catch2 是钉版依赖：tag 可以被移动、提交不会，所以 GIT_TAG 必须是 40 位提交号（理由写在 cmake/MiciriDeps.cmake 的头注释里）
    const deps = read(CATCH2_FILE);
    const tag = deps === null ? null : /FetchContent_Declare\(\s*Catch2\b[^)]*?GIT_TAG\s+([^\s)]+)/.exec(deps)?.[1];
    if (deps === null) errors.push(`缺 ${CATCH2_FILE}`);
    else if (!tag || !COMMIT.test(tag)) errors.push(`${CATCH2_FILE} 的 Catch2 GIT_TAG 必须是 40 位小写提交号，不能是 tag 或分支名`);
    return errors;
}

const H = "a1".repeat(32);
const CATCH2_SHA = "b5373dadca40b7edc8570cf9470b9b1cb1934d40";
/** cmake/MiciriDeps.cmake 的 FetchContent_Declare(Catch2 …) 写法；tagLine 为空时就是没有 GIT_TAG。 */
const catch2Source = (tagLine) =>
    `FetchContent_Declare(Catch2\n  GIT_REPOSITORY https://github.com/catchorg/Catch2.git\n  ${tagLine})\n`;
const GOOD = {
    [CATCH2_FILE]: catch2Source(`GIT_TAG ${CATCH2_SHA}`),
    ".gitleaks-version": "8.30.1\n",
    ".clang-format-version": "18.1.8\n",
    ".prettier-version": "3.9.6\n",
    ".actionlint-version": "1.7.12\n",
    ".node-version": "22.23.3\n",
    ".juce-commit": `${"d6181bde".repeat(5)}\n`,
    ".juce-version": "8.0.8\n",
    ".pluginval-version": "v1.0.4\n",
    "scripts/ci/mac/pluginval-macos.sha256": `v1.0.4 ${"3c".repeat(32)}\n`,
    ".clang-format": "# SPDX\n# clang-format 18.1.8：说明\nBasedOnStyle: LLVM\n",
    "requirements-tools.txt": `clang-format==18.1.8 \\\n    --hash=sha256:${H}\nreuse==6.2.0 \\\n    --hash=sha256:${H}\n`,
};

export default {
    name: "pins",
    group: "docs",
    inputs: [], // 钉版文件缺失在 checkPins 里直接判红，不能变成 SKIP
    run(ctx) {
        const read = (rel) => {
            const abs = path.join(ctx.root, rel);
            return fs.existsSync(abs) ? fs.readFileSync(abs, "utf8") : null;
        };
        const errors = checkPins(read);
        return { ok: errors.length === 0, lines: errors };
    },
    selfTest() {
        const with_ = (patch) => checkPins((f) => (f in patch ? patch[f] : (GOOD[f] ?? null)));
        const hashed = (entry) => `${entry} \\\n    --hash=sha256:${H}\n`;
        const req = (cf, reuse) => ({ "requirements-tools.txt": `${hashed(`clang-format==${cf}`)}${hashed(reuse)}` });
        const raw = (text) => ({ "requirements-tools.txt": text });
        const cases = [
            ["齐全且一致", false, with_({})],
            ["版本号带 v 前缀", true, with_({ ".gitleaks-version": "v8.30.1\n" })],
            ["多出一行", true, with_({ ".node-version": "22.23.3\n22.0.0\n" })],
            ["缺钉版文件", true, with_({ ".actionlint-version": null })],
            ["clang-format 两处版本不一致", true, with_(req("18.1.7", "reuse==6.2.0"))],
            ["reuse 未钉版", true, with_(req("18.1.8", "reuse"))],
            [
                "带多个哈希的写法",
                false,
                with_(
                    raw(
                        `clang-format==18.1.8 \\\n    --hash=sha256:${H} \\\n    --hash=sha256:${H}\n${hashed("reuse[charset-normalizer]==6.2.0")}`,
                    ),
                ),
            ],
            [
                "条目间有注释与空行",
                false,
                with_(raw(`# 说明\n${hashed("clang-format==18.1.8")}\n# 依赖\n${hashed("reuse==6.2.0")}`)),
            ],
            [
                "续行符写成字面量 \\n",
                true,
                with_(raw(`clang-format==18.1.8 \\n    --hash=sha256:${H}\n${hashed("reuse==6.2.0")}`)),
            ],
            ["条目没有哈希", true, with_(raw(`clang-format==18.1.8\n${hashed("reuse==6.2.0")}`))],
            [
                "哈希行漏了续行符",
                true,
                with_(
                    raw(
                        `clang-format==18.1.8 \\\n    --hash=sha256:${H}\n    --hash=sha256:${H}\n${hashed("reuse==6.2.0")}`,
                    ),
                ),
            ],
            [
                "哈希不是 64 位",
                true,
                with_(raw(`clang-format==18.1.8 \\\n    --hash=sha256:aa\n${hashed("reuse==6.2.0")}`)),
            ],
            [
                "续行符后面是空行",
                true,
                with_(raw(`clang-format==18.1.8 \\\n\n    --hash=sha256:${H}\n${hashed("reuse==6.2.0")}`)),
            ],
            ["文件以续行符结尾", true, with_(raw(`${hashed("clang-format==18.1.8")}reuse==6.2.0 \\\n`))],
            ["JUCE 提交号不是 40 位", true, with_({ ".juce-commit": "d6181bde\n" })],
            ["JUCE 提交号是大写", true, with_({ ".juce-commit": `${"D6181BDE".repeat(5)}\n` })],
            ["JUCE 版本不是 x.y.z", true, with_({ ".juce-version": "8.0\n" })],
            ["pluginval 版本不是 x.y.z", true, with_({ ".pluginval-version": "latest\n" })],
            [
                "mac pluginval sha256 的 tag 与 .pluginval-version 不一致",
                true,
                with_({ "scripts/ci/mac/pluginval-macos.sha256": `v1.0.5 ${"3c".repeat(32)}\n` }),
            ],
            // 钉版不带 v、登记带 v：下载脚本原样比较，会当场失败，这里必须先判红
            ["mac pluginval 钉版不带 v，与登记的 tag 不符", true, with_({ ".pluginval-version": "1.0.4\n" })],
            [
                "mac pluginval sha256 少一位",
                true,
                with_({ "scripts/ci/mac/pluginval-macos.sha256": `v1.0.4 ${"3c".repeat(31)}\n` }),
            ],
            ["Catch2 GIT_TAG 写的是 tag 名", true, with_({ [CATCH2_FILE]: catch2Source("GIT_TAG v3.5.4") })],
            [
                "Catch2 GIT_TAG 是 7 位短哈希",
                true,
                with_({ [CATCH2_FILE]: catch2Source(`GIT_TAG ${CATCH2_SHA.slice(0, 7)}`) }),
            ],
            [
                "Catch2 GIT_TAG 是大写提交号",
                true,
                with_({ [CATCH2_FILE]: catch2Source(`GIT_TAG ${CATCH2_SHA.toUpperCase()}`) }),
            ],
            ["Catch2 没有 GIT_TAG", true, with_({ [CATCH2_FILE]: catch2Source("") })],
            ["缺 cmake/MiciriDeps.cmake", true, with_({ [CATCH2_FILE]: null })],
            ["Catch2 GIT_TAG 是 40 位提交号", false, with_({ [CATCH2_FILE]: catch2Source(`GIT_TAG ${CATCH2_SHA}`) })],
            ...[".juce-commit", ".juce-version", ".pluginval-version", "scripts/ci/mac/pluginval-macos.sha256"].map(
                (f) => [`缺构建钉版 ${f}`, true, with_({ [f]: null })],
            ),
        ].map(([label, bad, errors]) => ({ label, bad, passed: bad ? errors.length > 0 : errors.length === 0 }));
        return caseResult(cases);
    },
};
