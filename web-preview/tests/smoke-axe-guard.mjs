// SPDX-License-Identifier: GPL-3.0-or-later
//
// axe 冒烟的缺包行为（不需要浏览器，也不需要装 playwright 与 axe-core）：依赖目录是个空目录时，
//   不设 MICIRI_REQUIRE_AXE：smoke-axe-page 以 2 退出（跳过），输出写明没有安装；
//   MICIRI_REQUIRE_AXE=1（CI 的设置）：以 1 退出（失败），输出写明「要求 axe 必须运行」。
// 证明 CI 里的 axe 不会因为安装步骤被删掉而悄悄退回跳过。
//
// 用法：node web-preview/tests/smoke-axe-guard.mjs [仓库根]

import fs from "node:fs";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { createRun, repoRoot, EXIT_FAIL, EXIT_SKIP } from "./lib/harness.mjs";
import { makeTempDir, removeTempDir } from "../../scripts/lib/safe-rm.mjs";

const ROOT = repoRoot();
const run = createRun("axe 缺包行为");
const target = path.join(path.dirname(fileURLToPath(import.meta.url)), "smoke-axe-page.mjs");
run.check(fs.existsSync(target), "smoke-axe-page.mjs 应存在");

const empty = makeTempDir("miciri-axe-guard-");
try {
    const probe = (extraEnv) => {
        const env = { ...process.env, MICIRI_AXE_DEPS: empty };
        delete env.MICIRI_REQUIRE_AXE;
        Object.assign(env, extraEnv);
        const r = spawnSync(process.execPath, [target, ROOT], { encoding: "utf8", env, timeout: 60000 });
        return { status: r.status, out: `${r.stdout || ""}${r.stderr || ""}` };
    };

    const soft = probe({});
    run.eq(soft.status, EXIT_SKIP, "没装依赖且不要求 axe：应整套跳过（退出码 2）");
    run.check(/没有安装 playwright、axe-core/.test(soft.out), "跳过时应写明缺 playwright 与 axe-core");

    const hard = probe({ MICIRI_REQUIRE_AXE: "1" });
    run.eq(hard.status, EXIT_FAIL, "没装依赖但要求 axe：应失败（退出码 1），不能退回跳过");
    run.check(/MICIRI_REQUIRE_AXE=1 要求 axe 必须运行/.test(hard.out), "失败时应写明是因为要求 axe 必须运行");
} finally {
    const refused = removeTempDir(empty);
    run.check(refused === null, `临时目录应能清理（${refused}）`);
}
run.finish();
