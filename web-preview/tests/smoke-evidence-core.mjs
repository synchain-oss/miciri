// SPDX-License-Identifier: GPL-3.0-or-later
//
// 证据画布的纯逻辑冒烟（不要浏览器）：
//   ① 缩放：1.00× 到 16×，光标下的时间点缩放前后不动，范围不越界，放大再缩回读数回到 1.00；
//   ② 滚轮累计：鼠标一格一档，触摸板的小增量攒够 100 才走，隔一会儿累计清零，方向对；键盘映射；
//   ③ 时间刻度：小数位随可见范围变化，标签与真实位置对得上，相邻标签不写成同一个数；
//   ④ 回执载荷的核对与归一（形状不对不画，NaN 按 0，未知事件丢掉）；每像素列的包络极值；事件标记的位置与压暗；
//   ⑤ 反例：检查要能变红。
//
// 用法：node web-preview/tests/smoke-evidence-core.mjs [仓库根]

import path from "node:path";
import { pathToFileURL } from "node:url";
import { createRun, repoRoot } from "./lib/harness.mjs";

const ROOT = repoRoot();
const run = createRun("证据画布逻辑");
const at = (rel) => import(pathToFileURL(path.join(ROOT, rel)).href);
const core = await at("web/app/detail/evidence-core.js");
const { synthEvidence } = await at("web-preview/mock/evidence.js");
const { zoomAt, panBy, clampView, rangeOf, zoomText, createWheel, keyView, timeTicks, normalize, columns, markers } =
    core;
const near = (a, b, eps = 1e-9) => Math.abs(a - b) < eps;

// ---- ① 缩放 ----
{
    const v0 = { z: 1, c: 0.5 };
    run.check(near(zoomAt(v0, 1).z, 1.25) && near(zoomAt(v0, 3).z, 1.25 ** 3), "每档 ×1.25");
    run.eq(zoomAt(v0, 40).z, 16, "放大到 16× 为止");
    run.eq(zoomAt(v0, -5), { z: 1, c: 0.5 }, "已经是 1× 再缩小不动");
    // 光标下的时间点缩放前后不动
    for (const p of [0, 0.25, 0.5, 0.8, 1]) {
        let v = { z: 2, c: 0.4 };
        const before = rangeOf(v)[0] + p / v.z;
        v = zoomAt(v, 3, p);
        const after = rangeOf(v)[0] + p / v.z;
        const [a, b] = rangeOf(v);
        run.check(
            near(before, after) || a === 0 || b === 1,
            `缩放以光标（${p}）为中心：光标下的时间点不动，或已贴到边`,
        );
        run.check(a >= -1e-12 && b <= 1 + 1e-12, `缩放后可见范围不越界（光标 ${p}）`);
    }
    run.eq(zoomText(zoomAt(zoomAt(v0, 7, 0.3), -7, 0.3)), "1.00", "放大再缩回：读数回到 1.00");
    run.eq(zoomText(zoomAt(v0, 40)), "16.00", "放到顶：读数 16.00");
    run.eq(zoomText(zoomAt(v0, 1)), "1.25", "读数两位小数");
    run.eq(panBy({ z: 1, c: 0.5 }, 0.3), { z: 1, c: 0.5 }, "没放大时平移不动");
    const p = panBy({ z: 4, c: 0.5 }, 10);
    run.check(near(rangeOf(p)[1], 1), "平移被夹在末端");
    run.check(near(panBy({ z: 4, c: 0.5 }, 0.5).c, 0.5 + 0.5 / 4), "平移一个可见范围的一半");
    const c = clampView({ z: Number.NaN, c: Number.NaN });
    run.eq([c.z, c.c], [1, 0.5], "非有限的视图回到整段");
}

// ---- ② 滚轮与键盘 ----
{
    const w = createWheel();
    run.eq(w.feed(-100, 0, 0), 1, "鼠标一格（向上 100）放大一档");
    run.eq(w.feed(100, 0, 1000), -1, "向下一格缩小一档");
    const pad = createWheel();
    let steps = 0;
    for (let i = 0; i < 9; i++) steps += pad.feed(-10, 0, 100 + i * 10);
    run.eq(steps, 0, "触摸板的小增量（九次共 90）还没攒够一档");
    run.eq(pad.feed(-10, 0, 200), 1, "攒到 100 走一档");
    const idle = createWheel();
    idle.feed(-60, 0, 0);
    run.eq(idle.feed(-60, 0, 400), 0, "隔了一会儿，累计清零，不会把两次 60 加成一档");
    run.eq(createWheel().feed(-250, 0, 0), 2, "一次大增量走多档，余数留着");
    run.eq(createWheel().feed(-4, 1, 0), 1, "按行滚动（deltaMode 1）：四行折算后够一档");
    run.eq(createWheel().feed(Number.NaN, 0, 0), 0, "非有限的增量忽略");
    run.eq(keyView({ z: 1, c: 0.5 }, "+").z, 1.25, "键盘 + 放大");
    run.eq(keyView({ z: 1, c: 0.5 }, "=").z, 1.25, "键盘 = 也放大（+ 不用按 Shift）");
    run.eq(keyView({ z: 2, c: 0.5 }, "-").z, 1.6, "键盘 - 缩小");
    run.check(
        keyView({ z: 2, c: 0.5 }, "ArrowRight").c > 0.5 && keyView({ z: 2, c: 0.5 }, "ArrowLeft").c < 0.5,
        "← → 平移",
    );
    run.eq(keyView({ z: 8, c: 0.3 }, "0"), { z: 1, c: 0.5 }, "键盘 0 复位");
    run.eq(keyView({ z: 1, c: 0.5 }, "a"), null, "别的键交还给浏览器");
}

// ---- ③ 时间刻度 ----
{
    const dur = 14;
    const full = timeTicks(dur, [0, 1]);
    run.eq(full.labels, ["0.0", "3.5", "7.0", "10.5", "14.0"], "整段 14 秒：五个刻度，一位小数");
    const deep = timeTicks(dur, rangeOf({ z: 16, c: 0.5 }));
    run.check(deep.decimals > full.decimals, "放大后小数位增加");
    // 扫遍缩放与位置：标签与真实位置相差不超过刻度间距的 2%，相邻标签互不相同
    let worst = 0;
    let dup = 0;
    for (const d of [1.5, 6, 14, 90]) {
        for (let z = 1; z <= 16; z *= 1.25) {
            for (const cc of [0, 0.13, 0.5, 0.77, 1]) {
                const r = rangeOf(clampView({ z, c: cc }));
                const t = timeTicks(d, r);
                const step = ((r[1] - r[0]) * d) / 4;
                t.values.forEach((x, i) => {
                    worst = Math.max(worst, Math.abs(Number(t.labels[i]) - x) / step);
                });
                if (new Set(t.labels).size !== t.labels.length) dup++;
            }
        }
    }
    run.check(worst <= 0.02 + 1e-9, `标签与真实位置的误差不超过刻度间距的 2%（最大 ${worst.toFixed(4)}）`);
    run.eq(dup, 0, "任何缩放与位置下，相邻刻度标签都不重复");
    // 反例：小数位固定为一位，放大后会出现重复标签，检查能看出来
    const fixed = (d, r) => timeTicks(d, r).values.map((v) => v.toFixed(1));
    const bad = fixed(1.5, rangeOf({ z: 16, c: 0.5 }));
    run.check(new Set(bad).size !== bad.length, "反例：固定一位小数，放大后相邻标签重复（上面的检查应当会抓到它）");
}

// ---- ④ 载荷、包络列、标记 ----
{
    const pack = synthEvidence("position", 1, 6);
    const data = normalize(pack);
    run.check(data !== null && data.min.length === pack.envelope.min.length, "假桥的证据载荷能通过核对");
    run.check(data.max.every((x) => x >= -1 && x <= 1) && data.min.every((x) => x >= -1 && x <= 1), "包络归一到 -1..1");
    run.eq([...new Set(data.events.map((e) => e.kind))].sort(), ["mouth", "plosive", "sibilance"], "三种事件都在");
    const env = (o) => ({ hopSec: 0.5, min: [0, -500], max: [500, 0], ...o });
    const mk = (o) => ({ rev: 1, durationSec: 1, envelope: env(o && o.envelope), events: [], ...o });
    run.eq(normalize(null), null, "没有载荷：不画");
    run.eq(normalize(mk({ durationSec: 0 })), null, "时长为 0：不画");
    run.eq(normalize(mk({ durationSec: Number.NaN })), null, "时长非有限：不画");
    run.eq(normalize(mk({ envelope: env({ hopSec: 0 }) })), null, "格宽为 0：不画");
    run.eq(normalize(mk({ envelope: env({ min: [0] }) })), null, "最小最大不等长：不画");
    run.eq(normalize(mk({ envelope: env({ min: [], max: [] }) })), null, "没有格：不画");
    const odd = normalize(
        mk({
            envelope: env({ min: [-2000, Number.NaN], max: [3000, null] }),
            events: [
                { kind: "plosive", timeSec: 0.2, durSec: 0.01 },
                { kind: "level", timeSec: 0.3, durSec: 0 },
                { kind: "mouth", timeSec: Number.NaN, durSec: 0 },
                { kind: "sibilance", timeSec: -1, durSec: 0 },
                null,
            ],
        }),
    );
    run.eq([...odd.max], [1, 0], "包络超过满刻度夹到 ±1，非有限的格按 0");
    run.eq([...odd.min], [-1, 0], "最小值同样夹住");
    run.eq(
        odd.events.map((e) => e.kind),
        ["plosive"],
        "只留已知种类、时间有限且不为负的事件",
    );

    // 两格：[0,0.5) 与 [0.5,1)，每格 min/max 已知
    const d2 = normalize(mk({ envelope: env({ min: [-100, -800], max: [200, 400] }) }));
    const c1 = columns(d2, [0, 1], 2);
    run.eq(
        [[...c1.lo], [...c1.hi]],
        [[-0.1, -0.8].map(Math.fround), [0.2, 0.4].map(Math.fround)],
        "整段两列：各取一格的最小与最大",
    );
    const c2 = columns(d2, [0, 1], 1);
    run.eq([c2.lo[0], c2.hi[0]], [Math.fround(-0.8), Math.fround(0.4)], "一列盖住两格：取两格的极值");
    const c3 = columns(d2, [0.6, 0.9], 4);
    run.eq([...c3.has], [1, 1, 1, 1], "放大到一格比像素还宽：相邻列取同一格");
    run.eq(
        [...c3.lo].every((x) => x === Math.fround(-0.8)),
        true,
        "放大后取到的是第二格",
    );
    const shortEnv = normalize(mk({ durationSec: 2, envelope: env({}) })); // 两格只盖住 1 秒
    const c4 = columns(shortEnv, [0, 1], 4);
    run.eq([...c4.has], [1, 1, 0, 0], "超出数据末端的列没有数据");

    // 标记：位置、范围过滤、压暗
    const ev = normalize(
        mk({
            durationSec: 10,
            events: [
                { kind: "plosive", timeSec: 1, durSec: 0.04 },
                { kind: "sibilance", timeSec: 5, durSec: 0.1 },
                { kind: "mouth", timeSec: 9, durSec: 0.003 },
            ],
        }),
    );
    const all = markers(ev, ["plosive", "sibilance", "mouth"], "sibilance", [0, 1], 100);
    run.eq(
        all.map((m) => [m.kind, Math.round(m.x), m.dim]),
        [
            ["plosive", 10, true],
            ["sibilance", 50, false],
            ["mouth", 90, true],
        ],
        "标记的横坐标与压暗：只有当前类不暗",
    );
    run.eq(
        markers(ev, ["plosive", "sibilance", "mouth"], null, [0, 1], 100).some((m) => m.dim),
        false,
        "没有当前类时都不压暗",
    );
    run.eq(
        markers(ev, ["plosive", "sibilance", "mouth"], "mouth", [0.4, 0.6], 200).map((m) => [m.kind, Math.round(m.x)]),
        [["sibilance", 100]],
        "放大后只留可见范围内的标记，横坐标按范围重算",
    );
    run.eq(markers(ev, [], null, [0, 1], 100), [], "kinds 为空（增益）：不画事件");
    // 反例：压暗写反，上面的断言会变红
    run.check(all.find((m) => m.kind === "sibilance").dim === false, "正例：当前类不压暗");
}

run.finish();
