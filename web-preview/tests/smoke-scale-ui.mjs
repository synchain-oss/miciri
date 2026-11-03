// SPDX-License-Identifier: GPL-3.0-or-later
//
// 缩放确认回退的状态机（10 秒）：用假定时器逐秒推进，不等真实时间。
// 覆盖：超时回到选档前的档位；确认态里再选档只重置倒计时、回退目标不变；保存后不再回退；
// 取消回到选档前；预览被拒时退出确认态；页面卸载只停计时、不发桥调用。
//
// 用法：node web-preview/tests/smoke-scale-ui.mjs [仓库根]

import path from "node:path";
import { pathToFileURL } from "node:url";
import { createRun, repoRoot } from "./lib/harness.mjs";

const ROOT = repoRoot();
const run = createRun("缩放确认回退");
const { createScaleUi, CONFIRM_SECONDS } = await import(pathToFileURL(path.join(ROOT, "web/shared/scale-ui.js")).href);

run.eq(CONFIRM_SECONDS, 10, "确认倒计时应为 10 秒");

/** 假定时器 + 假桥：返回 ui、桥调用记录、推进一秒的函数。 */
function rig({ current = 1, reject = false } = {}) {
    const calls = [];
    let tick = null;
    const changes = [];
    let scale = current;
    const bridge = {
        setUiScale: async (f) => {
            calls.push(["setUiScale", f]);
            if (reject) return { ok: false, reason: "scale-locked" };
            scale = f;
            return { ok: true };
        },
        commitUiScale: async () => {
            calls.push(["commitUiScale"]);
            return { ok: true };
        },
    };
    const timers = {
        setInterval: (fn) => {
            tick = fn;
            return 1;
        },
        clearInterval: () => {
            tick = null;
        },
    };
    const ui = createScaleUi({
        role: "shell",
        bridge,
        getScale: () => scale,
        timers,
        onChange: (s) => changes.push(s),
    });
    const second = async () => {
        if (tick) tick();
        await Promise.resolve();
        await Promise.resolve();
    };
    return { ui, calls, changes, second, hasTimer: () => tick !== null };
}

// 超时回退
{
    const r = rig();
    run.eq(await r.ui.preview(0.8), true, "选档应生效");
    run.eq([r.ui.state().confirming, r.ui.state().left], [true, 10], "选档后进入确认态，倒计时 10");
    for (let i = 0; i < 9; i++) await r.second();
    run.eq([r.ui.state().confirming, r.ui.state().left], [true, 1], "过 9 秒还剩 1 秒，仍在确认态");
    await r.second();
    run.eq(r.ui.state().confirming, false, "第 10 秒到点应退出确认态");
    run.eq(r.calls.at(-1), ["setUiScale", 1], "超时应回到选档前的档位");
    run.eq(r.hasTimer(), false, "回退后计时器应停");
}

// 确认态里再选档：只重置倒计时，回退目标不变
{
    const r = rig();
    await r.ui.preview(0.8);
    for (let i = 0; i < 4; i++) await r.second();
    run.eq(r.ui.state().left, 6, "过 4 秒还剩 6 秒");
    await r.ui.preview(0.65);
    run.eq([r.ui.state().left, r.ui.state().prev], [10, 1], "再选档应重置倒计时，回退目标仍是 1");
    await r.ui.revert();
    run.eq(r.calls.at(-1), ["setUiScale", 1], "取消应回到最初的档位，而不是中间那一档");
}

// 保存：提交，之后不再回退
{
    const r = rig();
    await r.ui.preview(0.8);
    await r.ui.keep();
    run.eq(r.calls.at(-1), ["commitUiScale"], "保存应调 commitUiScale");
    run.eq(r.ui.state().confirming, false, "保存后退出确认态");
    for (let i = 0; i < 12; i++) await r.second();
    run.eq(r.calls.filter((c) => c[0] === "setUiScale").length, 1, "保存之后不应再有回退的桥调用");
}

// 预览被拒：退出确认态，不起倒计时
{
    const r = rig({ reject: true });
    run.eq(await r.ui.preview(0.8), false, "被拒的预览应返回 false");
    run.eq([r.ui.state().confirming, r.hasTimer()], [false, false], "被拒后不应进入确认态、不应起计时器");
}

// 页面卸载：只停计时，不发桥调用
{
    const r = rig();
    await r.ui.preview(0.8);
    const before = r.calls.length;
    r.ui.pagehide();
    run.eq([r.hasTimer(), r.calls.length], [false, before], "pagehide 应只停计时，不发桥调用");
}

// 选项：当前屏幕放不下的置灰并带提示键
{
    const r = rig();
    const opts = createScaleUi({
        role: "shell",
        bridge: {},
        getScreen: () => ({ availWidth: 1200, availHeight: 800 }),
    }).options();
    run.eq(
        opts.map((o) => o.factor),
        [0.5, 0.65, 0.8, 1, 1.25, 1.5, 2],
        "选项应是全部 7 档",
    );
    run.eq(
        opts.filter((o) => o.disabled).map((o) => o.factor),
        [1.25, 1.5, 2],
        "1200×800 的屏幕上 1.25 及以上应置灰",
    );
    run.check(
        opts.filter((o) => o.disabled).every((o) => o.titleKey === "common.scale.overflow"),
        "置灰项应带「超出当前屏幕」的词条键",
    );
    run.check(
        opts.filter((o) => !o.disabled).every((o) => o.titleKey === null),
        "可选项不带提示键",
    );
    void r;
}

run.finish();
