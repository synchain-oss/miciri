<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# 架构与目录布局

Miciri 是录音检查插件：音频逐位直通，旁路收样本，在工作线程里分析，结果交给界面显示。本文只写不会轻易变的结构，
契约的细节在 `docs/contracts/`（`RESULT.md`、`THRESHOLDS.md`、`BRIDGE.md`），取舍的理由在 `docs/adr/`。

## 分层与数据流

```
音频线程 ──(无锁队列)──> 工作线程 ──> src/core 纯函数 ──> Result ──> Session 快照 ──> 桥 ──> 界面
 直通、心跳、meter、        收样本、降采样、     analyseX(samples, sr, ctx)         Processor 持有    BridgeApi    只读快照
 削波锁存、推队列           按步骤分段          不依赖 JUCE，double 精度
```

- **所有权**：Session、工作线程和结果都归 Processor；编辑器只读快照，可随时销毁重建，关窗不丢结果，卸载才清除。
- **音频线程**：只做直通、心跳、原子 meter、削波锁存、推无锁队列；不分配、不加锁、不做 I/O、不读墙钟、不抛异常；空闲时零开销。
- **时钟**：所有测试时长与测量都用采样计数，墙钟不参与任何测量。墙钟只用在两处诊断与保护：
  - “没有回调”的诊断：回调停了采样计数也停，所以由编辑器的消息线程定时器读心跳计数，秒数给桥快照，只显示、不改步骤状态。
  - 测试进行时，Session 的工作线程用墙钟作废当前这一步：超过 `no_signal.no_callback_s` 没收到音频（`callback-gap`，与上面同一个阈值），
    或音频到达明显快于实时（宿主没报的离线渲染，`offline-render`）。它们只决定这一步作不作废，不进任何结果。
- **core**：`src/core/` 不依赖 JUCE，纯函数，double 精度，禁 `-ffast-math`；阈值只从 `thresholds.json` 来；
  档位判定和连续量分开，golden 只锁连续量。
- **界面**：WebView 里的页面只依赖桥契约；文案走 i18n；不用 `innerHTML` 与原生 `<select>`。

## 目录与归属

| 路径 | 内容 | 怎么加东西 |
|---|---|---|
| `src/core/<模块>/` | 分析库 `miciri_core`，每个模块一个目录 | 新增目录与 `*.cpp`，自动并入，不改 CMake |
| `src/core/result/` | Result 结构、枚举、色调映射 | 契约，改动走变更规则 |
| `src/core/config/` | `Thresholds.h`：阈值的唯一入口 | 阈值先进 `thresholds.json` |
| `src/plugin/` | 插件本体与 `BridgeApi.h`（桥契约的 C++ 一侧） | 有 `CMakeLists.txt` 就加入；依赖 JUCE，`MICIRI_CORE_ONLY` 下自己 `return()` |
| `src/session/` 等 `src/<名>/` | 其余库 | 有 `CMakeLists.txt` 就加入 |
| `web/shared/` | 前端共享层，含 `bridge-api.js`（桥契约的 JS 一侧） | — |
| `web/app/<区>/` | 各页面 | — |
| `tools/<名>/` | 命令行工具，如 wav 转 JSON | 有 `CMakeLists.txt` 就加入 |
| `tests/<区>/` | 测试 | 有 `CMakeLists.txt` 就加入，用 `miciri_add_test` 登记 |
| `thresholds.json`、`thresholds.schema.json`（都在仓根）、`tools/gen-thresholds.cmake` | 阈值数据、schema、生成脚本 | — |
| `cmake/` | `MiciriWarnings`（告警、浮点、sanitizer）、`MiciriJuceGuard`、`MiciriThresholds` | — |
| `upstream/` | 复制来的文件的来源登记 | 见 `upstream/README.md` |

根 `CMakeLists.txt` 不为新模块改动：`src/core/*/*.cpp`、`src/*/CMakeLists.txt`、`tools/*/CMakeLists.txt`、`tests/**/CMakeLists.txt`
都按目录自动收集（`CONFIGURE_DEPENDS`）。第三方头文件库放 `third_party/<名>/`，`pocketfft` 目录存在时自动进 core 的头文件搜索路径。

## 构建

- 选项：`MICIRI_CORE_ONLY`（只建 core 与测试，不需要 JUCE）、`MICIRI_WARNINGS_AS_ERRORS`、`MICIRI_SANITIZER`（`none`、`asan_ubsan`、`tsan`，只作用于 core 与测试）。
- JUCE 只经 `-DJUCE_PATH=<检出>` 传入，`cmake/MiciriJuceGuard.cmake` 核对检出恰好是 `.juce-commit`。
- 本机与 CI 都走 `scripts/build.ps1`；测试都登记进 ctest，DSP golden 带标签 `golden`，分配计数器带标签 `alloc`。
- 插件身份变量定义在根 `CMakeLists.txt`：公司 `Synchain`，厂商码 `Snch`，插件码 `Mcri`，bundle `com.synchain.miciri`，
  VST3 类别 `Fx|Analyzer`；格式只有 VST3 与 AU（macOS）；Windows x64，macOS arm64。不出 Standalone 与 AAX。
