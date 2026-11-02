# ADR-0002：契约与目录布局

## Summary (EN)

- **Decision**: thresholds live in `thresholds.json` and reach C++ through a header generated at configure time by a CMake script (no extra tool, no runtime parsing); results are `Result<Measures, Verdict>` with continuous values separated from bands; the bridge contract is two mirrored name tables (JS and C++) compared by a test; directories are collected automatically so later work only adds files.
- **Consequences**: no magic numbers in `src/core`; golden files lock only continuous values; a drift between the UI and the plugin turns a test red; shared CMake files do not change when modules are added.
- **Compatibility**: contracts are append-only within a version; renaming, removing or reordering needs a version bump and a contract-change note.

## 状态

已采纳（2026-11-02）。

## 背景

分析算法、界面、Session 和插件骨架要并行做。它们共用三样东西：阈值、结果的形状、界面与插件之间的消息。先把这三样和文件布局定下来，
后续工作就只需要新增文件，几乎不碰共享文件，也不会因为字段对不上而在合并时才发现。

## 决定

**1. 阈值怎么进 C++：配置期用 CMake 脚本生成头文件。**
`thresholds.json` 每项带值、来源、标定日期、状态；`tools/gen-thresholds.cmake` 在配置期读 JSON（CMake 3.19 起自带解析）、按
`tools/thresholds-schema.json` 校验，生成 `miciri::thr::<组>::<名>` 的 `constexpr` 常量。考虑过的替代：

- 用 Node 或 Python 生成：构建就依赖一个钉版解释器，而 `scripts/build.ps1`、CI 的构建 job 和 sanitizer job 并不都带它；全局版本不能用于仓内脚本。
- 运行时读 JSON：core 要带 JSON 解析与文件 I/O，插件要随包携带数据文件，路径问题进入宿主环境；音频线程之外读盘也违背“只在内存里”的原则。
- 手写常量头文件：回到魔法数，状态与日期无处记录。

代价：数值统一成 `double`（整数也是 `double`），数组是 `std::array<double, N>`；`null` 项不生成，所以引用“还没有值”的阈值会编译失败，这是有意的。
校验口径只在 schema 里写一份，生成脚本从 schema 读必填字段、允许字段和 `state` 取值。

**2. Result 的形状：四块，档位与连续量分开。**
`Result<Measures, Verdict>` 加 `confidence`（可信，或“再测一次”加原因）和 `evidence`。`Measures` 只含算术类型（静态断言），golden 只锁它；
`Verdict` 由 measures 加阈值得出，所以改阈值不会让 golden 变红，也不需要重录。数字削波次数与模拟平顶处数是两个字段，色调映射保证平顶最多到琥珀。
枚举用一处定义的 X 宏生成（标识符、线上名字、`toString`），桥契约直接引用同一份名字。第 3 组（房间、位置、喷麦、齿音、口水音、方向、距离）先定字段，算法以后做。

**3. 桥 API 的版本与扩展：两份镜像加对拍。**
`web/shared/bridge-api.js` 与 `src/plugin/BridgeApi.h` 只列名字（函数、事件、失败原因、快照字段、枚举取值），不含逻辑；`tests/contracts` 逐项对拍，
并对两边各做若干破坏，确认对拍会变红。JS 文件限定为字面量文法，测试不执行它。`BridgeApi.h` 不依赖 JUCE，所以只建 core 的构建也能编。
版本规则：`BRIDGE_API_VERSION` 同一版本内只追加（表末尾加项），界面忽略不认识的字段与枚举值；改名、删除、调换顺序要升版本并写变更说明。
考虑过的替代：从一份 JSON 或 IDL 生成两边——多一个生成步骤和一份新格式，而这里只有名字，对拍已经足够。

**4. 布局与 CMake：按目录自动收集。**
`src/core/<模块>/*.cpp` 并入 `miciri_core`；`src/*/CMakeLists.txt`、`tools/*/CMakeLists.txt`、`tests/**/CMakeLists.txt` 存在就加入；
测试用 `miciri_add_test` 登记。依赖 JUCE 的目录在 `MICIRI_CORE_ONLY` 下自己 `return()`。`cmake/` 里的告警与 JUCE 守卫由同组织另一个仓的对应文件改名改写而来，登记在 `upstream/cmake.json`。

## 没做的

- 网页资源嵌入函数与 WebView2 的取包：属于插件骨架，放在 `src/plugin/` 里，不进共享 CMake。
- 契约变更的自动检查：`scripts/check-commits.mjs` 守的是 `docs/contracts/RESULT.md`、`THRESHOLDS.md`、`BRIDGE.md` 与根目录的 `thresholds.schema.json`；
  本仓的 schema 在 `tools/thresholds-schema.json`，守卫的路径需要对应更新。

## 后果

- 加阈值：改 `thresholds.json`，配置期自动生成、校验；加错会在配置时报出路径与原因。
- 加桥字段：JS、`BridgeApi.h`（或 `Enums.h`）、`BRIDGE.md` 同一个提交里一起改，三处对拍才绿。
- 加模块：新增目录与文件即可；不改根 `CMakeLists.txt`、`src/core/CMakeLists.txt`、`tests/CMakeLists.txt`。
