// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from synchain-oss/rinne; see upstream/ci.json
//
// 两个 git hook 的自测（含经真实 sh 外壳的端到端用例）。hook 本身在本机 commit / push 时运行，
// 这里只把自测接进注册表。CI 上找不到 gitleaks 时 pre-push 的自测判红，不跳过。

import { selfTest as commitMsg } from "../hooks/commit-msg.mjs";
import { selfTest as prePush } from "../hooks/pre-push.mjs";
import { ROOT, caseResult, git } from "../lib/check-kit.mjs";

/** 必须带可执行位（git 里是 100755）的文件：hook 没有可执行位时 Linux 上的 git 根本不运行它，端到端用例全部变成「没拦住」。 */
export const EXECUTABLE = ["scripts/hooks/commit-msg", "scripts/hooks/pre-push", "scripts/ci/mac.sh"];

/** lsFiles 是 git ls-files -s 的输出。返回问题列表。 */
export function modeProblems(lsFiles) {
    const modes = new Map();
    for (const l of lsFiles.split(/\r?\n/)) {
        const m = /^(\d+) [0-9a-f]+ \d\t(.+)$/.exec(l);
        if (m) modes.set(m[2], m[1]);
    }
    return EXECUTABLE.flatMap((f) =>
        modes.get(f) === "100755"
            ? []
            : [`${f}：git 里的模式是 ${modes.get(f) ?? "（没有）"}，要 100755（git update-index --chmod=+x）`],
    );
}

export default {
    name: "hooks",
    group: "compliance",
    inputs: [],
    run(ctx) {
        const errors = modeProblems(git(["ls-files", "-s", "--", ...EXECUTABLE], { cwd: ctx.root ?? ROOT }));
        return { ok: errors.length === 0, lines: errors };
    },
    selfTest() {
        const [a, b] = [commitMsg(), prePush()];
        const ls = (mode) =>
            EXECUTABLE.map((f, i) => `${i === 0 ? mode : "100755"} ${"a".repeat(40)} 0\t${f}`).join("\n");
        const c = caseResult([
            { label: "模式齐全", bad: false, passed: modeProblems(ls("100755")).length === 0 },
            { label: "hook 只有 100644", bad: true, passed: modeProblems(ls("100644")).length === 1 },
            { label: "文件不在索引里", bad: true, passed: modeProblems("").length === EXECUTABLE.length },
        ]);
        return {
            ok: a.ok && b.ok && c.ok,
            bad: a.bad + b.bad + c.bad,
            good: a.good + b.good + c.good,
            lines: [...a.lines, ...b.lines, ...c.lines],
        };
    },
};
