<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# 宿主探针（仅测试构建）

给做宿主测试的人看。外部的宿主测试脚本读不到 WebView，插件也没有参数可读，所以测试构建的插件自己把计数写成一个 JSON 文件，
脚本去读这个文件，就能断言“插件在各种渲染模式下看到的离线标志”和“离线时分析次数为 0”。

**发布版完全不含这段代码。** 探针的源文件不编进发布版插件，二进制里没有探针代码，也没有下面的环境变量名。

## 开关

| 开关 | 说明 |
|---|---|
| CMake 选项 `MICIRI_HOST_PROBE` | 默认 `OFF`。`ON` 时探针的源文件编进插件，并给插件目标加编译定义 `MICIRI_HOST_PROBE=1` |
| 环境变量 `MICIRI_HOST_PROBE_JSON` | JSON 的输出路径（宿主进程启动前设好）。没设或为空，就什么都不写，也不起定时器 |

构建：`cmake -S . -B <构建目录> -DJUCE_PATH=<JUCE 检出> -DMICIRI_HOST_PROBE=ON`。ON 构建只用于宿主测试，不得发布。

## 什么时候写

- 消息线程上约每 250 ms 写一次，内容没变就不写。
- `prepareToPlay`、`releaseResources` 和插件实例销毁时各再写一次，所以宿主关掉插件后文件里是最后的数字。
- 写法是先写同目录的临时文件，再原子改名覆盖：读文件的一方永远看不到写了一半的文件。目标目录要事先存在，插件不替你建。
- 写失败（目录不存在、没权限、文件被读者占着）不会影响音频，也不会弹窗；下一个节拍会再试。

## 字段

`schema` 固定为 1；字段改名或含义变化时才升。所有计数从进程里第一个插件实例创建起累计，**同一进程里的多个实例合计，写进同一个文件**，
所以做宿主测试时每个宿主进程只放一个实例，最好每次测试换一个输出路径。

| 字段 | 含义 |
|---|---|
| `schema` | 固定 1 |
| `pid` | 宿主进程号（用来确认读到的是这次启动写的文件） |
| `sampleRate`、`maxBlockSize` | 最近一次 `prepareToPlay` 的采样率与最大块长 |
| `numInputChannels`、`numOutputChannels` | 最近一次 `prepareToPlay` 时主总线的声道数 |
| `prepareCount` | `prepareToPlay` 被调用的次数 |
| `blocksTotal` | 收到的有样本的音频块数（0 样本的调用不计），恒等于 `blocksNonRealtime + blocksRealtime`，也恒等于 `blocksAllZero + blocksNonZero` |
| `blocksNonRealtime` | 宿主告诉插件“现在是离线处理”（`isNonRealtime()` 为真）时收到的块数 |
| `blocksRealtime` | 其余的块数 |
| `blocksAllZero` | 内容全是 0 的块数（+0 与 -0 都算 0；NaN、Inf、非规格化数算有内容） |
| `blocksNonZero` | 有内容的块数 |
| `editorOpens`、`editorCloses` | 编辑器被创建、被销毁的次数 |
| `analysisCount` | 分析线程做过的分析次数；离线渲染时必须是 0。分析代码接上之前恒为 0 |
| `lastIsNonRealtime` | 最近一个块收到时的离线标志；还没收到任何块时为 `false` |

示例（离线渲染 3 块之后）：

```json
{
  "schema": 1,
  "pid": 12345,
  "sampleRate": 48000,
  "maxBlockSize": 512,
  "numInputChannels": 2,
  "numOutputChannels": 2,
  "prepareCount": 1,
  "blocksTotal": 3,
  "blocksNonRealtime": 3,
  "blocksRealtime": 0,
  "blocksAllZero": 1,
  "blocksNonZero": 2,
  "editorOpens": 0,
  "editorCloses": 0,
  "analysisCount": 0,
  "lastIsNonRealtime": true
}
```

## 对音频与状态的影响

- 不加任何自动化参数，不进插件 state；音频仍然逐位直通。
- 音频线程只做两件事：读一遍块内容判断是否全 0，再做一次 relaxed 原子加一。不分配、不加锁、不做 I/O。
- 文件只在消息线程与宿主的准备线程上写，写文件的动作之间用一把进程内的锁串行化，音频线程碰不到它。

## 给后续代码接钩子

分析线程每做一次分析，调一次 `miciri::probe::noteAnalysis()`（头文件 `src/plugin/HostProbe.h`）。发布构建里它是空函数，
调用不产生任何代码。

## 测试

计数、全 0 判断、JSON 与原子写文件的逻辑不依赖宿主，`tests/plugin/host_probe_test.cpp` 在默认（OFF）构建里照常编译、运行；
带探针的处理器接线由 `tests/plugin/host_probe_processor_test.cpp` 守着，它把处理器源文件在 `MICIRI_HOST_PROBE=1` 下再编一遍。
