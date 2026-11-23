// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from synchain-oss/rinne; see upstream/web.json
//
// 无头浏览器 + DevTools 协议，零 npm 依赖：靠两件 Node 内置，fetch（取协议目标列表）和全局 WebSocket（Node 22 起有）。
// 不引入 puppeteer：仓库不带运行时依赖。起的是独立 user-data-dir 的无头实例，不碰用户正在用的浏览器，也不用任何扩展。
// 用哪个浏览器见 browserCandidates：先 MICIRI_BROWSER，再 Playwright 自带的 Chromium，最后系统里的 Chrome。收尾先发 CDP 的 Browser.close
// 让它自己退出，只有退不出去才强杀进程树。
// 无头浏览器的光栅路径和真机不同，颜色、字体和亚像素可能有极小差异；本库用来回归和「有没有画出来」，设计验收以真机为准。
// 本机同时只能有一个浏览器冒烟：调用方在 gates 里持 gui 锁；手动跑 run-all 时自己别并行。

import { spawn, spawnSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { makeTempDir, removeTempDir } from "../../../scripts/lib/safe-rm.mjs";

// 不用 Edge：Edge 每启动一次就向外壳注册一个标签代理窗口（Windows.Internal.Shell.TabProxyWindow），进程被强杀时不会注销，
// 泄漏的窗口越积越多，拖慢整台机器上的每一次窗口操作。Playwright 自带的 Chromium 没有这个问题，所以候选里不再有任何 Edge 路径。

/** 系统里装的 Chrome / Chromium，排在自带的 Chromium 之后。 */
const SYSTEM_CHROME = [
    "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
    "C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe",
    "/usr/bin/google-chrome",
    "/usr/bin/chromium",
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
];

// Playwright 把完整的 Chromium 装在 <PLAYWRIGHT_BROWSERS_PATH>/chromium-<修订号>/ 下，可执行文件的相对位置按平台与架构不同。
// 前一项是钉版 playwright（web-preview/tests/axe/package.json）的布局，后一项是旧版的布局，旧环境里装的也认。
// 同目录旁边还有 chromium_headless_shell-<修订号>：那是另一个二进制，不认 --headless=new，名字也不匹配 chromium-<数字>，不会被选中。
const MAC_APP = ["Google Chrome for Testing.app", "Contents", "MacOS", "Google Chrome for Testing"];
const BUNDLED_LAYOUTS = {
    "win32-x64": [
        ["chrome-win64", "chrome.exe"],
        ["chrome-win", "chrome.exe"],
    ],
    "win32-arm64": [
        ["chrome-win64", "chrome.exe"],
        ["chrome-win", "chrome.exe"],
    ],
    "linux-x64": [
        ["chrome-linux64", "chrome"],
        ["chrome-linux", "chrome"],
    ],
    "linux-arm64": [
        ["chrome-linux-arm64", "chrome"],
        ["chrome-linux", "chrome"],
    ],
    "darwin-x64": [
        ["chrome-mac-x64", ...MAC_APP],
        ["chrome-mac", "Chromium.app", "Contents", "MacOS", "Chromium"],
    ],
    "darwin-arm64": [
        ["chrome-mac-arm64", ...MAC_APP],
        ["chrome-mac", "Chromium.app", "Contents", "MacOS", "Chromium"],
    ],
};

/**
 * browsersPath 下 Playwright 自带 Chromium 的可执行文件候选，修订号大的在前；没设目录、读不了目录、平台不认时返回空数组。
 * 只列路径，不检查文件在不在（半装好的目录由 findBrowser 逐个检查后跳过）。
 */
export function bundledChromiumCandidates(
    browsersPath,
    { platform = process.platform, arch = process.arch, readdir = fs.readdirSync } = {},
) {
    const layouts = BUNDLED_LAYOUTS[`${platform}-${arch}`];
    if (!browsersPath || !layouts) return [];
    let names;
    try {
        names = readdir(browsersPath);
    } catch {
        return [];
    }
    return names
        .map((name) => ({ name, m: /^chromium-(\d+)$/.exec(name) }))
        .filter((e) => e.m)
        .sort((a, b) => Number(b.m[1]) - Number(a.m[1]))
        .flatMap((e) => layouts.map((parts) => path.join(browsersPath, e.name, ...parts)));
}

/**
 * 浏览器候选，按优先级：环境变量 MICIRI_BROWSER → Playwright 自带的 Chromium（PLAYWRIGHT_BROWSERS_PATH，gates 的页面冒烟与 CI 的
 * web-smoke 都会设）→ 系统里的 Chrome / Chromium。参数只为自测注入。
 */
export function browserCandidates({ env = process.env, platform, arch, readdir } = {}) {
    return [
        env.MICIRI_BROWSER,
        ...bundledChromiumCandidates(env.PLAYWRIGHT_BROWSERS_PATH, { platform, arch, readdir }),
        ...SYSTEM_CHROME,
    ];
}

/** 找一个可用的 Chromium 系浏览器；找不到返回 null（调用方按跳过处理，不记通过）。参数只为自测注入。 */
export function findBrowser({ exists = fs.existsSync, ...rest } = {}) {
    return browserCandidates(rest).find((p) => p && exists(p)) || null;
}

/** 找不到浏览器时给人看的提示，各处冒烟的跳过原因与启动错误共用。 */
export const NO_BROWSER_HINT =
    "没有 Chromium 系浏览器；可用环境变量 MICIRI_BROWSER 指定，或让 PLAYWRIGHT_BROWSERS_PATH 指向 playwright 装好浏览器的目录";

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

/**
 * fetch 的禁用端口表：Fetch 标准的 bad port 列表（https://fetch.spec.whatwg.org/#port-blocking），Node 的 fetch（undici）
 * 对这些端口直接抛 `TypeError: fetch failed`（cause 是 bad port），根本不发请求。调试端口由系统随机分配（--remote-debugging-port=0），
 * 偶尔落在表里，之后对 http://127.0.0.1:<端口>/json/... 的每一次 fetch 都会抛错，所以拿到这样的端口就当这一次启动失败。
 * 表多收几项只会多重起一次，少收才会让冒烟偶发红；smoke-browser-launch 核对表里每一项在当前 Node 上确实被 fetch 拒绝。
 */
export const FETCH_BAD_PORTS = Object.freeze([
    1, 7, 9, 11, 13, 15, 17, 19, 20, 21, 22, 23, 25, 37, 42, 43, 53, 69, 77, 79, 87, 95, 101, 102, 103, 104, 109, 110,
    111, 113, 115, 117, 119, 123, 135, 137, 139, 143, 161, 179, 389, 427, 465, 512, 513, 514, 515, 526, 530, 531, 532,
    540, 548, 554, 556, 563, 587, 601, 636, 989, 990, 993, 995, 1719, 1720, 1723, 2049, 3659, 4045, 4190, 5060, 5061,
    6000, 6566, 6665, 6666, 6667, 6668, 6669, 6679, 6697, 10080,
]);
const FETCH_BAD_PORT_SET = new Set(FETCH_BAD_PORTS);

/** 这个端口是否在 fetch 的禁用端口表里（对它的 fetch 会直接抛 bad port）。 */
export function isFetchBadPort(port) {
    return FETCH_BAD_PORT_SET.has(Number(port));
}

/**
 * 开调试端口的有界重试。无头浏览器偶尔在负载下 10 秒内写不出 DevToolsActivePort（CI 与本机都见过），这一次没起来不等于页面坏了，
 * 所以换一个新 profile 重起；但次数与总时长都有上界，重试完仍没起来照常抛错判红，不改成跳过：页面冒烟是 required check。
 * attempts：最多起几次（含第一次）；waitMs：每次等端口文件多久；backoffMs：第 k 次失败后等多久再起（不够长就重复最后一项）；
 * totalMs：从第一次起算的总时长上界，下一次起不完就不再起。
 */
export const LAUNCH_POLICY = Object.freeze({ attempts: 3, waitMs: 10000, backoffMs: [1000, 2000], totalMs: 45000 });

/**
 * 按 policy 有界重试 attempt(k)（k 从 1 起）：返回非 null 就算起来了，返回 null 就退避再起。返回 { value, attempts, retries, ms }；
 * 次数用完或总时长不够再起一次时抛错，错误里写明起了几次、重试了几次、用了多久（err.launchAttempts 是起的次数）。
 * onRetry(k, pauseMs) 在每次退避之前调用（打日志用）；now、wait 可以注入假时钟（自测用）。
 */
export async function withLaunchRetry(
    attempt,
    policy = LAUNCH_POLICY,
    { now = Date.now, wait = sleep, onRetry = () => {} } = {},
) {
    const t0 = now();
    for (let k = 1; ; k++) {
        const value = await attempt(k);
        if (value != null) return { value, attempts: k, retries: k - 1, ms: now() - t0 };
        const pause = policy.backoffMs[Math.min(k - 1, policy.backoffMs.length - 1)] ?? 0;
        const spent = now() - t0;
        if (k >= policy.attempts || spent + pause + policy.waitMs > policy.totalMs) {
            const err = new Error(
                `浏览器起了 ${k} 次，每次都没在 ${policy.waitMs / 1000} 秒内开出可用的调试端口（重试 ${k - 1} 次，共 ${(spent / 1000).toFixed(1)} 秒）`,
            );
            err.launchAttempts = k;
            throw err;
        }
        onRetry(k, pause);
        await wait(pause);
    }
}

/** 一个页面：eval、goto、等条件、收集错误。 */
class Page {
    constructor(ws, targetId, port) {
        this.ws = ws;
        this.targetId = targetId;
        this.port = port;
        this.errors = [];
        this.id = 0;
        this.pending = new Map();
        ws.addEventListener("message", (ev) => {
            const m = JSON.parse(ev.data);
            if (m.method && this.requests && m.method === "Network.requestWillBeSent")
                this.requests.push(m.params.request.url);
            if (m.id && this.pending.has(m.id)) {
                this.pending.get(m.id)(m);
                this.pending.delete(m.id);
            } else if (m.method === "Runtime.exceptionThrown") {
                const d = m.params.exceptionDetails;
                this.errors.push(`异常：${d.exception?.description || d.text}`);
            } else if (m.method === "Runtime.consoleAPICalled" && m.params.type === "error") {
                this.errors.push(`console.error：${m.params.args.map((a) => a.value ?? a.description).join(" ")}`);
            } else if (m.method === "Log.entryAdded" && m.params.entry.level === "error") {
                this.errors.push(`日志：${m.params.entry.text} ${m.params.entry.url || ""}`);
            }
        });
    }

    send(method, params = {}) {
        return new Promise((resolve, reject) => {
            const id = ++this.id;
            const timer = setTimeout(() => reject(new Error(`CDP ${method} 超时`)), 30000);
            this.pending.set(id, (m) => {
                clearTimeout(timer);
                if (m.error) reject(new Error(`CDP ${method}：${m.error.message}`));
                else resolve(m.result);
            });
            this.ws.send(JSON.stringify({ id, method, params }));
        });
    }

    async init(width, height) {
        await this.send("Page.enable");
        await this.send("Runtime.enable");
        await this.send("Log.enable");
        await this.setViewport(width, height);
    }

    setViewport(width, height, deviceScaleFactor = 1) {
        return this.send("Emulation.setDeviceMetricsOverride", { width, height, deviceScaleFactor, mobile: false });
    }

    async goto(url) {
        await this.send("Page.navigate", { url });
        await this.waitFor("document.readyState === 'complete'", { timeout: 20000 });
    }

    /** 在页面里求值（可返回 Promise），按值返回；页面抛异常时这里也抛。 */
    async eval(expression) {
        const r = await this.send("Runtime.evaluate", { expression, awaitPromise: true, returnByValue: true });
        if (r.exceptionDetails) {
            throw new Error(`页面求值失败：${r.exceptionDetails.exception?.description || r.exceptionDetails.text}`);
        }
        return r.result.value;
    }

    /** 轮询到表达式为真；超时抛错，带最后一次的值。 */
    async waitFor(expression, { timeout = 10000, step = 40, label = expression } = {}) {
        const t0 = Date.now();
        let last;
        for (;;) {
            try {
                last = await this.eval(expression);
            } catch (e) {
                last = String(e.message);
            }
            if (last === true || (last && typeof last !== "string")) return last;
            if (Date.now() - t0 > timeout)
                throw new Error(`等待超时（${timeout} ms）：${label}；最后读到 ${JSON.stringify(last)}`);
            await sleep(step);
        }
    }

    /** 截图，只返回 PNG 字节；写文件由调用方负责（且只能写到它自己限定的目录）。 */
    async screenshot() {
        const r = await this.send("Page.captureScreenshot", { format: "png" });
        return Buffer.from(r.data, "base64");
    }

    /** 开始记录页面发出的所有网络请求（Network.requestWillBeSent），返回会持续增长的 URL 数组。 */
    async recordRequests() {
        this.requests = [];
        await this.send("Network.enable");
        return this.requests;
    }

    async close() {
        try {
            this.ws.close();
            await fetch(`http://127.0.0.1:${this.port}/json/close/${this.targetId}`).catch(() => {});
        } catch {
            // 已经关了
        }
    }
}

/**
 * 向浏览器级 CDP 端点发 Browser.close，让浏览器自己收尾退出：比强杀干净（Edge 被强杀会留下标签代理窗口，见文件开头）。
 * 不等应答也行：浏览器收到命令就开始退出，应答不一定来得及送到。连不上、地址缺失、超时都抛错，由调用方退到强杀。
 */
export async function cdpBrowserClose(port, { timeoutMs = 3000 } = {}) {
    const res = await fetch(`http://127.0.0.1:${port}/json/version`, { signal: AbortSignal.timeout(timeoutMs) });
    const { webSocketDebuggerUrl } = await res.json();
    if (!webSocketDebuggerUrl) throw new Error("/json/version 没给浏览器级调试地址");
    const ws = new WebSocket(webSocketDebuggerUrl);
    try {
        await new Promise((resolve, reject) => {
            const timer = setTimeout(() => reject(new Error("连浏览器级调试端口超时")), timeoutMs);
            ws.addEventListener(
                "open",
                () => {
                    clearTimeout(timer);
                    resolve();
                },
                { once: true },
            );
            ws.addEventListener(
                "error",
                () => {
                    clearTimeout(timer);
                    reject(new Error("连不上浏览器级调试端口"));
                },
                { once: true },
            );
        });
        await new Promise((resolve) => {
            const done = () => {
                clearTimeout(timer);
                resolve();
            };
            const timer = setTimeout(done, timeoutMs);
            for (const ev of ["message", "close", "error"]) ws.addEventListener(ev, done, { once: true });
            ws.send(JSON.stringify({ id: 1, method: "Browser.close" }));
        });
    } finally {
        try {
            ws.close();
        } catch {
            // 已经关了
        }
    }
}

/** 强杀进程树。Windows 上 Chromium 还有 GPU / 工具子进程握着 profile 里的文件：只杀主进程，子进程一直不放，目录就删不掉；连子进程一起结束。 */
function killProcessTree(proc) {
    try {
        if (process.platform === "win32" && proc.pid)
            spawnSync("taskkill", ["/pid", String(proc.pid), "/T", "/F"], { stdio: "ignore" });
        proc.kill();
    } catch {
        // 已经退出
    }
}

const hasExited = (proc) => proc.exitCode != null || proc.signalCode != null;

/** 等进程的 exit 事件，最多 ms 毫秒；等到返回 true。 */
function waitForExit(proc, ms) {
    if (hasExited(proc)) return Promise.resolve(true);
    return new Promise((resolve) => {
        const onExit = () => {
            clearTimeout(timer);
            resolve(true);
        };
        const timer = setTimeout(() => {
            proc.removeListener?.("exit", onExit);
            resolve(false);
        }, ms);
        proc.once("exit", onExit);
    });
}

/**
 * 收掉浏览器进程。有调试端口就先发 Browser.close，等进程 exit（最多 graceMs）；没退出、或根本发不出去，才强杀进程树，
 * 并往标准错误写一行说明为什么要强杀。没有可用的调试端口（没开出来、端口被 fetch 禁用）时没法对话，直接强杀，不算“退到强杀”，不写这一行。
 * 返回 "gone"（早已退出）、"graceful"（自己退出的）或 "killed"（强杀的）。
 * closeBrowser、killTree、graceMs 只为自测注入。
 */
export async function stopBrowser(
    proc,
    port,
    { closeBrowser = cdpBrowserClose, killTree = killProcessTree, graceMs = 5000 } = {},
) {
    if (hasExited(proc)) return "gone";
    if (port) {
        let why = "";
        try {
            await closeBrowser(port);
        } catch (e) {
            why = e.message;
        }
        // 命令没送出去也可能是浏览器已经在退出了：给它一小段时间，而不是立刻强杀
        if (await waitForExit(proc, why ? Math.min(graceMs, 1000) : graceMs)) return "graceful";
        console.error(`[浏览器] 没能正常关闭（${why || `${graceMs / 1000} 秒内没退出`}），已强制结束进程树`);
    }
    killTree(proc);
    return "killed";
}

/**
 * 起一次浏览器：新的临时 profile，等 waitMs 看它写不写出调试端口。写出来且 fetch 连得上（不在禁用端口表里）返回 { port, cleanup }；
 * 没写出来，或写出来的端口在 fetch 的禁用端口表里，就收掉这一次的进程树与 profile，返回 null（由 withLaunchRetry 决定还起不起）。
 * cleanup() 先让浏览器自己退出、不行再强杀（见 stopBrowser），然后删 profile，返回 stopBrowser 的结果。
 * spawnProc、closeBrowser、killTree、graceMs 只为自测注入假浏览器，正常调用不传。
 */
export async function launchOnce(exe, waitMs, { spawnProc = spawn, ...stopOpts } = {}) {
    const profile = makeTempDir("miciri-pv-");
    const proc = spawnProc(
        exe,
        [
            "--headless=new",
            // Linux 上不带沙箱：CI 的 Ubuntu 限制了非发行版自带浏览器的用户命名空间，带沙箱起不来；Playwright 自己启动时默认也不带。
            // 这里只加载本机静态服务给的预览页，profile 是一次性的。
            ...(process.platform === "linux" ? ["--no-sandbox"] : []),
            "--disable-gpu",
            "--hide-scrollbars",
            "--no-first-run",
            "--no-default-browser-check",
            "--disable-extensions",
            "--remote-debugging-port=0",
            `--user-data-dir=${profile}`,
            "about:blank",
        ],
        { stdio: "ignore" },
    );
    // 起不起来（可执行文件坏了之类）不让未处理的 error 事件打崩进程：这一次算没开出端口，不再干等
    let spawnError = null;
    proc.once("error", (e) => {
        spawnError = e;
    });
    let port = 0;
    const portFile = path.join(profile, "DevToolsActivePort");
    // 按墙钟截止而不是按轮询次数：机器负载高时 100 ms 的 sleep 会拉长，数满 100 次可能远超 waitMs
    const deadline = Date.now() + waitMs;
    while (!port && !spawnError && Date.now() < deadline) {
        await sleep(100);
        try {
            port = Number(fs.readFileSync(portFile, "utf8").split("\n")[0]);
        } catch {
            // 还没写出来
        }
    }
    // talkPort 是能对话的调试端口；没有就是 0，直接强杀
    const cleanupWith = async (talkPort) => {
        const how = await stopBrowser(proc, talkPort, stopOpts);
        if (how === "killed") await sleep(300);
        let why;
        for (let i = 0; i < 20; i++) {
            why = removeTempDir(profile);
            if (why === null) return how;
            await sleep(300); // 浏览器进程还握着文件：等一等再删
        }
        console.error(`浏览器的临时 profile 没清掉：${why}`);
        return how;
    };
    // 主进程被强杀（超时）时 finally 不会执行；正常退出和未捕获异常时 exit 事件还能尽力清一次
    process.once("exit", () => {
        removeTempDir(profile);
    });
    // 系统分配的端口落在 fetch 的禁用端口表里：之后对它的 fetch 一律抛 bad port。这一次作废，走和「没开出端口」同一条收尾（收进程树与 profile）
    let badPort = 0;
    if (port && isFetchBadPort(port)) {
        badPort = port;
        port = 0;
    }
    if (!port) {
        await cleanupWith(0);
        if (spawnError) console.error(`[浏览器] 这一次没起来：${spawnError.message}`);
        if (badPort)
            console.error(
                `[浏览器] 系统分配的调试端口 ${badPort} 在 fetch 的禁用端口表里（会报 bad port），这一次作废`,
            );
        return null;
    }
    return { port, cleanup: () => cleanupWith(port) };
}

/**
 * 启动无头浏览器。返回 { newPage(), close(), launch }；launch 是 { attempts, retries, ms }：第几次起来的、重试了几次、用了多久。
 * 开调试端口按 LAUNCH_POLICY 有界重试，每次重试与「重试之后才起来」都往标准错误写一行（run-all 会把它当作这一套的最后一行显示），
 * 便于与 CI 上的偶发对拍；重试完仍没起来照常抛错。
 * @param {object} [o] width、height：初始视口。
 */
export async function launchBrowser({ width = 1280, height = 900 } = {}) {
    const exe = findBrowser();
    if (!exe) throw new Error(`找不到 Chromium 系浏览器：${NO_BROWSER_HINT}`);
    const got = await withLaunchRetry(() => launchOnce(exe, LAUNCH_POLICY.waitMs), LAUNCH_POLICY, {
        onRetry: (k, pause) =>
            console.error(
                `[浏览器] 第 ${k} 次启动没在 ${LAUNCH_POLICY.waitMs / 1000} 秒内开出可用的调试端口，${pause / 1000} 秒后重起（最多起 ${LAUNCH_POLICY.attempts} 次）`,
            ),
    });
    const { port, cleanup } = got.value;
    const launch = { attempts: got.attempts, retries: got.retries, ms: got.ms };
    if (launch.retries)
        console.error(
            `[浏览器] 调试端口第 ${launch.attempts} 次启动才开出（重试 ${launch.retries} 次，共 ${(launch.ms / 1000).toFixed(1)} 秒）`,
        );
    const pages = [];
    return {
        launch,
        async newPage() {
            const t = await (await fetch(`http://127.0.0.1:${port}/json/new?about:blank`, { method: "PUT" })).json();
            const ws = new WebSocket(t.webSocketDebuggerUrl);
            await new Promise((resolve, reject) => {
                ws.addEventListener("open", resolve, { once: true });
                ws.addEventListener("error", () => reject(new Error("连不上调试端口")), { once: true });
            });
            const page = new Page(ws, t.id, port);
            await page.init(width, height);
            pages.push(page);
            return page;
        },
        async close() {
            for (const p of pages) await p.close();
            await cleanup();
        },
    };
}
