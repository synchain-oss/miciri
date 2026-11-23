// SPDX-License-Identifier: GPL-3.0-or-later
//
// 页面冒烟用哪个浏览器：候选顺序与挑选逻辑。不需要真浏览器：目录与文件是否存在都用注入的假数据，所以 --no-browser 也会跑。
// 顺序是 MICIRI_BROWSER → Playwright 自带的 Chromium（PLAYWRIGHT_BROWSERS_PATH 下最新的 chromium-<修订号>）→ 系统里的 Chrome。
// 候选里没有任何 Edge：Edge 被强杀会在外壳里留下标签代理窗口，越积越多，拖慢整台机器上的窗口操作。
//   1. bundledChromiumCandidates：修订号大的在前，不选 chromium_headless_shell 之类的邻居，每个平台的可执行文件位置对；
//   2. browserCandidates：环境变量在前、自带 Chromium 居中、系统 Chrome 在后，任何平台都没有 Edge；
//   3. findBrowser：按顺序取第一个存在的；半装好的新修订号会被跳过；只装了 Edge 时找不到（返回 null，由调用方按跳过处理）；
//   4. 真实环境：设了 PLAYWRIGHT_BROWSERS_PATH 且里面真有 chromium-<数字> 时，findBrowser 必须落在那个目录里
//      （CI 里 playwright install 装出的实际布局不被认出来，这一条会红）。
//
// 用法：node web-preview/tests/smoke-browser-find.mjs [仓库根]

import fs from "node:fs";
import path from "node:path";
import { createRun } from "./lib/harness.mjs";
import { browserCandidates, bundledChromiumCandidates, findBrowser } from "./lib/browser.mjs";

const run = createRun("浏览器候选顺序");

const BASE = path.join(path.sep, "pw-browsers");
// 目录里有新旧两个修订号、无头 shell、别的组件、一个看着像但不是数字的名字
const NAMES = [
    "chromium-1100",
    "chromium_headless_shell-1248",
    "chromium-1248",
    "ffmpeg-1013",
    "winldd-1007",
    "chromium-999",
    "chromium-abc",
];
const readdir = () => NAMES;
const J = (...p) => path.join(BASE, ...p);
const MAC_APP = ["Google Chrome for Testing.app", "Contents", "MacOS", "Google Chrome for Testing"];

// ---- 1. bundledChromiumCandidates ----
{
    const got = bundledChromiumCandidates(BASE, { platform: "win32", arch: "x64", readdir });
    run.eq(
        got,
        [
            J("chromium-1248", "chrome-win64", "chrome.exe"),
            J("chromium-1248", "chrome-win", "chrome.exe"),
            J("chromium-1100", "chrome-win64", "chrome.exe"),
            J("chromium-1100", "chrome-win", "chrome.exe"),
            J("chromium-999", "chrome-win64", "chrome.exe"),
            J("chromium-999", "chrome-win", "chrome.exe"),
        ],
        "Windows：修订号大的在前（按数字比，999 排在 1100 之后），每个修订号先钉版布局、后旧布局",
    );
    run.check(
        !got.some((p) => p.includes("headless_shell") || p.includes("ffmpeg") || p.includes("chromium-abc")),
        "不该选无头 shell、ffmpeg 与名字不是 chromium-<数字> 的目录",
    );
}
const firstOf = (platform, arch) => bundledChromiumCandidates(BASE, { platform, arch, readdir })[0];
run.eq(firstOf("linux", "x64"), J("chromium-1248", "chrome-linux64", "chrome"), "Linux x64 的可执行文件位置");
run.eq(firstOf("linux", "arm64"), J("chromium-1248", "chrome-linux-arm64", "chrome"), "Linux arm64 的可执行文件位置");
run.eq(firstOf("darwin", "arm64"), J("chromium-1248", "chrome-mac-arm64", ...MAC_APP), "mac arm64 的可执行文件位置");
run.eq(firstOf("darwin", "x64"), J("chromium-1248", "chrome-mac-x64", ...MAC_APP), "mac x64 的可执行文件位置");
run.eq(
    bundledChromiumCandidates(BASE, { platform: "linux", arch: "x64", readdir }).slice(0, 2),
    [J("chromium-1248", "chrome-linux64", "chrome"), J("chromium-1248", "chrome-linux", "chrome")],
    "Linux 同一修订号里钉版布局在旧布局前",
);
run.eq(
    bundledChromiumCandidates(undefined, { platform: "win32", arch: "x64", readdir }),
    [],
    "没设目录：没有自带 Chromium 候选",
);
run.eq(
    bundledChromiumCandidates("", { platform: "win32", arch: "x64", readdir }),
    [],
    "目录为空串：没有自带 Chromium 候选",
);
run.eq(
    bundledChromiumCandidates(BASE, { platform: "freebsd", arch: "x64", readdir }),
    [],
    "不认的平台：没有自带 Chromium 候选",
);
run.eq(
    bundledChromiumCandidates(BASE, {
        platform: "win32",
        arch: "x64",
        readdir: () => {
            throw new Error("ENOENT");
        },
    }),
    [],
    "读不了目录：没有自带 Chromium 候选，不抛错",
);

// ---- 2. browserCandidates ----
const SYSTEM_LIKE = /google-chrome|Google Chrome|\/usr\/bin\/chromium|Google\\Chrome/;
for (const [platform, arch] of [
    ["win32", "x64"],
    ["linux", "x64"],
    ["darwin", "arm64"],
]) {
    const all = browserCandidates({
        env: { MICIRI_BROWSER: "/custom/browser", PLAYWRIGHT_BROWSERS_PATH: BASE },
        platform,
        arch,
        readdir,
    });
    const bundledCount = bundledChromiumCandidates(BASE, { platform, arch, readdir }).length;
    run.check(all[0] === "/custom/browser", `${platform}：MICIRI_BROWSER 应排第一`);
    run.check(
        all.slice(1, 1 + bundledCount).every((p) => p.startsWith(BASE)),
        `${platform}：环境变量之后紧接着是自带 Chromium`,
    );
    run.check(
        all.slice(1 + bundledCount).length > 0 && all.slice(1 + bundledCount).every((p) => SYSTEM_LIKE.test(p)),
        `${platform}：自带 Chromium 之后才是系统里的 Chrome / Chromium`,
    );
    run.check(
        !all.some((p) => /edge/i.test(String(p))),
        `${platform}：候选里不该有 Edge（${all.filter((p) => /edge/i.test(String(p))).join("、")}）`,
    );
}
{
    const none = browserCandidates({ env: {}, platform: "win32", arch: "x64", readdir });
    run.check(
        none.every((p) => p === undefined || !/chromium-\d/.test(p)),
        "没设 PLAYWRIGHT_BROWSERS_PATH：不该有自带 Chromium 候选",
    );
    run.check(!none.some((p) => /edge/i.test(String(p))), "什么环境变量都没设时候选里也没有 Edge");
}

// ---- 3. findBrowser ----
const winArgs = { platform: "win32", arch: "x64", readdir };
const newest = J("chromium-1248", "chrome-win64", "chrome.exe");
const older = J("chromium-1100", "chrome-win64", "chrome.exe");
const SYSTEM_CHROME = "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe";
const pick = (env, existing) => findBrowser({ env, ...winArgs, exists: (p) => existing.includes(p) });
run.eq(
    pick({ PLAYWRIGHT_BROWSERS_PATH: BASE }, [SYSTEM_CHROME, newest, older]),
    newest,
    "自带 Chromium 与系统 Chrome 都在：用自带的最新修订号",
);
run.eq(
    pick({ MICIRI_BROWSER: "/custom/browser", PLAYWRIGHT_BROWSERS_PATH: BASE }, [
        "/custom/browser",
        newest,
        SYSTEM_CHROME,
    ]),
    "/custom/browser",
    "MICIRI_BROWSER 指向的文件存在：用它",
);
run.eq(
    pick({ MICIRI_BROWSER: "/custom/browser", PLAYWRIGHT_BROWSERS_PATH: BASE }, [newest, SYSTEM_CHROME]),
    newest,
    "MICIRI_BROWSER 指向的文件不存在：不报错，往下找自带 Chromium",
);
run.eq(
    pick({ PLAYWRIGHT_BROWSERS_PATH: BASE }, [older, SYSTEM_CHROME]),
    older,
    "最新修订号的目录没装全：跳过它，用旧修订号",
);
run.eq(pick({}, [SYSTEM_CHROME]), SYSTEM_CHROME, "只有系统 Chrome：用系统 Chrome");
run.eq(pick({ PLAYWRIGHT_BROWSERS_PATH: BASE }, []), null, "什么都没有：返回 null（调用方按跳过处理，不记通过）");
{
    // 机器上只装了 Edge：绝不能选它（候选里本来就没有 Edge 路径，这里把任何含 edge 的路径都当作存在，也选不到）
    const got = findBrowser({ env: { PLAYWRIGHT_BROWSERS_PATH: BASE }, ...winArgs, exists: (p) => /edge/i.test(p) });
    run.check(got === null, `只装了 Edge 的机器上应找不到浏览器（实得 ${got}）`);
}

// ---- 4. 真实环境：设了 PLAYWRIGHT_BROWSERS_PATH 且里面有 chromium-<数字> ----
{
    const real = process.env.PLAYWRIGHT_BROWSERS_PATH;
    let hasChromium = false;
    if (real) {
        try {
            hasChromium = fs.readdirSync(real).some((n) => /^chromium-\d+$/.test(n));
        } catch {
            // 目录不存在：没有什么可核对的
        }
    }
    if (real && hasChromium && !process.env.MICIRI_BROWSER) {
        const got = findBrowser();
        const inside = got !== null && path.resolve(got).startsWith(path.resolve(real) + path.sep);
        run.check(
            inside,
            `PLAYWRIGHT_BROWSERS_PATH 下有 chromium-<数字> 目录，但 findBrowser 没选到它里面的可执行文件（实得 ${got}）：布局没被认出来？`,
        );
    }
}

run.finish();
