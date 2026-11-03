// SPDX-License-Identifier: GPL-3.0-or-later
//
// 给人用的预览服务：站点根是仓库根，只监听 127.0.0.1。
//   node web-preview/serve.mjs [端口]      缺省 8833；被占用就报错退出，不自动换端口
// 然后打开 http://127.0.0.1:8833/web-preview/index.html（可加 ?lang=en&scale=0.8）。

import { fileURLToPath } from "node:url";
import path from "node:path";
import { startStaticServer } from "./tests/lib/static-server.mjs";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const port = Number(process.argv[2] || 8833);

try {
    const server = await startStaticServer(root, { port });
    console.log(`预览：${server.url}/web-preview/index.html`);
} catch (e) {
    console.error(`端口 ${port} 不可用：${e.message}`);
    process.exit(1);
}
