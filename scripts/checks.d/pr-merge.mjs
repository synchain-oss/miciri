// SPDX-License-Identifier: GPL-3.0-or-later
//
// 合并前检查 v0（scripts/maintainer/pr-merge.mjs）的自测接进注册表。脚本本身只读 GitHub，不在 CI 上跑。

import { selfTest } from "../maintainer/pr-merge.mjs";

export default {
    name: "pr-merge",
    group: "pr",
    inputs: [],
    run: () => ({ skip: "合并前检查针对具体 PR，在维护者本机运行" }),
    selfTest,
};
