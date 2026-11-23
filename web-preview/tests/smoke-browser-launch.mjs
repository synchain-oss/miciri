// SPDX-License-Identifier: GPL-3.0-or-later
//
// 无头浏览器的启动与收尾。本文件不需要真浏览器：用假的 spawn 写出 DevToolsActivePort、假进程发 exit 事件，所以 --no-browser 也会跑。
// 启动：系统随机分配的调试端口偶尔落在 fetch 的禁用端口表里（Fetch 标准的 bad port 列表），之后对 http://127.0.0.1:<端口>/json/... 的
// fetch 一律抛 bad port。拿到这样的端口，这一次启动必须作废（收进程树与临时 profile）并交给 withLaunchRetry 有界重试。
// 收尾：先发 CDP 的 Browser.close 让浏览器自己退出，等它 exit；只有没退出、或发不出去才强杀进程树，并写一行说明。
//   1. isFetchBadPort：已知禁用端口为 bad、普通端口（含系统常分配的高端口）为 ok；
//   2. 表里每一项在当前 Node 上都真的被 fetch 拒绝（防抄错端口）；
//   3. launchOnce：禁用端口返回 null 且 profile 被收掉；普通端口返回 { port } 且 cleanup 能收掉 profile；
//   4. withLaunchRetry：第一次拿到禁用端口、第二次正常，就在第二次起来；每次都拿到禁用端口，在次数上限处抛错，不无限重起；
//   5. 收尾逻辑：自己退出就不强杀；没退出或发不出去才强杀并写一行；没有可用端口时不去对话直接强杀；早已退出的不再动；
//   6. cdpBrowserClose 本身：对一个手写的最小 CDP 服务器真发 Browser.close，对端不应答时也按超时返回，连不上时抛错；
//   7. --no-sandbox 只给 Linux 上 Playwright 自带的 Chromium，系统 Chrome 与 MICIRI_BROWSER 指定的照常带沙箱。
//
// 用法：node web-preview/tests/smoke-browser-launch.mjs [仓库根]

import crypto from "node:crypto";
import { EventEmitter } from "node:events";
import fs from "node:fs";
import http from "node:http";
import path from "node:path";
import { createRun } from "./lib/harness.mjs";
import {
    FETCH_BAD_PORTS,
    LAUNCH_POLICY,
    cdpBrowserClose,
    isFetchBadPort,
    launchOnce,
    needsNoSandbox,
    withLaunchRetry,
} from "./lib/browser.mjs";

const run = createRun("浏览器启动与收尾：禁用端口判定、重试、优雅关闭");

// ---- 1. 判定 ----
for (const p of [1, 22, 6000, 6666, 6697, 10080])
    run.check(isFetchBadPort(p) === true, `端口 ${p} 应判为 fetch 禁用端口`);
for (const p of [80, 443, 8080, 9222, 33333, 40000, 65535])
    run.check(isFetchBadPort(p) === false, `端口 ${p} 不该判为禁用端口`);
run.check(
    isFetchBadPort("6000") === true,
    "字符串形式的禁用端口也要判出来（DevToolsActivePort 里读出来的是文本转的数）",
);
run.check(!isFetchBadPort(0) && !isFetchBadPort(NaN), "0 与 NaN（没读到端口）不算禁用端口，由调用方按没开出端口处理");
run.check(
    FETCH_BAD_PORTS.length === 82,
    `禁用端口表应有 82 项（实得 ${FETCH_BAD_PORTS.length}）；改表请同时对照 Fetch 标准`,
);

run.check(new Set(FETCH_BAD_PORTS).size === FETCH_BAD_PORTS.length, "禁用端口表里不该有重复项");

// ---- 2. 表里每一项都被当前 Node 的 fetch 拒绝（fetch 在发请求之前就抛，不会连任何东西）----
const notRejected = [];
for (const p of FETCH_BAD_PORTS) {
    try {
        await fetch(`http://127.0.0.1:${p}/json/version`, { signal: AbortSignal.timeout(3000) });
        notRejected.push(p);
    } catch (e) {
        if (!/bad port/i.test(String(e.cause?.message ?? e.message))) notRejected.push(p);
    }
}
run.check(
    notRejected.length === 0,
    `禁用端口表里这些端口在 ${process.version} 的 fetch 上没有抛 bad port：${notRejected.join(", ")}`,
);

// ---- 假浏览器：假进程 + 假 spawn ----
/** 假进程：pid 不设（不会真去 taskkill），exit() 模拟进程退出（先置 exitCode 再发 exit 事件，和真的 ChildProcess 一样）。 */
class FakeProc extends EventEmitter {
    pid = undefined;
    exitCode = null;
    signalCode = null;
    exit() {
        this.exitCode = 0;
        this.emit("exit", 0, null);
    }
    kill() {
        // 强杀由注入的 killTree 记数；这里什么都不做
    }
}

/**
 * 假的 spawn：从参数里取 --user-data-dir，往里写 DevToolsActivePort；不起任何进程。
 * mode 决定注入给 launchOnce 的收尾行为：
 *   "exit"  closeBrowser 发出后进程退出（正常关闭）；
 *   "stuck" closeBrowser 发出了，进程却不退出；
 *   "throw" closeBrowser 抛错（连不上）。
 * closeBrowser、killTree 都只记数，不碰网络、不杀进程；graceMs 取很短，免得测试干等。
 */
function fakeBrowser(port, mode = "exit") {
    const seen = [];
    const procs = [];
    const argsSeen = [];
    const calls = { close: 0, kill: 0 };
    const spawnProc = (_exe, args) => {
        const profile = args.find((a) => a.startsWith("--user-data-dir=")).slice("--user-data-dir=".length);
        seen.push(profile);
        argsSeen.push(args);
        fs.writeFileSync(path.join(profile, "DevToolsActivePort"), `${port}\n/devtools/browser/fake\n`);
        const proc = new FakeProc();
        procs.push(proc);
        return proc;
    };
    const closeBrowser = async () => {
        calls.close++;
        if (mode === "throw") throw new Error("连不上浏览器级调试端口");
        if (mode === "exit") procs.at(-1).exit();
    };
    const killTree = () => {
        calls.kill++;
    };
    return { opts: { spawnProc, closeBrowser, killTree, graceMs: 50 }, seen, procs, calls, argsSeen };
}

/** 抓标准错误：launchOnce 把作废原因、强杀原因写到 console.error，测试里收下来，免得它成了 run-all 显示的最后一行。 */
async function withCapturedStderr(fn) {
    const lines = [];
    const orig = console.error;
    console.error = (...a) => lines.push(a.join(" "));
    try {
        return { result: await fn(), lines };
    } finally {
        console.error = orig;
    }
}

// ---- 3. launchOnce：用假浏览器写出指定端口 ----
{
    const fake = fakeBrowser(6000);
    const { result, lines } = await withCapturedStderr(() => launchOnce("fake-browser", 2000, fake.opts));
    run.check(
        result === null,
        "拿到禁用端口 6000 时 launchOnce 应返回 null（这一次启动作废），实得 " + JSON.stringify(result),
    );
    run.check(fake.seen.length === 1 && !fs.existsSync(fake.seen[0]), "禁用端口作废后，这一次的临时 profile 应被收掉");
    run.check(
        lines.some((l) => l.includes("6000") && l.includes("bad port")),
        "作废时应往标准错误写明端口与原因",
    );
}
{
    const fake = fakeBrowser(40123);
    const { result } = await withCapturedStderr(() => launchOnce("fake-browser", 2000, fake.opts));
    run.check(
        result !== null && result.port === 40123,
        "普通端口 40123 应当被接受，实得 " + JSON.stringify(result?.port ?? null),
    );
    run.check(fake.seen.length === 1 && fs.existsSync(fake.seen[0]), "接受的启动在 cleanup 之前 profile 还在");
    if (result) {
        await result.cleanup();
        run.check(!fs.existsSync(fake.seen[0]), "cleanup 之后 profile 应被收掉");
    }
}

// ---- 4. 与 withLaunchRetry 串起来：假时钟跳过退避，不真等 ----
const noWait = { now: () => 0, wait: async () => {} };
{
    const ports = [6666, 40123];
    let used = 0;
    const profiles = [];
    const attempt = async () => {
        const fake = fakeBrowser(ports[used++]);
        const r = await launchOnce("fake-browser", 2000, fake.opts);
        profiles.push(...fake.seen);
        return r;
    };
    const { result: got } = await withCapturedStderr(() => withLaunchRetry(attempt, LAUNCH_POLICY, noWait));
    run.check(
        got.value.port === 40123 && got.attempts === 2 && got.retries === 1,
        "第一次禁用端口、第二次正常：应在第二次起来（重试 1 次）",
    );
    run.check(!fs.existsSync(profiles[0]), "被作废的第一次的 profile 已收掉");
    await got.value.cleanup();
    run.check(!fs.existsSync(profiles[1]), "第二次的 profile 在 cleanup 后收掉");
}
{
    let used = 0;
    const profiles = [];
    const attempt = async () => {
        used++;
        const fake = fakeBrowser(6000);
        const r = await launchOnce("fake-browser", 2000, fake.opts);
        profiles.push(...fake.seen);
        return r;
    };
    let err = null;
    await withCapturedStderr(async () => {
        try {
            await withLaunchRetry(attempt, LAUNCH_POLICY, noWait);
        } catch (e) {
            err = e;
        }
    });
    run.check(
        err !== null && err.launchAttempts === LAUNCH_POLICY.attempts,
        "每次都拿到禁用端口：应在次数上限处抛错，不无限重起",
    );
    run.check(used === LAUNCH_POLICY.attempts, `应恰好起 ${LAUNCH_POLICY.attempts} 次（实得 ${used}）`);
    run.check(
        profiles.length === used && profiles.every((p) => !fs.existsSync(p)),
        "每一次作废的 profile 都已收掉，没有残留",
    );
}

// ---- 5. 收尾：先优雅关闭，退不出去才强杀 ----
const forced = (lines) => lines.filter((l) => l.includes("没能正常关闭"));
{
    const fake = fakeBrowser(40123, "exit");
    const { result } = await withCapturedStderr(() => launchOnce("fake-browser", 2000, fake.opts));
    const { result: how, lines } = await withCapturedStderr(() => result.cleanup());
    run.check(how === "graceful", `浏览器收到 Browser.close 后自己退出：应判为 graceful，实得 ${how}`);
    run.check(
        fake.calls.close === 1 && fake.calls.kill === 0,
        `自己退出就不该强杀（发关闭 ${fake.calls.close} 次，强杀 ${fake.calls.kill} 次）`,
    );
    run.check(forced(lines).length === 0, "正常关闭不该写强杀的说明");
    run.check(!fs.existsSync(fake.seen[0]), "正常关闭后 profile 应被收掉");
}
{
    const fake = fakeBrowser(40123, "stuck");
    const { result } = await withCapturedStderr(() => launchOnce("fake-browser", 2000, fake.opts));
    const { result: how, lines } = await withCapturedStderr(() => result.cleanup());
    run.check(how === "killed", `发了关闭但进程不退出：应退到强杀，实得 ${how}`);
    run.check(
        fake.calls.close === 1 && fake.calls.kill === 1,
        `应发 1 次关闭、强杀 1 次（实得 ${fake.calls.close}、${fake.calls.kill}）`,
    );
    run.check(
        forced(lines).length === 1 && forced(lines)[0].includes("没退出"),
        "强杀时应恰好写一行说明，并写明是没退出",
    );
    run.check(!fs.existsSync(fake.seen[0]), "强杀后 profile 也应被收掉");
}
{
    const fake = fakeBrowser(40123, "throw");
    const { result } = await withCapturedStderr(() => launchOnce("fake-browser", 2000, fake.opts));
    const { result: how, lines } = await withCapturedStderr(() => result.cleanup());
    run.check(how === "killed" && fake.calls.kill === 1, "发不出 Browser.close：应退到强杀");
    run.check(forced(lines).length === 1 && forced(lines)[0].includes("连不上"), "强杀的说明里应带上发不出去的原因");
}
{
    // 发关闭时抛了错，但那是因为浏览器已经在退出：不该强杀
    const fake = fakeBrowser(40123, "exit");
    fake.opts.closeBrowser = async () => {
        fake.calls.close++;
        fake.procs.at(-1).exit();
        throw new Error("连接被对端关闭");
    };
    const { result } = await withCapturedStderr(() => launchOnce("fake-browser", 2000, fake.opts));
    const { result: how, lines } = await withCapturedStderr(() => result.cleanup());
    run.check(
        how === "graceful" && fake.calls.kill === 0 && forced(lines).length === 0,
        "发关闭时连接断了但进程已退出：算正常关闭，不强杀",
    );
}
{
    // 没有可用端口（这里是被 fetch 禁用的端口）：没法对话，直接强杀；这不是“退到强杀”，不写那一行
    const fake = fakeBrowser(6000, "exit");
    const { lines } = await withCapturedStderr(() => launchOnce("fake-browser", 2000, fake.opts));
    run.check(fake.calls.close === 0, "没有可用端口时不该去发 Browser.close");
    run.check(fake.calls.kill === 1, `没有可用端口时应直接强杀（强杀 ${fake.calls.kill} 次）`);
    run.check(forced(lines).length === 0, "没有可用端口时的强杀不写“没能正常关闭”");
}
{
    // 进程在 cleanup 之前就已经退出：什么都不用做
    const fake = fakeBrowser(40123, "stuck");
    const { result } = await withCapturedStderr(() => launchOnce("fake-browser", 2000, fake.opts));
    fake.procs.at(-1).exit();
    const { result: how } = await withCapturedStderr(() => result.cleanup());
    run.check(how === "gone" && fake.calls.close === 0 && fake.calls.kill === 0, "进程早已退出：不发关闭、不强杀");
    run.check(!fs.existsSync(fake.seen[0]), "早已退出时 profile 也应被收掉");
}

// ---- 6. cdpBrowserClose 本身：手写的最小 CDP 服务器（/json/version + WebSocket）----
const WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
/** 最小 CDP 服务器：/json/version 给出浏览器级 WebSocket 地址；WebSocket 上收到的 JSON 命令记进 received，respond 为真时回一个空结果。 */
function startFakeCdp({ respond }) {
    const received = [];
    const server = http.createServer((req, res) => {
        if (req.url === "/json/version") {
            res.setHeader("content-type", "application/json");
            res.end(
                JSON.stringify({
                    webSocketDebuggerUrl: `ws://127.0.0.1:${server.address().port}/devtools/browser/fake`,
                }),
            );
        } else {
            res.statusCode = 404;
            res.end();
        }
    });
    server.on("upgrade", (req, socket) => {
        const accept = crypto
            .createHash("sha1")
            .update(req.headers["sec-websocket-key"] + WS_GUID)
            .digest("base64");
        socket.write(
            `HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`,
        );
        socket.on("error", () => {});
        socket.on("data", (buf) => {
            // 客户端发来的帧都带掩码、负载很短（< 126 字节）：只认文本帧（1）和关闭帧（8）
            for (let off = 0; off + 6 <= buf.length;) {
                const op = buf[off] & 0x0f;
                const len = buf[off + 1] & 0x7f;
                const mask = buf.subarray(off + 2, off + 6);
                const payload = Buffer.from(buf.subarray(off + 6, off + 6 + len));
                for (let i = 0; i < len; i++) payload[i] ^= mask[i % 4];
                off += 6 + len;
                if (op === 8) return void socket.end();
                if (op !== 1) continue;
                const msg = JSON.parse(payload.toString("utf8"));
                received.push(msg);
                if (respond) {
                    const reply = Buffer.from(JSON.stringify({ id: msg.id, result: {} }));
                    socket.write(Buffer.concat([Buffer.from([0x81, reply.length]), reply]));
                }
            }
        });
    });
    return new Promise((resolve) =>
        server.listen(0, "127.0.0.1", () =>
            resolve({
                port: server.address().port,
                received,
                stop: () =>
                    new Promise((done) => {
                        server.close(() => done());
                        server.closeAllConnections?.();
                    }),
            }),
        ),
    );
}

{
    const cdp = await startFakeCdp({ respond: true });
    let err = null;
    try {
        await cdpBrowserClose(cdp.port, { timeoutMs: 2000 });
    } catch (e) {
        err = e;
    }
    run.check(err === null, `对正常的 CDP 服务器发 Browser.close 不该抛错：${err?.message}`);
    run.eq(cdp.received, [{ id: 1, method: "Browser.close" }], "服务器应恰好收到一条 Browser.close 命令");
    await cdp.stop();
}
{
    const cdp = await startFakeCdp({ respond: false });
    const t0 = Date.now();
    let err = null;
    try {
        await cdpBrowserClose(cdp.port, { timeoutMs: 300 });
    } catch (e) {
        err = e;
    }
    const ms = Date.now() - t0;
    run.check(err === null && ms < 2000, `对端不应答时应按超时返回而不是挂住（用了 ${ms} ms，错误 ${err?.message}）`);
    run.eq(cdp.received, [{ id: 1, method: "Browser.close" }], "对端不应答时命令也应已送到");
    await cdp.stop();
}
{
    const cdp = await startFakeCdp({ respond: true });
    const port = cdp.port;
    await cdp.stop();
    let err = null;
    try {
        await cdpBrowserClose(port, { timeoutMs: 1000 });
    } catch (e) {
        err = e;
    }
    run.check(err !== null, "连不上调试端口时 cdpBrowserClose 应抛错，让调用方退到强杀");
}

// ---- 7. --no-sandbox 只给 Linux 上 Playwright 自带的 Chromium ----
{
    const PW = path.join(path.sep, "pw-browsers");
    const bundled = path.join(PW, "chromium-1248", "chrome-linux64", "chrome");
    const env = { PLAYWRIGHT_BROWSERS_PATH: PW };
    run.check(needsNoSandbox(bundled, { platform: "linux", env }), "Linux 上自带的 Chromium 要加 --no-sandbox");
    run.check(!needsNoSandbox("/usr/bin/google-chrome", { platform: "linux", env }), "Linux 上系统 Chrome 照常带沙箱");
    run.check(
        !needsNoSandbox("/opt/custom/browser", { platform: "linux", env }),
        "Linux 上 MICIRI_BROWSER 指定的浏览器（不在自带目录下）照常带沙箱",
    );
    run.check(
        !needsNoSandbox(path.join(path.sep, "pw-browsers-other", "chrome"), { platform: "linux", env }),
        "名字只是前缀相同的兄弟目录不算在自带目录之下",
    );
    run.check(!needsNoSandbox(PW, { platform: "linux", env }), "自带目录本身不是浏览器可执行文件，不算");
    run.check(
        needsNoSandbox(path.join(PW, "..cache", "chrome"), { platform: "linux", env }),
        "自带目录下名字以两个点开头的子目录（..cache）仍算在自带目录之下",
    );
    run.check(
        !needsNoSandbox(path.join(PW, "..", "chrome"), { platform: "linux", env }),
        "自带目录的上一级不算在自带目录之下",
    );
    run.check(!needsNoSandbox(bundled, { platform: "linux", env: {} }), "没设 PLAYWRIGHT_BROWSERS_PATH：不加");
    run.check(!needsNoSandbox(bundled, { platform: "win32", env }), "Windows 上不加");
    run.check(!needsNoSandbox(bundled, { platform: "darwin", env }), "mac 上不加");

    // 经 launchOnce 传到浏览器的命令行上
    const withFlag = fakeBrowser(40123);
    await withCapturedStderr(async () => {
        const r = await launchOnce(bundled, 2000, { ...withFlag.opts, platform: "linux", env });
        await r.cleanup();
    });
    run.check(withFlag.argsSeen[0]?.includes("--no-sandbox"), "Linux 上起自带 Chromium：命令行里应有 --no-sandbox");
    const without = fakeBrowser(40123);
    await withCapturedStderr(async () => {
        const r = await launchOnce("/usr/bin/google-chrome", 2000, { ...without.opts, platform: "linux", env });
        await r.cleanup();
    });
    run.check(!without.argsSeen[0]?.includes("--no-sandbox"), "Linux 上起系统 Chrome：命令行里不该有 --no-sandbox");
}

run.finish();
