// SPDX-License-Identifier: GPL-3.0-or-later
//
// 无头浏览器启动的端口判定：系统随机分配的调试端口偶尔落在 fetch 的禁用端口表里（Fetch 标准的 bad port 列表），
// 之后对 http://127.0.0.1:<端口>/json/... 的 fetch 一律抛 bad port。拿到这样的端口，这一次启动必须作废（收进程树与临时
// profile）并交给 withLaunchRetry 有界重试。本文件不需要真浏览器：用假的 spawn 写出 DevToolsActivePort，所以 --no-browser 也会跑。
//   1. isFetchBadPort：已知禁用端口为 bad、普通端口（含系统常分配的高端口）为 ok；
//   2. 表里每一项在当前 Node 上都真的被 fetch 拒绝（防抄错端口）；
//   3. launchOnce：禁用端口返回 null 且 profile 被收掉；普通端口返回 { port } 且 cleanup 能收掉 profile；
//   4. withLaunchRetry：第一次拿到禁用端口、第二次正常，就在第二次起来；每次都拿到禁用端口，在次数上限处抛错，不无限重起。
//
// 用法：node web-preview/tests/smoke-browser-launch.mjs [仓库根]

import fs from "node:fs";
import path from "node:path";
import { createRun } from "./lib/harness.mjs";
import { FETCH_BAD_PORTS, LAUNCH_POLICY, isFetchBadPort, launchOnce, withLaunchRetry } from "./lib/browser.mjs";

const run = createRun("浏览器调试端口：禁用端口判定与重试");

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

// ---- 3. launchOnce：用假浏览器写出指定端口 ----
/** 假的 spawn：从参数里取 --user-data-dir，往里写 DevToolsActivePort；不起任何进程。 */
function fakeSpawn(port) {
    const seen = [];
    const spawnProc = (_exe, args) => {
        const profile = args.find((a) => a.startsWith("--user-data-dir=")).slice("--user-data-dir=".length);
        seen.push(profile);
        fs.writeFileSync(path.join(profile, "DevToolsActivePort"), `${port}\n/devtools/browser/fake\n`);
        return { pid: undefined, once() {}, kill() {} };
    };
    return { spawnProc, seen };
}

/** 抓标准错误：launchOnce 把作废原因写到 console.error，测试里收下来，免得它成了 run-all 显示的最后一行。 */
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

{
    const fake = fakeSpawn(6000);
    const { result, lines } = await withCapturedStderr(() =>
        launchOnce("fake-browser", 2000, { spawnProc: fake.spawnProc }),
    );
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
    const fake = fakeSpawn(40123);
    const { result } = await withCapturedStderr(() => launchOnce("fake-browser", 2000, { spawnProc: fake.spawnProc }));
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
        const fake = fakeSpawn(ports[used++]);
        const r = await launchOnce("fake-browser", 2000, { spawnProc: fake.spawnProc });
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
        const fake = fakeSpawn(6000);
        const r = await launchOnce("fake-browser", 2000, { spawnProc: fake.spawnProc });
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

run.finish();
