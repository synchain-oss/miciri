// SPDX-License-Identifier: GPL-3.0-or-later
//
// 冒烟脚本共用的只读文件遍历：列出若干目录下指定扩展名的文件，可跳过某些相对路径前缀。只读，不改不删。

import fs from "node:fs";
import path from "node:path";

/**
 * @param {string} root 仓库根
 * @param {string[]} dirs 要遍历的相对目录
 * @param {string[]} exts 扩展名（含点）
 * @param {string[]} [skip] 相对仓库根的路径前缀（正斜杠），命中就跳过
 * @returns {string[]} 相对仓库根的路径（正斜杠）
 */
export function listFiles(root, dirs, exts, skip = []) {
    const out = [];
    const walk = (rel) => {
        for (const ent of fs.readdirSync(path.join(root, rel), { withFileTypes: true })) {
            const child = `${rel}/${ent.name}`;
            if (skip.some((s) => child === s || child.startsWith(`${s}/`))) continue;
            if (ent.isDirectory()) walk(child);
            else if (exts.includes(path.extname(ent.name))) out.push(child);
        }
    };
    for (const d of dirs) if (fs.existsSync(path.join(root, d))) walk(d);
    return out.sort();
}

export const readText = (root, rel) => fs.readFileSync(path.join(root, rel), "utf8");
