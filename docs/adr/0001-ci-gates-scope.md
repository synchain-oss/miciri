# ADR-0001：CI 与本地 gates 的范围

## Summary (EN)

- **Decision**: keep a minimal set of repository checks, each with a stated reason; the cut ones are listed here so they are not added back by habit.
- **Consequences**: sub-PRs run four required checks (`branch-gate`, `clang-format`, `docs-truth`, `compliance`); builds run locally and on PRs into `dev` and `prod`.
- **Compatibility**: adding a check means adding a reason below; removing one means moving it to the cut list.

## 状态

已采纳（2026-11-02）。

## 背景

CI 与 gates 从同组织另一个仓的做法派生而来，那个仓有四十来个脚本，大半服务于它自己的产品（共享内存、配对、探针、冻结布局）。本仓要的是轻量的门禁：防泄露、防署名出错、防不安全的 workflow、防公开仓里出现真实录音。

## 保留的检查与理由

| 检查 | 理由 |
|---|---|
| branch-gate（check-commits、PR 标题与正文） | DCO、身份、署名、分支规则，公开仓第一天就要 |
| privacy、hooks | 个人邮箱、本机路径、内部编号一旦推出就收不回；本机钩子在提交与推送前拦 |
| spdx、reuse | GPL 项目的许可头与第三方登记 |
| workflows、actions-lock、pins、required-contexts | workflow 加固、action 钉 SHA、工具钉版、required 名称与 workflow 对得上 |
| review-bot | 评审 bot 的 prompt、模型分档、尾注格式与调用方配置不能悄悄漂移 |
| upstream | 复制来的文件必须登记来源 |
| path-case、offline、recursive-delete | Windows 与 macOS 大小写冲突；插件不联网；递归删除只许删自己建的目录 |
| wav-size | 公开仓只放合成夹具：单个 ≤ 200 KB，合计 < 5 MB，只在 `tests/fixtures/` 与 `tests/golden/` |
| pr-merge（v0，只读） | 合并前核对 bot 尾注、pr-agent、thread、required checks，并列出全部评论 |

## 砍掉的内容

- 变异检查框架（`--mutation-check`）：自测的坏样例已经覆盖，框架本身太重。
- 共享内存、探针、配对、两套安装包相关的构建与契约检查，截图类 workflow。
- 许可闭包与模块闭包检查、结构与基础设施路径清单、措辞与声明类检查、标签检查。
- 合并门的完整复刻（豁免记录、构建后指纹、锁存标记）与 PR 开设、推送、dispatch 的包装脚本：改用 v0 只读核对，合并由维护者手动做。
- `bootstrap-tools.ps1` 与 PowerShell 版 TempDir 模块：钉版工具经 `scripts/setup-local.ps1` 配到 git 配置，删除只走 node 的 `safe-rm`。
- sccache、字体工具、`pr-agent.yml`（本仓只有 `pr-agent-claude` 一个 pr-agent）。

## 后果

- 子 PR 不跑完整构建是有意的；`build.yml` 与 `release.yml` 在还没有 CMake 与打包脚本时明确判红，不会假绿。
- DSP golden、分配计数器（alloc canary）目前是占位：gates 记 SKIP 并写明原因，夹具与测试落地后改成真检查。
