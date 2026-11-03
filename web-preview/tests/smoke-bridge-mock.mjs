// SPDX-License-Identifier: GPL-3.0-or-later
//
// 假桥与前端桥：mock 的快照形状必须与契约字段表逐项一致（字段与顺序）、回执按契约规则、
// createBridge 对缺名与拼错事件名要抛错。契约追加字段而 mock 没跟上时，这里先红。
//
// 用法：node web-preview/tests/smoke-bridge-mock.mjs [仓库根]

import path from "node:path";
import { pathToFileURL } from "node:url";
import { createRun, repoRoot } from "./lib/harness.mjs";

const ROOT = repoRoot();
const run = createRun("假桥与前端桥");
const at = (rel) => import(pathToFileURL(path.join(ROOT, rel)).href);
const api = await at("web/shared/bridge-api.js");
const { createMockBackend } = await at("web-preview/mock/backend.js");
const { buildSnapshot, fields } = await at("web-preview/mock/snapshot.js");
const { createBridge } = await at("web/shared/bridge.js");

// ---- 快照形状 ----
const snap = buildSnapshot({ lang: "en", uiScale: 0.8, layout: "stereo" });
run.eq(Object.keys(snap), api.SNAPSHOT_ROOT, "快照根的字段与顺序应与 SNAPSHOT_ROOT 一致");
run.eq(Object.keys(snap.device), api.SNAPSHOT_DEVICE, "device 的字段应与 SNAPSHOT_DEVICE 一致");
run.eq(Object.keys(snap.connect), api.SNAPSHOT_CONNECT, "connect 的字段应与 SNAPSHOT_CONNECT 一致");
run.eq(Object.keys(snap.steps), api.ENUM_STEP_ID, "steps 的键应是全部步骤");
for (const id of api.ENUM_STEP_ID)
    run.eq(Object.keys(snap.steps[id]), api.SNAPSHOT_STEP, `steps.${id} 的字段应与 SNAPSHOT_STEP 一致`);
run.eq(
    [snap.apiVersion, snap.lang, snap.uiScale, snap.device.layout],
    [api.BRIDGE_API_VERSION, "en", 0.8, "stereo"],
    "快照应带上调用方给的值",
);
run.eq(snap.results, {}, "没做过的步骤没有 results 的键");
for (const id of api.ENUM_STEP_ID) {
    run.check(api.ENUM_STEP_STATUS.includes(snap.steps[id].status), `steps.${id}.status 应是合法枚举`);
}
run.check(api.ENUM_SIGNAL_STATE.includes(snap.connect.signalState), "signalState 应是合法枚举");
// 反例：多一个或少一个字段都要抛错
let threw = false;
try {
    fields(["a", "b"], { a: 1 }, "x");
} catch {
    threw = true;
}
run.check(threw, "反例：缺字段应当抛错");
threw = false;
try {
    fields(["a"], { a: 1, b: 2 }, "x");
} catch {
    threw = true;
}
run.check(threw, "反例：多字段应当抛错");

// ---- 回执规则 ----
const resized = [];
const mock = createMockBackend({ uiScale: 1, onResize: (f) => resized.push(f) });
const pushed = [];
mock.addEventListener("miciri.state", (s) => pushed.push(s));
for (const name of api.BRIDGE_FUNCTIONS) run.check(typeof mock[name] === "function", `mock 应实现桥函数 ${name}`);

run.eq(await mock.setLang("fr"), { ok: false, reason: "bad-lang" }, "不认识的语言回 bad-lang");
run.eq(await mock.setLang("en"), { ok: true }, "setLang en 应成功");
run.eq(pushed.at(-1).lang, "en", "setLang 之后应推送新快照");
run.eq(await mock.setUiScale(0.7), { ok: false, reason: "bad-scale" }, "不在档位表的缩放回 bad-scale");
run.eq(await mock.commitUiScale(), { ok: false, reason: "no-pending" }, "没有待确认项时 commitUiScale 回 no-pending");
run.eq(await mock.setUiScale(0.8), { ok: true }, "setUiScale 0.8 应成功");
run.eq(
    [resized.at(-1), mock.hasPending(), pushed.at(-1).uiScale],
    [0.8, true, 0.8],
    "设置后应改窗口、记待确认、推快照",
);
run.eq(await mock.setUiScale(1), { ok: true }, "回到已提交的档位应成功");
run.eq(mock.hasPending(), false, "回到已提交的档位后待确认项应消失");
run.eq(await mock.commitUiScale(), { ok: false, reason: "no-pending" }, "回退之后不能再提交");
await mock.setUiScale(1.25);
run.eq(await mock.commitUiScale(), { ok: true }, "有待确认项时 commitUiScale 应成功");
run.eq([mock.committedScale(), mock.hasPending()], [1.25, false], "提交后已提交档位更新、待确认项清空");
run.eq(await mock.startStep("nope"), { ok: false, reason: "bad-step" }, "不认识的步骤回 bad-step");
run.eq(await mock.setMicType("ribbon"), { ok: false, reason: "bad-arg" }, "不认识的话筒类型回 bad-arg");
run.eq(await mock.setGuideSeen("yes"), { ok: false, reason: "bad-arg" }, "类型不符回 bad-arg");
const locked = createMockBackend({ scaleLocked: true });
run.eq(await locked.setUiScale(0.8), { ok: false, reason: "scale-locked" }, "缩放被锁时回 scale-locked");
run.check(
    mock.calls.every((c) => api.BRIDGE_FUNCTIONS.includes(c.name)),
    "calls 里只应有名表内的函数",
);

// ---- 前端桥 ----
const bridge = createBridge({ mockBackend: createMockBackend({}) });
run.eq([bridge.isPreview, typeof bridge.on], [true, "function"], "注入 mock 时应走预览分支");
for (const name of api.BRIDGE_FUNCTIONS) run.check(typeof bridge[name] === "function", `桥上应挂 ${name}`);
run.eq(await bridge.setLang("zh"), { ok: true }, "桥函数应原样透传回执");
threw = false;
try {
    bridge.on("miciri.nope", () => {});
} catch {
    threw = true;
}
run.check(threw, "订阅名表外的事件应当抛错");
const seen = [];
const be = createMockBackend({});
const b2 = createBridge({ mockBackend: be });
const off = b2.on("miciri.state", (s) => seen.push(s.lang));
await b2.setLang("en");
off();
await b2.setLang("zh");
run.eq(seen, ["en"], "事件应分发给订阅者，退订后不再收到");
const incomplete = createMockBackend({});
delete incomplete.showLicense;
let msg = "";
try {
    createBridge({ mockBackend: incomplete });
} catch (e) {
    msg = String(e.message);
}
run.check(msg.includes("showLicense"), `mock 缺函数时应抛错并列出缺失项，实得：${msg}`);
msg = "";
try {
    createBridge({});
} catch (e) {
    msg = String(e.message);
}
run.check(msg.includes("mock"), "既没有真宿主也没有 mock 时应抛错并给出指引");

run.finish();
