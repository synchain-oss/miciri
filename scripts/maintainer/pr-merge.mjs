#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-or-later
//
// 合并前检查 v0：只读，不合并、不评论、不改任何 GitHub 状态。四条判据，全过才 ready：
//   1. bot-footer      当前 head 上有 claude-pr-review 的总结（github-actions[bot] 发的，首行 head 标记的 sha 等于 PR head），
//                      它的末行机读尾注是 red=0 important=0
//   2. pr-agent        pr-agent-claude 在当前 head 上已完成且成功（check run 名含 pr-agent；skipped 不算）
//   3. threads-resolved  所有 review thread 都已 resolved（线程多到要翻页时按未通过算）
//   4. required-checks .github/required-checks.json 里这个 base 要求的 check 在当前 head 上都是 success
//                      （同名取最近一次；缺、进行中、失败、跳过都不算）
// 另外把所有评论（issue comments、review comments、reviews，以及 review thread 的状态）逐条列成 JSON，附上机器能判断的
// 处置状态；外部评论（既不是 DLsnows 也不是 bot）与 bot 汇总里没开 thread 的意见标成需要人看，它们不影响 ready：
// 这个脚本只证明「判据成立」，评论有没有被读过、回应过，仍要人对着 JSON 逐条确认。
//   node scripts/maintainer/pr-merge.mjs check --pr <n> [--repo <owner/name>] [--out <文件>]
//   node scripts/maintainer/pr-merge.mjs --self-test
// 退出码：0 ready；1 not ready；2 出错（取不到数据、账号不对）。读取经 gh；每次先断言当前账号是 DLsnows，不切换账号。

import fs from "node:fs";
import path from "node:path";
import { execFileSync } from "node:child_process";
import { FOOTER, headMark } from "../review-bot.mjs";
import { ROOT, assertNodeVersion, caseResult, isMain } from "../lib/check-kit.mjs";

export const ME = "DLsnows";
const BOT_AUTHOR = "github-actions[bot]";
const REVIEW_BOT = "claude";

const isBot = (u) => u?.type === "Bot" || /\[bot\]$/.test(u?.login || "");
const oneLine = (s, n = 160) => (s || "").replace(/\s+/g, " ").trim().slice(0, n);
const lastLine = (body) =>
    (body || "")
        .split(/\r?\n/)
        .map((l) => l.trim())
        .filter(Boolean)
        .at(-1) || "";

/** 这个 base 要求哪些 check：feature/* 走 feature-lines，dev、prod 各自一组；没有记录返回 null。 */
export function requiredFor(base, required) {
    const key = /^feature\//.test(base) ? "feature-lines" : base;
    return Array.isArray(required?.[key]?.contexts) ? required[key].contexts : null;
}

/** 同名的 check run 取最近一次（started_at，其次 id）。 */
function latestRuns(checkRuns) {
    const by = new Map();
    for (const r of checkRuns) {
        const old = by.get(r.name);
        const key = (x) => `${x.started_at || ""}|${String(x.id).padStart(20, "0")}`;
        if (!old || key(r) > key(old)) by.set(r.name, r);
    }
    return by;
}
const runVerdict = (r) =>
    !r
        ? "缺"
        : r.status !== "completed"
          ? `未完成（${r.status}）`
          : r.conclusion === "success"
            ? null
            : `结论 ${r.conclusion}`;

/** 纯函数：吃已取回的数据，吐判据与评论清单。 */
export function evaluate({
    pr,
    issueComments = [],
    reviewComments = [],
    reviews = [],
    threads,
    checkRuns = [],
    required,
}) {
    const head = pr.head;
    const checks = [];
    const add = (id, ok, detail) => checks.push({ id, ok, detail });

    // 1. bot 总结
    const summaries = issueComments.filter((c) => c.user?.login === BOT_AUTHOR && headMark(c.body)?.bot === REVIEW_BOT);
    const current = summaries.filter((c) => headMark(c.body).sha === head).at(-1);
    if (!current) {
        add(
            "bot-footer",
            false,
            summaries.length
                ? `claude-pr-review 的总结都不是针对当前 head ${head.slice(0, 7)} 的`
                : "还没有 claude-pr-review 的总结",
        );
    } else {
        const m = FOOTER.exec(lastLine(current.body));
        if (!m) add("bot-footer", false, "当前 head 的总结末行不是机读尾注");
        else if (m[1] !== "0" || m[2] !== "0")
            add("bot-footer", false, `尾注 red=${m[1]} important=${m[2]}（要求都是 0）`);
        else add("bot-footer", true, `red=0 important=0 suggest=${m[3]}（评论 ${current.id}）`);
    }

    // 2. pr-agent
    const runs = latestRuns(checkRuns);
    const agent = [...runs.values()]
        .filter((r) => /pr-agent/i.test(r.name))
        .sort((a, b) => (a.started_at < b.started_at ? 1 : -1))[0];
    const agentBad = runVerdict(agent);
    add("pr-agent", !agentBad, agentBad ? `pr-agent 检查：${agentBad}` : `${agent.name} 已完成且成功`);

    // 3. 线程
    if (!threads || threads.truncated)
        add("threads-resolved", false, "取不到完整的 review thread 列表（超过一页或查询失败）");
    else {
        const open = threads.nodes.filter((t) => !t.isResolved);
        add(
            "threads-resolved",
            open.length === 0,
            open.length ? `${open.length} 个 thread 未 resolved` : `${threads.nodes.length} 个 thread 全部 resolved`,
        );
    }

    // 4. required checks
    const ctx = requiredFor(pr.base, required);
    if (!ctx) add("required-checks", false, `required-checks.json 里没有 ${pr.base} 对应的条目`);
    else {
        const bad = ctx.map((n) => [n, runVerdict(runs.get(n))]).filter(([, v]) => v);
        add(
            "required-checks",
            bad.length === 0,
            bad.length ? bad.map(([n, v]) => `${n}：${v}`).join("；") : `${ctx.join("、")} 全部 success`,
        );
    }

    // 评论清单
    const threadOf = new Map();
    for (const t of threads?.nodes || [])
        for (const c of t.comments?.nodes || []) threadOf.set(String(c.databaseId), t);
    const comments = [];
    const push = (kind, c, disposition, needsHuman = false) =>
        comments.push({
            kind,
            id: c.id ?? c.databaseId,
            author: c.user?.login ?? c.author?.login ?? "?",
            at: c.created_at ?? c.submitted_at ?? null,
            disposition,
            needsHuman,
            text: oneLine(c.body),
        });
    const classify = (c, bot, whenBot) => {
        const login = c.user?.login;
        if (login === ME) return ["own", false];
        if (isBot(c.user) || bot) return whenBot();
        return ["external：不是 DLsnows 也不是 bot，交给用户", true];
    };
    for (const c of issueComments) {
        const hm = c.user?.login === BOT_AUTHOR ? headMark(c.body) : null;
        const [d, n] = classify(c, false, () =>
            hm
                ? [
                      `bot-summary（${hm.bot}，${hm.sha === head ? "当前 head" : `旧 head ${hm.sha.slice(0, 7)}`}）`,
                      false,
                  ]
                : ["bot 评论：读完并回应，汇总里没开 thread 的意见也要处理", true],
        );
        push("issue-comment", c, d, n);
    }
    for (const c of reviewComments) {
        const t = threadOf.get(String(c.id));
        const [d, n] = classify(c, false, () =>
            t
                ? [t.isResolved ? "thread 已 resolved" : "thread 未 resolved", !t.isResolved]
                : ["bot 行内评论，没找到所属 thread", true],
        );
        push(
            "review-comment",
            c,
            c.user?.login === ME && t ? (t.isResolved ? "own（thread 已 resolved）" : "own（thread 未 resolved）") : d,
            n,
        );
    }
    for (const r of reviews) {
        const [d, n] = classify(r, false, () => [`review ${r.state}`, !!(r.body || "").trim()]);
        push("review", r, d, n);
    }
    const ready = checks.every((c) => c.ok);
    return { ready, head, base: pr.base, checks, needsHuman: comments.filter((c) => c.needsHuman).length, comments };
}

// ---------- 取数（gh）

function gh(args, { input } = {}) {
    return execFileSync("gh", args, {
        encoding: "utf8",
        input,
        maxBuffer: 64 * 1024 * 1024,
        stdio: ["pipe", "pipe", "pipe"],
    });
}
const ndjson = (text) =>
    text
        .split(/\r?\n/)
        .filter(Boolean)
        .map((l) => JSON.parse(l));
// 每个元素用 @json 转成一行紧凑 JSON：gh 的 --jq 对字符串原样输出，所以评论正文里的换行被转义后仍只占一行
const pages = (route, jq = ".[] | @json") => ndjson(gh(["api", "--paginate", route, "--jq", jq]));

const THREADS_QUERY = `query($owner:String!,$name:String!,$number:Int!){repository(owner:$owner,name:$name){pullRequest(number:$number){
reviewThreads(first:100){pageInfo{hasNextPage} nodes{isResolved isOutdated comments(first:100){nodes{databaseId author{login}}}}}}}}`;

export function fetchAll(repo, number) {
    const who = gh(["api", "user", "--jq", ".login"]).trim();
    if (who !== ME)
        throw new Error(
            `gh 当前账号是 ${who || "?"}，不是 ${ME}；不切换账号，请用 GH_TOKEN 只给这条命令传 DLsnows 的 token`,
        );
    const raw = JSON.parse(gh(["api", `repos/${repo}/pulls/${number}`]));
    const [owner, name] = repo.split("/");
    let threads = null;
    try {
        const q = JSON.parse(
            gh([
                "api",
                "graphql",
                "-f",
                `query=${THREADS_QUERY}`,
                "-F",
                `owner=${owner}`,
                "-F",
                `name=${name}`,
                "-F",
                `number=${number}`,
            ]),
        );
        const rt = q.data.repository.pullRequest.reviewThreads;
        threads = { nodes: rt.nodes, truncated: rt.pageInfo.hasNextPage };
    } catch {
        threads = null;
    }
    return {
        pr: { head: raw.head.sha, base: raw.base.ref, state: raw.state, draft: raw.draft },
        issueComments: pages(`repos/${repo}/issues/${number}/comments`),
        reviewComments: pages(`repos/${repo}/pulls/${number}/comments`),
        reviews: pages(`repos/${repo}/pulls/${number}/reviews`),
        threads,
        checkRuns: pages(`repos/${repo}/commits/${raw.head.sha}/check-runs`, ".check_runs[] | @json"),
        required: JSON.parse(fs.readFileSync(path.join(ROOT, ".github/required-checks.json"), "utf8")),
    };
}

// ---------- 自测

export function selfTest() {
    const HEAD = "a".repeat(40);
    const OLD = "b".repeat(40);
    const summary = (sha, footer = "<!-- miciri-review v1 red=0 important=0 suggest=2 -->", login = BOT_AUTHOR) => ({
        id: 1,
        user: { login, type: login.endsWith("[bot]") ? "Bot" : "User" },
        created_at: "2026-01-01T00:00:00Z",
        body: `<!-- miciri-review-head sha=${sha} bot=claude -->\n审查对象\n\n## 摘要\n无\n\n${footer}`,
    });
    const run = (name, conclusion = "success", status = "completed", t = "2026-01-01T00:01:00Z", id = 1) => ({
        id,
        name,
        status,
        conclusion,
        started_at: t,
    });
    const green = () => ({
        pr: { head: HEAD, base: "feature/v1" },
        issueComments: [summary(HEAD)],
        reviewComments: [],
        reviews: [],
        threads: { nodes: [{ isResolved: true, comments: { nodes: [{ databaseId: 7 }] } }], truncated: false },
        checkRuns: ["branch-gate", "clang-format", "docs-truth", "compliance"]
            .map((n) => run(n))
            .concat(run("pr-agent-claude / pr-agent")),
        required: {
            "feature-lines": { contexts: ["branch-gate", "clang-format", "docs-truth", "compliance"] },
            dev: { contexts: ["branch-gate"] },
        },
    });
    const mut = (f) => {
        const x = green();
        f(x);
        return x;
    };
    const failedIds = (x) =>
        evaluate(x)
            .checks.filter((c) => !c.ok)
            .map((c) => c.id);
    const bad = (label, x, id) => ({ label, bad: true, passed: failedIds(x).includes(id) && !evaluate(x).ready });
    const good = (label, x, pred = (e) => e.ready) => ({ label, bad: false, passed: pred(evaluate(x)) });
    const cases = [
        good("全绿 ready", green()),
        bad(
            "没有 bot 总结",
            mut((x) => (x.issueComments = [])),
            "bot-footer",
        ),
        bad(
            "总结针对旧 head",
            mut((x) => (x.issueComments = [summary(OLD)])),
            "bot-footer",
        ),
        bad(
            "red=1",
            mut((x) => (x.issueComments = [summary(HEAD, "<!-- miciri-review v1 red=1 important=0 suggest=0 -->")])),
            "bot-footer",
        ),
        bad(
            "important=2",
            mut((x) => (x.issueComments = [summary(HEAD, "<!-- miciri-review v1 red=0 important=2 suggest=0 -->")])),
            "bot-footer",
        ),
        bad(
            "尾注不在末行",
            mut((x) => (x.issueComments = [{ ...summary(HEAD), body: `${summary(HEAD).body}\n\n多余的一行` }])),
            "bot-footer",
        ),
        bad(
            "非 bot 账号伪造总结",
            mut((x) => (x.issueComments = [summary(HEAD, undefined, "someone")])),
            "bot-footer",
        ),
        bad(
            "pr-agent 缺",
            mut((x) => (x.checkRuns = x.checkRuns.filter((r) => !/pr-agent/.test(r.name)))),
            "pr-agent",
        ),
        bad(
            "pr-agent 进行中",
            mut(
                (x) =>
                    (x.checkRuns = x.checkRuns.map((r) =>
                        /pr-agent/.test(r.name) ? run(r.name, null, "in_progress") : r,
                    )),
            ),
            "pr-agent",
        ),
        bad(
            "pr-agent 失败",
            mut((x) => (x.checkRuns = x.checkRuns.map((r) => (/pr-agent/.test(r.name) ? run(r.name, "failure") : r)))),
            "pr-agent",
        ),
        bad(
            "pr-agent 被跳过不算",
            mut((x) => (x.checkRuns = x.checkRuns.map((r) => (/pr-agent/.test(r.name) ? run(r.name, "skipped") : r)))),
            "pr-agent",
        ),
        bad(
            "有未 resolved 的 thread",
            mut((x) => x.threads.nodes.push({ isResolved: false, comments: { nodes: [] } })),
            "threads-resolved",
        ),
        bad(
            "thread 要翻页",
            mut((x) => (x.threads.truncated = true)),
            "threads-resolved",
        ),
        bad(
            "取不到 thread",
            mut((x) => (x.threads = null)),
            "threads-resolved",
        ),
        bad(
            "必需检查缺一个",
            mut((x) => (x.checkRuns = x.checkRuns.filter((r) => r.name !== "compliance"))),
            "required-checks",
        ),
        bad(
            "必需检查失败",
            mut((x) => (x.checkRuns = x.checkRuns.map((r) => (r.name === "docs-truth" ? run(r.name, "failure") : r)))),
            "required-checks",
        ),
        bad(
            "必需检查进行中",
            mut(
                (x) =>
                    (x.checkRuns = x.checkRuns.map((r) =>
                        r.name === "branch-gate" ? run(r.name, null, "queued") : r,
                    )),
            ),
            "required-checks",
        ),
        bad(
            "base 没有对应条目",
            mut((x) => (x.pr.base = "hotfix/x")),
            "required-checks",
        ),
        good(
            "同名重跑，最近一次成功",
            mut((x) => x.checkRuns.push(run("compliance", "failure", "completed", "2025-12-31T00:00:00Z", 0))),
        ),
        bad(
            "同名重跑，最近一次失败",
            mut((x) => x.checkRuns.push(run("compliance", "failure", "completed", "2026-01-02T00:00:00Z", 9))),
            "required-checks",
        ),
        good(
            "外部评论不改变 ready，但标成需要人看",
            mut((x) => x.issueComments.push({ id: 5, user: { login: "stranger", type: "User" }, body: "hi" })),
            (e) => e.ready && e.comments.some((c) => c.needsHuman && /^external/.test(c.disposition)),
        ),
        good(
            "bot 评论列出且标成要读",
            mut((x) =>
                x.issueComments.push({ id: 6, user: { login: "qodo-merge-pro[bot]", type: "Bot" }, body: "建议" }),
            ),
            (e) => e.comments.some((c) => c.id === 6 && c.needsHuman),
        ),
        good(
            "DLsnows 的评论是 own",
            mut((x) => x.issueComments.push({ id: 8, user: { login: ME, type: "User" }, body: "处置" })),
            (e) => e.comments.some((c) => c.id === 8 && c.disposition === "own" && !c.needsHuman),
        ),
        good(
            "行内评论对应 resolved 的 thread",
            mut((x) => x.reviewComments.push({ id: 7, user: { login: "bot[bot]", type: "Bot" }, body: "x" })),
            (e) =>
                e.comments.some(
                    (c) => c.kind === "review-comment" && c.disposition === "thread 已 resolved" && !c.needsHuman,
                ),
        ),
    ];
    return caseResult(cases);
}

// ---------- CLI

function main(argv) {
    if (argv.includes("--self-test")) {
        const r = selfTest();
        for (const l of r.lines) console.error(l);
        console.log(r.ok ? `pr-merge 自测通过（坏样例 ${r.bad}，正例 ${r.good}）` : "pr-merge 自测未通过");
        return r.ok ? 0 : 1;
    }
    const arg = (f) => (argv.includes(f) ? argv[argv.indexOf(f) + 1] : undefined);
    if (argv[0] !== "check" || !/^\d+$/.test(arg("--pr") || "")) {
        console.error("用法：pr-merge.mjs check --pr <n> [--repo <owner/name>] [--out <文件>] | --self-test");
        return 2;
    }
    assertNodeVersion();
    try {
        const repo = arg("--repo") || "synchain-oss/miciri";
        const result = { repo, pr: Number(arg("--pr")), ...evaluate(fetchAll(repo, arg("--pr"))) };
        const text = JSON.stringify(result, null, 2);
        if (arg("--out")) fs.writeFileSync(arg("--out"), `${text}\n`);
        else console.log(text);
        console.error(
            result.ready
                ? "READY：四项判据全过（评论仍要人逐条核对）"
                : `NOT READY：${result.checks
                      .filter((c) => !c.ok)
                      .map((c) => c.id)
                      .join("、")}`,
        );
        return result.ready ? 0 : 1;
    } catch (e) {
        console.error(`pr-merge 出错：${e.message}`);
        return 2;
    }
}

if (isMain(import.meta.url)) process.exit(main(process.argv.slice(2)));
