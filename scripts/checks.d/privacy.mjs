// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from synchain-oss/rinne; see upstream/ci.json
//
// 隐私门禁：工作树全量扫描（通用规则 + 个人针）。自测用规则表里的夹具。

import { formatFinding, scanTree, selfTest } from "../check-privacy.mjs";
import { loadPersonalNeedles } from "../lib/privacy-needles.mjs";

export default {
    name: "privacy",
    group: "compliance",
    inputs: [],
    run(ctx) {
        const { needles, notes } = loadPersonalNeedles({ env: ctx.env, cwd: ctx.root, mode: ctx.needles });
        const hits = scanTree(ctx.root, needles);
        return { ok: hits.length === 0, lines: [...notes, ...hits.map(formatFinding)] };
    },
    selfTest: () => selfTest(),
};
