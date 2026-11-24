// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from synchain-oss/rinne; see upstream/web.json
//
// boot-signal.js：页面启动信号。普通脚本（不是模块），在入口页 head 里排在所有样式表与模块之前加载。
//
// 给原生宿主发三种上行信号，宿主据此决定什么时候揭开窗口、什么时候判定启动失败，并把各阶段的毫秒数写进兜底面板的诊断行：
//   __miciri__bootStage  {stage}            "script"：本脚本开始运行；"ready"：首个快照已应用（启动完成）。
//   __miciri__firstFrame {paintDeltaMs?}    外壳已经画出来：宿主据此把挪走的 WebView 放回可视区（开窗遮挡闸）。
//   __miciri__bootError  {stage, detail}    启动失败：宿主立刻换兜底面板，不用干等看门狗超时。
// 事件名与原生侧 WebViewHost.h 的三个常量逐字相同，改名要在同一个提交里一起改。
//
// 通道是 JUCE 在文档创建前注入的 window.__JUCE__.postMessage，不经 bridge.js：桥本身起不来时这里也得发得出去。
// 浏览器预览里没有 __JUCE__，三种信号都静默不发。
//
// 为什么必须是 head 里的普通脚本：模块自身加载或解析失败时，模块里的 try/catch 一行都跑不到，
// 只有先注册好的全局监听接得住；资源级失败（script、link 取不到）不冒泡，只有捕获阶段收得到。
//
// 页面模块经 window.__miciriBoot 用三个入口：
//   shellDrawn()       外壳画好了，两拍之后发首帧信号；
//   ready()            首个快照已应用，发 "ready"，此后运行期的错误不再当成启动失败上报；
//   fail(stage, err)   启动链路自己接住的错误（比如桥调用失败）也报给宿主。
(function () {
    "use strict";

    var EVENT_STAGE = "__miciri__bootStage";
    var EVENT_FRAME = "__miciri__firstFrame";
    var EVENT_ERROR = "__miciri__bootError";
    // 外壳画出来以后两拍迟迟等不到（合成停摆）时首帧信号的保险：宿主还有自己的超时兜底，这里只是让它少等一会儿。
    var FRAME_GUARD_MS = 2500;
    var DETAIL_MAX = 300;

    var booted = false;
    var errorSent = false;
    var frameSent = false;
    var frameArmed = false;
    var paintStartMs = -1;

    // 发一条信号；发不出去（没有宿主、宿主抛错）返回 false，调用方决定要不要再试。
    function post(eventId, payload) {
        try {
            var juce = window.__JUCE__;
            if (!juce || typeof juce.postMessage !== "function") return false;
            juce.postMessage(JSON.stringify({ eventId: eventId, payload: payload }));
            return true;
        } catch (e) {
            return false;
        }
    }

    // 启动失败只报第一条：后面多半是同一次失败的连锁反应。启动完成之后的运行期错误归界面自己处理。
    function fail(stage, err) {
        if (errorSent || booted) return;
        var detail = err;
        if (err && typeof err === "object") {
            // 桥的错误把底层原因挂在 cause 上，诊断行只收 ASCII，底层浏览器报的那句更有用
            var cause = err.cause && err.cause.message ? err.cause.message : "";
            detail = cause || err.stack || err.message || String(err);
        }
        if (post(EVENT_ERROR, { stage: String(stage), detail: String(detail).slice(0, DETAIL_MAX) })) errorSent = true;
    }

    function sendFrame() {
        if (frameSent) return;
        var p = {};
        if (paintStartMs >= 0) p.paintDeltaMs = Math.round(performance.now() - paintStartMs);
        // 真发出去才算数：发不出去时保险定时器到点还会再试一次
        if (post(EVENT_FRAME, p)) frameSent = true;
    }

    function shellDrawn() {
        if (frameArmed) return;
        frameArmed = true;
        // 保险不绕两拍：合成停摆（窗口最小化、被完全遮挡）时 requestAnimationFrame 也不回调。
        // 保险只在外壳画出来之后才上：外壳没画出来就不发首帧，诊断里的 frame 不会冒充“外壳已画出”，
        // 揭开窗口交给原生侧自己的超时。
        setTimeout(sendFrame, FRAME_GUARD_MS);
        if (typeof window.requestAnimationFrame !== "function") {
            sendFrame();
            return;
        }
        // 两拍：外壳的那一帧真正提交给合成器之后再让宿主揭开窗口
        window.requestAnimationFrame(function () {
            window.requestAnimationFrame(sendFrame);
        });
    }

    function ready() {
        if (booted) return;
        booted = true;
        post(EVENT_STAGE, { stage: "ready" });
    }

    window.addEventListener(
        "error",
        function (e) {
            // 浏览器的 “ResizeObserver loop …” 是良性通知，不是启动失败（外壳的缩放适配用了 ResizeObserver）
            if (e && !e.error && typeof e.message === "string" && e.message.indexOf("ResizeObserver loop") === 0)
                return;
            var t = e && e.target;
            if (t && t !== window && (t.src || t.href)) {
                // 只认脚本和样式表（rel="stylesheet"）：图片、图标、preload 之类与启动无关的失败不该把整个界面换成兜底面板
                var sheet = t.tagName === "LINK" && /(^|\s)stylesheet(\s|$)/i.test(t.rel || "");
                if (t.tagName === "SCRIPT" || sheet) fail("resource", t.src || t.href);
                return;
            }
            fail("script", (e && (e.error || e.message)) || "unknown error");
        },
        true,
    );
    window.addEventListener("unhandledrejection", function (e) {
        fail("promise", (e && e.reason) || "unhandled rejection");
    });

    // 只记录首次绘制的时刻（诊断用），不拿它触发首帧信号：首次绘制可能只是底色，外壳还没出来
    try {
        var po = new PerformanceObserver(function (list) {
            var entries = list.getEntries();
            if (entries.length === 0) return;
            paintStartMs = entries[0].startTime;
            try {
                po.disconnect();
            } catch (e) {
                // 断开失败无关紧要
            }
        });
        po.observe({ type: "paint", buffered: true });
    } catch (e) {
        // 没有 PerformanceObserver：诊断里少一个差值，不影响信号
    }

    window.__miciriBoot = { shellDrawn: shellDrawn, ready: ready, fail: fail };
    post(EVENT_STAGE, { stage: "script" });
})();
