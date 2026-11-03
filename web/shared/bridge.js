// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from synchain-oss/rinne; see upstream/web.json
//
// bridge.js：前端桥，界面与原生之间唯一的传输层。
//
// 职责：把 bridge-api.js 里登记的名字原样接到两种后端上，别的什么都不做。
//   - 上行：对 BRIDGE_FUNCTIONS 的每个名字生成一个 async 方法，参数原样透传；
//   - 下行：对 BRIDGE_EVENTS 的每个名字挂监听，统一经 on(evt, cb) 分发。
// 页面只依赖本文件导出的 createBridge()，不直接碰 window.__JUCE__，也不自建事件通道。
//
// 后端选择：
//   - 检测到真 JUCE 宿主时用 JuceBackend：惰性 import("../js/juce/index.js") 后取 getNativeFunction(name)；
//     必须惰性，因为 JUCE 的辅助脚本在模块顶层就访问 window，静态 import 会让本文件在没有 window 的环境里报错。
//   - 否则用注入的 mock 后端：优先取 opts.mockBackend，其次全局 window.__MICIRI_MOCK__，最后是同源父窗口的
//     同名全局（预览页用 iframe 装载页面，父窗口先把 mock 放好，页面求值时一定读得到，不存在时序问题）。
//     读父窗口属性时跨源会抛错，一律当作没有。本文件不含任何假数据实现；两者都没有就抛错并给出指引。
//   - 真宿主的判据是 isJuceHost()：backend 存在，且 initialisationData.__juce__functions 是非空数组。
//     JUCE 的辅助脚本在普通浏览器里会自己造一个占位的 window.__JUCE__，只看「__JUCE__ 存在」会把预览页
//     误判成真宿主，所有调用永远挂起、页面白屏且没有报错。占位对象加已注入的 mock 时走 mock，并留一条警告。
//
// 只做传输，不复制契约语义：不校验参数、不做夹取（越界或类型不符由原生侧夹取或拒绝）、不代调
// requestInitialState()、不缓存快照、不重放事件、不做节流与 diff。载荷与枚举见 docs/contracts/BRIDGE.md。
//
// mock 后端的形状：window.__JUCE__.backend 的同形替身，
//   - BRIDGE_FUNCTIONS 的每个名字一个方法（可以是 async，返回 JSON 对象）；
//   - addEventListener(evtName, cb) 一个方法，用于推送 BRIDGE_EVENTS 的事件。
// 缺名会在 createBridge() 处抛错并列出缺失项。

import { BRIDGE_EVENTS, BRIDGE_FUNCTIONS } from "./bridge-api.js";

/** 返回对象上的保留键：函数名不得与它们重名。 */
const RESERVED_KEYS = new Set(["isPreview", "on"]);

/** 极简事件分发器。同一事件的回调按注册顺序同步调用；派发前取快照，允许回调内退订。 */
function makeEmitter() {
    const map = new Map();
    return {
        on(evt, cb) {
            if (typeof cb !== "function") throw new TypeError("bridge.on(evt, cb)：cb 必须是函数");
            let list = map.get(evt);
            if (!list) {
                list = [];
                map.set(evt, list);
            }
            list.push(cb);
            return function off() {
                const i = list.indexOf(cb);
                if (i >= 0) list.splice(i, 1);
            };
        },
        emit(evt, payload) {
            const list = map.get(evt);
            if (!list) return;
            for (const cb of list.slice()) cb(payload);
        },
    };
}

/** 事件接线，两种后端共用：后端只需实现 addEventListener(name, cb)。 */
function wireEvents(backend, em) {
    for (const name of BRIDGE_EVENTS) backend.addEventListener(name, (payload) => em.emit(name, payload));
}

/** 订阅口：事件名不在名表内就抛错（拼错事件名比收不到事件更难查）。 */
function makeOn(em) {
    const allowed = new Set(BRIDGE_EVENTS);
    return function on(evt, cb) {
        if (!allowed.has(evt)) {
            throw new RangeError(
                `bridge.on：事件名 ${JSON.stringify(evt)} 不在名表内；可用事件：${BRIDGE_EVENTS.join(", ")}`,
            );
        }
        return em.on(evt, cb);
    };
}

/**
 * 组装返回对象 { isPreview, on, ...每个函数一个方法 }。
 * 名表里有而后端尚未实现的名字不挂：挂了会让页面的 typeof bridge.X === "function" 特性探测判成「能用」，
 * 按钮点亮却什么也不做，比「桥上没有这个名字」更坏。名表与实现之间的时间差由此如实暴露。
 */
function assemble(isPreview, em, call, isRegistered) {
    const api = { isPreview, on: makeOn(em) };
    const absent = [];
    for (const name of BRIDGE_FUNCTIONS) {
        if (RESERVED_KEYS.has(name)) throw new Error(`bridge：函数名 ${name} 与返回对象的保留键冲突`);
        if (!isRegistered(name)) {
            absent.push(name);
            continue;
        }
        api[name] = call(name);
    }
    if (absent.length > 0) {
        console.warn(
            `[bridge] 名表里有而宿主未登记的函数 ${absent.length} 个：${absent.join(", ")}，这些名字不会挂到桥上。`,
        );
    }
    return api;
}

/** 是否为真 JUCE 宿主（而不是辅助脚本造的占位对象，详见文件头）。 */
function isJuceHost(juce) {
    if (!juce || !juce.backend) return false;
    const fns = juce.initialisationData ? juce.initialisationData.__juce__functions : undefined;
    return Array.isArray(fns) && fns.length > 0;
}

/**
 * JUCE 前端辅助脚本（封装 native 函数调用）的默认位置，相对本文件。它不在本仓的 web/ 里写：
 * 由插件在打包页面资源时放进来（同一路径），所以页面代码只认这个固定位置。
 */
const JUCE_HELPER = "../js/juce/index.js";

function makeJuceBackend(helperPath) {
    const em = makeEmitter();
    wireEvents(window.__JUCE__.backend, em);
    // 第一次调用时才加载；失败不缓存（补上文件后重试能恢复），并换成说得清原因的错误：
    // 裸的模块解析错误只会让每个桥调用都以「Failed to fetch」之类的话失败，查不到是哪个文件缺了。
    let helper = null;
    const loadHelper = () => {
        if (!helper) {
            helper = import(helperPath).catch((cause) => {
                helper = null;
                throw new Error(
                    `bridge：加载不了 JUCE 前端辅助脚本（${helperPath}），真宿主的桥调用都发不出去。请确认插件已把它打包进页面资源。`,
                    { cause },
                );
            });
        }
        return helper;
    };
    const call =
        (name) =>
        async (...args) => {
            const { getNativeFunction } = await loadHelper();
            return getNativeFunction(name)(...args);
        };
    const registered = new Set((window.__JUCE__.initialisationData || {}).__juce__functions || []);
    return assemble(false, em, call, (name) => registered.has(name));
}

function makeMockBridge(mock) {
    if (typeof mock.addEventListener !== "function") {
        throw new TypeError(
            "createBridge：mock 后端缺少 addEventListener(evtName, cb)，须与 window.__JUCE__.backend 同形",
        );
    }
    const missing = BRIDGE_FUNCTIONS.filter((name) => typeof mock[name] !== "function");
    if (missing.length > 0) {
        throw new TypeError(
            `createBridge：mock 后端缺少 ${missing.length} 个函数（须逐一实现全集）：${missing.join(", ")}`,
        );
    }
    const em = makeEmitter();
    wireEvents(mock, em);
    const call =
        (name) =>
        async (...args) =>
            mock[name](...args);
    return assemble(true, em, call, (name) => typeof mock[name] === "function");
}

/** 取预览页放好的 mock：本窗口优先，其次同源父窗口。跨源或没有都返回 undefined。 */
function findInjectedMock(win) {
    if (!win) return undefined;
    if (win.__MICIRI_MOCK__) return win.__MICIRI_MOCK__;
    try {
        return win.parent && win.parent !== win ? win.parent.__MICIRI_MOCK__ : undefined;
    } catch {
        return undefined;
    }
}

/**
 * 工厂，界面只调这一个。
 * @param {{mockBackend?: object, juceHelper?: string}} [opts] mockBackend 仅在非真 JUCE 宿主时使用；
 *   juceHelper 是真宿主分支加载辅助脚本的路径（相对本文件），缺省为约定位置，只给测试覆盖。
 * @returns {{isPreview: boolean, on: Function}} 另含名表里全部桥函数（均为 async）。
 */
export function createBridge(opts = {}) {
    const win = typeof window !== "undefined" ? window : undefined;
    const juce = win ? win.__JUCE__ : undefined;
    const mockBackend = opts.mockBackend || findInjectedMock(win);
    if (isJuceHost(juce)) return makeJuceBackend(opts.juceHelper || JUCE_HELPER);
    if (mockBackend) {
        if (juce) {
            console.warn(
                "createBridge：window.__JUCE__ 只是辅助脚本的占位对象（未登记任何 native 函数），已按注入的 mock 运行。",
            );
        }
        return makeMockBridge(mockBackend);
    }
    throw new Error(
        "createBridge：未检测到真 JUCE 宿主，且未注入 mock 后端。浏览器预览请经 web-preview 的入口加载；bridge.js 本身不含任何假数据实现。",
    );
}
