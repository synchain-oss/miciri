// SPDX-License-Identifier: GPL-3.0-or-later
//
// 按 thresholds.json 重新生成 docs/ALGORITHMS.md 里的阈值表（两行标记之间的部分，其余手写的文字不动）。
// 改了 thresholds.json 就跑一遍，把结果和改动一起提交：
//   node scripts/gen-algorithms.mjs            写回文档
//   node scripts/gen-algorithms.mjs --check    只核对，文档里的表过期了就退出码 1（检查 algorithms-doc 做的是同一件事）

import fs from "node:fs";
import path from "node:path";
import { ROOT, assertNodeVersion, isMain } from "./lib/check-kit.mjs";
import { DOC_FILE, extractBlock, renderThresholds, replaceBlock } from "./lib/algorithms-doc.mjs";

function main(argv) {
    assertNodeVersion();
    const json = JSON.parse(fs.readFileSync(path.join(ROOT, "thresholds.json"), "utf8"));
    const file = path.join(ROOT, DOC_FILE);
    const doc = fs.readFileSync(file, "utf8");
    const block = renderThresholds(json);
    if (argv.includes("--check")) {
        const fresh = extractBlock(doc) === block;
        console.log(
            fresh ? `${DOC_FILE}：阈值表是最新的` : `${DOC_FILE}：阈值表过期了，运行 node scripts/gen-algorithms.mjs`,
        );
        return fresh ? 0 : 1;
    }
    const next = replaceBlock(doc, block);
    if (next === doc) console.log(`${DOC_FILE}：没有变化`);
    else {
        fs.writeFileSync(file, next);
        console.log(`${DOC_FILE}：已按 thresholds.json 更新阈值表`);
    }
    return 0;
}

if (isMain(import.meta.url)) process.exit(main(process.argv.slice(2)));
