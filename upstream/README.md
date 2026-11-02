# upstream/

本目录登记**复制自同组织其他仓库**的文件：来源仓、分支、提交，以及每个文件对应的来源路径与 blob。`scripts/check-upstream.mjs`（docs-truth 的 `upstream` 检查）把全部片段合起来核对。

## 片段格式

每个片段是 `upstream/<area>.json`，`<area>` 是小写字母、数字与连字符：

- `sources`：来源表，每项有 `repo`、`ref` 和 40 位 `sha`；
- `files`：每项有 `path`（本仓路径）、`source`（`sources` 里的键）、`from`（来源仓里的路径）、`fromBlob`（来源仓该路径在 `sha` 提交上的 40 位 blob）和 `mode`。

`mode` 只有两种：`adapted` 表示跟随来源、只做了必要的改动，之后可以按 `fromBlob` 核对漂移；`forked` 表示已经为本仓改写，不再跟随。

## 文件头

能写注释的文件，在前 8 行里恰好有一行来源声明，形如：

- `# Derived from synchain-oss/<仓库名>; see upstream/<area>.json`（脚本、yml、toml 等用 `#`，源码用 `//`，Markdown 用 `<!-- ... -->`）。

`<area>` 必须是登记这个文件的那个片段。写不了注释的格式（json、版本钉文件、sha256 等）只登记、不要求文件头。文件头里不写内部编号，注释整体改写成本仓自己的说明。

## 现有片段

| 片段 | 内容 |
|---|---|
| `ci` | workflow、检查脚本与运行器、hooks、钉版文件、格式与许可配置 |
| `build` | 构建 workflow 与构建脚本 |
| `release` | 发版 workflow |
| `gates` | 本地 gates、定档规则、`CLAUDE.md`、`CONTRIBUTING.md` |

## 漂移核对

`node scripts/check-upstream.mjs --drift [--ref-dir <来源仓的本地克隆>]` 把 `fromBlob` 与来源分支现在的 blob 比对。它要联网或读本地克隆，只给维护者手动跑，不进 CI。
