// SPDX-License-Identifier: GPL-3.0-or-later
//
// 预览用的假原生后端：window.__JUCE__.backend 的同形替身，不依赖插件。
// 每个桥函数都按 docs/contracts/BRIDGE.md 的回执规则实现（{ok:true} 或 {ok:false, reason}），
// 事件经 addEventListener 登记、由 emit() 推送。只模拟桥的语义，不模拟任何测试或算法：
// 步骤相关的函数只校验参数并记录，后面的页面需要时再扩展，名字与回执形状不变。
//
// 窗口缩放按「先设置、确认才提交」实现：setUiScale 立刻改窗口（onResize）并推快照；档位与已提交的不同时记为待确认，
// commitUiScale 提交，没有待确认项回 no-pending；setUiScale 回到已提交的档位时待确认项自动消失（回退走的就是这条）。
// calls 记录全部调用，供冒烟断言；它不属于桥契约。

import {
    BRIDGE_EVENTS,
    BRIDGE_FUNCTIONS,
    BRIDGE_REASONS,
    ENUM_CHANGE_KIND,
    ENUM_MIC_TYPE,
    ENUM_STEP_ID,
} from "../../web/shared/bridge-api.js";
import { DESIGN } from "../../web/shared/design-box.js";
import { LANGS } from "../../web/shared/i18n.js";
import { buildSnapshot } from "./snapshot.js";

const OK = Object.freeze({ ok: true });
const fail = (reason) => {
    if (!BRIDGE_REASONS.includes(reason)) throw new Error(`mock：回执原因 ${reason} 不在 BRIDGE_REASONS 里`);
    return { ok: false, reason };
};

/**
 * @param {object} [o]
 * @param {string} [o.lang]
 * @param {number} [o.uiScale] 开窗时的档位
 * @param {(f:number)=>void} [o.onResize] 宿主改窗口尺寸的回调：参数是档位
 * @param {boolean} [o.scaleLocked] 为 true 时 setUiScale 回 scale-locked
 */
export function createMockBackend(o = {}) {
    const snap = buildSnapshot({ lang: o.lang, uiScale: o.uiScale });
    const onResize = typeof o.onResize === "function" ? o.onResize : () => {};
    const listeners = new Map();
    const calls = [];
    let committed = snap.uiScale;
    let pending = false;

    const emit = (name, payload) => {
        if (!BRIDGE_EVENTS.includes(name)) throw new Error(`mock：事件 ${name} 不在 BRIDGE_EVENTS 里`);
        for (const cb of listeners.get(name) || []) cb(payload);
    };
    const pushState = () => emit("miciri.state", structuredClone(snap));

    const impl = {
        requestInitialState: () => structuredClone(snap),
        setLang(lang) {
            if (!LANGS.includes(lang)) return fail("bad-lang");
            snap.lang = lang;
            pushState();
            return OK;
        },
        setUiScale(f) {
            if (o.scaleLocked) return fail("scale-locked");
            if (!DESIGN.shell.presets.includes(f)) return fail("bad-scale");
            snap.uiScale = f;
            pending = f !== committed;
            onResize(f);
            pushState();
            return OK;
        },
        commitUiScale() {
            if (!pending) return fail("no-pending");
            committed = snap.uiScale;
            pending = false;
            return OK;
        },
        setGuideSeen(seen) {
            if (typeof seen !== "boolean") return fail("bad-arg");
            snap.guideSeen = seen;
            pushState();
            return OK;
        },
        setMicType(type) {
            if (!ENUM_MIC_TYPE.includes(type)) return fail("bad-arg");
            snap.micType = type;
            pushState();
            return OK;
        },
        startStep: (id) => (ENUM_STEP_ID.includes(id) ? OK : fail("bad-step")),
        cancelStep: () => OK,
        reportChange: (kind) => (ENUM_CHANGE_KIND.includes(kind) ? OK : fail("bad-arg")),
        dismissChannelPrompt: () => OK,
        openExternal: (link) => (typeof link === "string" ? OK : fail("bad-arg")),
        showLicense: () => OK,
        clearClipLatch: () => OK,
    };

    const backend = {
        addEventListener(name, cb) {
            if (!listeners.has(name)) listeners.set(name, []);
            listeners.get(name).push(cb);
        },
        emit,
        calls,
        committedScale: () => committed,
        hasPending: () => pending,
    };
    for (const name of BRIDGE_FUNCTIONS) {
        if (typeof impl[name] !== "function") throw new Error(`mock：桥函数 ${name} 没有实现`);
        backend[name] = async (...args) => {
            calls.push({ name, args });
            return impl[name](...args);
        };
    }
    return backend;
}
