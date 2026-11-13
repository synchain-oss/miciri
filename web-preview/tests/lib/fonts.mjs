// SPDX-License-Identifier: GPL-3.0-or-later
//
// 页面冒烟共用：核对四个离线字体在页面里真的加载成功。
// 字体文件缺失或文件名写错时，浏览器只在控制台报一条 404，页面会悄悄回退到系统字体、看起来照常，
// 所以不能只靠「没有页面错误」；这里直接问 document.fonts，每个族都要有字面且状态是 loaded。

/** 页面里应当存在的字族，以及一段能触发它加载的样本文字（汉字族要用汉字）。 */
export const FONT_SAMPLES = Object.freeze([
    { family: "Space Grotesk", sample: "Aa0" },
    { family: "Miciri Sans", sample: "Aa0" },
    { family: "Miciri Mono", sample: "Aa0" },
    { family: "Noto Sans SC", sample: "汉" },
]);

/**
 * 在页面里逐族触发加载并读回 document.fonts 里每个字面的族名与状态。
 * @param {{ eval: (js: string) => Promise<any> }} page 无头页面
 * @param {string} docExpr 取得目标 document 的表达式，例如 "document" 或框架里的 contentDocument
 * @returns {Promise<{ family: string, status: string }[]>}
 */
export function readFontFaces(page, docExpr) {
    const samples = JSON.stringify(FONT_SAMPLES);
    return page.eval(`(async () => {
        const d = ${docExpr};
        for (const { family, sample } of ${samples}) {
            // 加载失败时 load() 会拒绝；失败的事实由下面读到的 status 给出，这里不让它中断整个读取
            await d.fonts.load('16px "' + family + '"', sample).catch(() => {});
        }
        await d.fonts.ready; // 还有字面在加载时先等它落定，再读状态
        return [...d.fonts].map((f) => ({ family: f.family.replace(/^["']|["']$/g, ""), status: f.status }));
    })()`);
}

/**
 * 判读：每个应有的族都至少有一个字面，且它的所有字面都是 loaded。返回问题列表，空表示全部正常。
 * @param {{ family: string, status: string }[]} faces readFontFaces 的结果
 * @param {string[]} [families] 应有的族名，缺省取 FONT_SAMPLES
 */
export function fontProblems(faces, families = FONT_SAMPLES.map((s) => s.family)) {
    const problems = [];
    for (const family of families) {
        const mine = faces.filter((f) => f.family === family);
        if (mine.length === 0) problems.push(`${family}：页面里没有这个字面（@font-face 缺失或族名不对）`);
        for (const f of mine) if (f.status !== "loaded") problems.push(`${family}：状态是 ${f.status}，不是 loaded`);
    }
    return problems;
}

/** 在页面里核对四个字体都已加载，结果记进 run。label 写进断言说明，便于区分是哪个页面。 */
export async function checkFontsLoaded(run, page, docExpr, label) {
    const faces = await readFontFaces(page, docExpr);
    run.eq(fontProblems(faces), [], `${label}：四个离线字体都应已加载（document.fonts）`);
}
