// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from synchain-oss/rinne; see upstream/ci.json
//
// 工具钉版文件：每个文件恰好一行版本号；同一工具在多处出现时版本必须一致
// （.clang-format 头注释、requirements-tools.txt 与 .clang-format-version 对拍）。
// 构建用的钉版文件（JUCE、pluginval）与工具钉版一样是必需的，缺一个就判红。mac 的 pluginval 包 sha256 也在这里：
// 它登记的是 .pluginval-version 所指那个 tag 的包，两者对不上就判红。
// 新增钉版依赖（例如 Catch2、pocketfft）的那一张卡，同一个 PR 在这里加它的判据。
// axe 无障碍冒烟的 npm 工具链（playwright、axe-core）钉在 web-preview/tests/axe/ 的 package.json 与 package-lock.json：
// 版本必须是确切版本、锁文件里每个包都带 sha512 完整性哈希并来自 npm 官方源，build.yml 里只许 `npm ci --ignore-scripts`。

import fs from "node:fs";
import path from "node:path";
import { caseResult } from "../lib/check-kit.mjs";

const SEMVER = /^\d+\.\d+\.\d+$/;
const CATCH2_FILE = "cmake/MiciriDeps.cmake";
const COMMIT = /^[0-9a-f]{40}$/;
const AXE_DIR = "web-preview/tests/axe";
const AXE_PACKAGES = ["axe-core", "playwright"];
const BUILD_WORKFLOW = ".github/workflows/build.yml";
const INTEGRITY = /^sha512-[A-Za-z0-9+/]+={0,2}$/;
const NPM_REGISTRY = "https://registry.npmjs.org/";

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

/** 解析 JSON：失败返回 null。 */
function parseJson(text) {
    try {
        return JSON.parse(text);
    } catch {
        return null;
    }
}

/**
 * axe 工具链的钉版：package.json 里全是确切版本（不许 ^ ~ 范围、tag、git 地址），两个必需的包都在；
 * package-lock.json 是 lockfileVersion 3，根的依赖与 package.json 一致，每个包都带 sha512 完整性哈希、
 * 来自 npm 官方源，直接依赖的锁定版本等于钉的版本；build.yml 里的 npm 命令只许 `npm ci` 且带 --ignore-scripts，
 * 并且确实装了（删掉安装步骤会让 axe 退回跳过）、web-smoke 要求 axe 必须运行（MICIRI_REQUIRE_AXE）。
 */
export function checkAxePins(read) {
    const errors = [];
    const pkgText = read(`${AXE_DIR}/package.json`);
    const lockText = read(`${AXE_DIR}/package-lock.json`);
    if (pkgText === null) errors.push(`缺 ${AXE_DIR}/package.json`);
    if (lockText === null) errors.push(`缺 ${AXE_DIR}/package-lock.json`);
    const pkg = pkgText === null ? null : parseJson(pkgText);
    const lock = lockText === null ? null : parseJson(lockText);
    if (pkgText !== null && !pkg) errors.push(`${AXE_DIR}/package.json 不是合法 JSON`);
    if (lockText !== null && !lock) errors.push(`${AXE_DIR}/package-lock.json 不是合法 JSON`);
    if (pkg) {
        for (const kind of ["dependencies", "devDependencies", "optionalDependencies"])
            for (const [name, ver] of Object.entries(pkg[kind] || {}))
                if (!SEMVER.test(ver))
                    errors.push(`${AXE_DIR}/package.json 的 ${name} 必须钉确切版本 x.y.z，现在是 ${ver}`);
        for (const name of AXE_PACKAGES)
            if (!pkg.dependencies?.[name]) errors.push(`${AXE_DIR}/package.json 的 dependencies 里缺 ${name}`);
    }
    if (lock) {
        const where = `${AXE_DIR}/package-lock.json`;
        if (lock.lockfileVersion !== 3) errors.push(`${where} 必须是 lockfileVersion 3`);
        const packages = lock.packages || {};
        if (pkg && JSON.stringify(packages[""]?.dependencies ?? {}) !== JSON.stringify(pkg.dependencies ?? {}))
            errors.push(`${where} 根的依赖与 package.json 不一致（改了 package.json 要重新生成锁文件）`);
        for (const [key, p] of Object.entries(packages)) {
            if (key === "") continue;
            if (!INTEGRITY.test(p.integrity || "")) errors.push(`${where} 的 ${key} 没有 sha512 完整性哈希`);
            if (!String(p.resolved || "").startsWith(NPM_REGISTRY))
                errors.push(`${where} 的 ${key} 不是来自 ${NPM_REGISTRY}`);
        }
        for (const [name, ver] of Object.entries(pkg?.dependencies ?? {}))
            if (packages[`node_modules/${name}`]?.version !== ver)
                errors.push(`${where} 里 ${name} 的锁定版本与 package.json 钉的 ${ver} 不一致`);
    }
    const wf = read(BUILD_WORKFLOW);
    if (wf !== null) {
        const code = wf.replace(/^\s*#.*$/gm, "").replace(/\s#.*$/gm, "");
        const npmLines = code.split(/\r?\n/).filter((l) => /\bnpm\s+\S/.test(l));
        const isCi = (l) => /\bnpm\s+ci\b/.test(l);
        for (const l of npmLines) {
            // 同一行里每一处 npm 调用都要核对：不能在 npm ci 后面用 && 夹带别的 npm 命令
            const others = [...l.matchAll(/\bnpm\s+(\S+)/g)].filter((m) => m[1] !== "ci");
            if (others.length > 0) errors.push(`${BUILD_WORKFLOW} 里只许 npm ci（要按锁文件装）：${l.trim()}`);
            else if (!/--ignore-scripts\b/.test(l))
                errors.push(`${BUILD_WORKFLOW} 的 npm ci 必须带 --ignore-scripts：${l.trim()}`);
        }
        if (!npmLines.some(isCi)) errors.push(`${BUILD_WORKFLOW} 里没有 npm ci：axe 工具链没有安装步骤`);
        if (!/^\s*MICIRI_REQUIRE_AXE:\s*['"]?1['"]?\s*$/m.test(code))
            errors.push(`${BUILD_WORKFLOW} 必须设 MICIRI_REQUIRE_AXE: '1'，否则缺包时 axe 会悄悄跳过`);
    } else errors.push(`缺 ${BUILD_WORKFLOW}`);
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
    // 先去掉 CMake 的 # 注释：注释里写的 GIT_TAG 不算数
    const code = deps?.replace(/#.*$/gm, "") ?? null;
    const tag = code === null ? null : /FetchContent_Declare\(\s*Catch2\b[^)]*?GIT_TAG\s+([^\s)]+)/.exec(code)?.[1];
    if (deps === null) errors.push(`缺 ${CATCH2_FILE}`);
    else if (!tag || !COMMIT.test(tag))
        errors.push(`${CATCH2_FILE} 的 Catch2 GIT_TAG 必须是 40 位小写提交号，不能是 tag 或分支名`);
    errors.push(...checkAxePins(read));
    return errors;
}

const H = "a1".repeat(32);
const CATCH2_SHA = "b5373dadca40b7edc8570cf9470b9b1cb1934d40";
/** cmake/MiciriDeps.cmake 的 FetchContent_Declare(Catch2 …) 写法；tagLine 为空时就是没有 GIT_TAG。 */
const catch2Source = (tagLine) =>
    `FetchContent_Declare(Catch2\n  GIT_REPOSITORY https://github.com/catchorg/Catch2.git\n  ${tagLine})\n`;
/** axe 钉版夹具：packages 里每个包的写法可以按需改坏。 */
const AXE_INTEGRITY = `sha512-${"Ab1+/".repeat(17)}==`;
const axePackage = (over = {}) => ({
    version: "1.2.3",
    resolved: `${NPM_REGISTRY}x/-/x-1.2.3.tgz`,
    integrity: AXE_INTEGRITY,
    ...over,
});
const axePkgJson = (deps = { "axe-core": "4.14.0", playwright: "1.64.1" }) =>
    JSON.stringify({ name: "x", private: true, dependencies: deps });
const axeLockJson = ({ deps = { "axe-core": "4.14.0", playwright: "1.64.1" }, version = 3, pkgs = {} } = {}) =>
    JSON.stringify({
        name: "x",
        lockfileVersion: version,
        packages: {
            "": { name: "x", dependencies: deps },
            "node_modules/axe-core": axePackage({ version: "4.14.0" }),
            "node_modules/playwright": axePackage({ version: "1.64.1" }),
            ...pkgs,
        },
    });
const BUILD_GOOD = [
    "jobs:",
    "  web-smoke:",
    "    steps:",
    "      # npm install 只出现在注释里，不算数",
    "      - name: Install",
    '        run: (cd "$D" && npm ci --ignore-scripts --no-audit --no-fund)',
    "      - name: Web smoke",
    "        env:",
    "          MICIRI_REQUIRE_AXE: '1'",
    "        run: node web-preview/tests/run-all.mjs",
    "",
].join("\n");
const GOOD = {
    [`${AXE_DIR}/package.json`]: axePkgJson(),
    [`${AXE_DIR}/package-lock.json`]: axeLockJson(),
    [BUILD_WORKFLOW]: BUILD_GOOD,
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

const AXE_PKG = `${AXE_DIR}/package.json`;
const AXE_LOCK = `${AXE_DIR}/package-lock.json`;
const buildWith = (run) => BUILD_GOOD.replace("npm ci --ignore-scripts --no-audit --no-fund", run);
/** axe 钉版的自测用例：[标签, 是否应判红, 错误表]。 */
function axeCases(with_) {
    const range = { "axe-core": "^4.14.0", playwright: "1.64.1" };
    const only = { playwright: "1.64.1" };
    return [
        ["axe 钉版齐全", false, with_({})],
        [
            "axe 依赖写成 ^ 范围",
            true,
            with_({ [AXE_PKG]: axePkgJson(range), [AXE_LOCK]: axeLockJson({ deps: range }) }),
        ],
        ["axe 依赖写成 latest", true, with_({ [AXE_PKG]: axePkgJson({ ...only, "axe-core": "latest" }) })],
        ["缺 axe-core", true, with_({ [AXE_PKG]: axePkgJson(only), [AXE_LOCK]: axeLockJson({ deps: only }) })],
        ["缺 package.json", true, with_({ [AXE_PKG]: null })],
        ["缺 package-lock.json", true, with_({ [AXE_LOCK]: null })],
        ["package.json 不是合法 JSON", true, with_({ [AXE_PKG]: "{" })],
        ["锁文件是 lockfileVersion 2", true, with_({ [AXE_LOCK]: axeLockJson({ version: 2 }) })],
        [
            "锁文件里某个包没有完整性哈希",
            true,
            with_({ [AXE_LOCK]: axeLockJson({ pkgs: { "node_modules/y": axePackage({ integrity: undefined }) } }) }),
        ],
        [
            "锁文件里的哈希是 sha1",
            true,
            with_({
                [AXE_LOCK]: axeLockJson({
                    pkgs: { "node_modules/y": axePackage({ integrity: `sha1-${"a".repeat(27)}=` }) },
                }),
            }),
        ],
        [
            "锁文件里的包来自别的源",
            true,
            with_({
                [AXE_LOCK]: axeLockJson({
                    pkgs: { "node_modules/y": axePackage({ resolved: "https://example.invalid/y.tgz" }) },
                }),
            }),
        ],
        [
            "锁文件里的包是 git 地址",
            true,
            with_({
                [AXE_LOCK]: axeLockJson({
                    pkgs: { "node_modules/y": axePackage({ resolved: "git+https://github.com/o/y.git#abc" }) },
                }),
            }),
        ],
        [
            "锁文件根的依赖与 package.json 不一致",
            true,
            with_({ [AXE_LOCK]: axeLockJson({ deps: { "axe-core": "4.13.0", playwright: "1.64.1" } }) }),
        ],
        [
            "锁定版本与钉的版本不一致",
            true,
            with_({
                [AXE_LOCK]: axeLockJson({ pkgs: { "node_modules/playwright": axePackage({ version: "1.64.0" }) } }),
            }),
        ],
        ["build.yml 用 npm install", true, with_({ [BUILD_WORKFLOW]: buildWith("npm install --ignore-scripts") })],
        [
            "build.yml 的 npm ci 没带 --ignore-scripts",
            true,
            with_({ [BUILD_WORKFLOW]: buildWith("npm ci --no-audit") }),
        ],
        [
            "build.yml 里装了别的 npm 命令",
            true,
            with_({ [BUILD_WORKFLOW]: buildWith("npm ci --ignore-scripts && npm run build") }),
        ],
        ["build.yml 没有 npm ci（安装步骤被删）", true, with_({ [BUILD_WORKFLOW]: buildWith("true") })],
        [
            "build.yml 只在注释里写 npm ci",
            true,
            with_({ [BUILD_WORKFLOW]: buildWith("true # npm ci --ignore-scripts") }),
        ],
        [
            "build.yml 没设 MICIRI_REQUIRE_AXE",
            true,
            with_({ [BUILD_WORKFLOW]: BUILD_GOOD.replace("MICIRI_REQUIRE_AXE: '1'", "FOO: '1'") }),
        ],
        [
            "build.yml 把 MICIRI_REQUIRE_AXE 设成 0",
            true,
            with_({ [BUILD_WORKFLOW]: BUILD_GOOD.replace("MICIRI_REQUIRE_AXE: '1'", "MICIRI_REQUIRE_AXE: '0'") }),
        ],
        ["缺 build.yml", true, with_({ [BUILD_WORKFLOW]: null })],
    ];
}

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
            [
                "只有注释里写着 GIT_TAG 的提交号",
                true,
                with_({
                    [CATCH2_FILE]: `FetchContent_Declare(Catch2\n  # GIT_TAG ${CATCH2_SHA})\n  GIT_REPOSITORY https://github.com/catchorg/Catch2.git)\n`,
                }),
            ],
            [
                "注释里有旧 tag，真正的提交号仍认得出",
                false,
                with_({ [CATCH2_FILE]: `# 旧写法：GIT_TAG v3.5.4\n${catch2Source(`GIT_TAG ${CATCH2_SHA}`)}` }),
            ],
            ["Catch2 GIT_TAG 是 40 位提交号", false, with_({ [CATCH2_FILE]: catch2Source(`GIT_TAG ${CATCH2_SHA}`) })],
            ...[".juce-commit", ".juce-version", ".pluginval-version", "scripts/ci/mac/pluginval-macos.sha256"].map(
                (f) => [`缺构建钉版 ${f}`, true, with_({ [f]: null })],
            ),
            // axe 工具链：确切版本、sha512、官方源、npm ci --ignore-scripts、axe 必须运行
            ...axeCases(with_),
        ].map(([label, bad, errors]) => ({ label, bad, passed: bad ? errors.length > 0 : errors.length === 0 }));
        return caseResult(cases);
    },
};
