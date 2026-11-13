[English](README.md) | **简体中文**

# Synchain Miciri

> 一个开源插件，帮你检查人声录音的设置：增益、距离、话筒方向、房间和噪声。

**Miciri**（读作 *MIC-i-ri*，中文名见切，意为看准距离）面向刚开始录人声的人。它带你做几个简短的测试，然后告诉你最该先改的一件事，
并给出背后的数值。音频原样通过：Miciri 只听，不改你的声音。

> **状态：开发中。** 还没有发布版本。开发在 `feature/v1` 分支上进行。

## 它做什么

把 Miciri 放在你录人声的那条轨道上，照窗口里的步骤做。每一步从几秒到一分钟左右。

| 步骤 | 你做什么 | Miciri 看什么 |
|---|---|---|
| 接通 | 选话筒类型（也可以跳过），说几句话 | 有没有信号进来、左右声道、采样率和缓冲大小 |
| 增益 | 用你录音时最大的音量唱约 8 秒 | 峰值电平、削波、增益该调高还是调低 |
| 安静 | 保持安静约 8 秒 | 底噪、电流声、设备有没有处理声音的迹象 |
| 房间 | 站在离话筒约 1 米的地方用力拍手 | 四个频段的混响时间 T20 |
| 位置 | 回到唱歌的位置：轻拍几下，念两张字表，再自然念一小段 | 直达声与房间声比、喷麦、齿音、口水音，以及太近太远的标尺 |
| 方向（可选） | 朝话筒的四个方向各念几秒 | 当前方向是不是最响最亮 |

报告第一屏是**最该先改的一件事**，加上一到三个具体动作。每项结果只显示一个词和一个主数字，读数、比值和证据点进去再看。
你改了设置以后，只重测受影响的那几项，就能看到“上次 → 现在”。

<!-- 截图随第一个发布版本补上。 -->

## 运行条件

- Windows 10 或 11（x64），带 Microsoft Edge WebView2 运行时（新版 Windows 自带）；或者 Apple 芯片的 macOS（arm64）。
- 能加载 VST3 插件的宿主（Windows、macOS），或 Audio Unit 宿主（macOS）。
- 单声道和立体声轨道。支持什么、不支持什么见[已知问题](docs/KNOWN_ISSUES.md)。

## 支持的 DAW

目前没有任何宿主有完整的测试记录，所以 [docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md) 里每个宿主都写着**未测**。v1 的目标宿主是 REAPER、
Cubase、FL Studio 和 GarageBand / Logic（Audio Unit）。验证过一个，就在那里补上宿主版本和日期。

## 安装

发布文件会叫 `Miciri-v<版本>-win64-setup.exe`、`Miciri-v<版本>-win64.zip`、`Miciri-v<版本>-macos-arm64.pkg` 和
`Miciri-v<版本>-macos-arm64.zip`。刚接触插件：用安装包。自己管理插件目录：用 zip。两种都没有签名，也没有公证。
[使用指南](docs/USER_GUIDE.zh-CN.md)里有逐步的安装说明，包括怎样手动解除 macOS 的隔离。

## 快速开始

1. 把 Miciri 放在你录音的那条轨道上，打开监听，让音频进到插件里。戴上耳机。
2. 打开插件窗口，对着话筒说话。左边的电平条动了，就说明信号进来了。
3. 从增益做到位置。方向是可选的。
4. 看报告，改掉它点名的那一件事，再重测那一部分。

## 隐私

音频只在内存里分析，不写盘，不上传，不进日志，插件不发任何网络请求。详情见 [docs/PRIVACY.zh-CN.md](docs/PRIVACY.zh-CN.md)。

## 从源码构建

需要 CMake、支持 C++17 的编译器，以及 JUCE 8.0.8 的检出（提交号在 `.juce-commit`）。

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DJUCE_PATH=<JUCE 检出目录>
cmake --build build
```

加 `-DMICIRI_CORE_ONLY=ON` 只构建分析核心和它的测试，不需要 JUCE。要参与开发的人请再读 [CONTRIBUTING.md](CONTRIBUTING.md) 里的本地检查。

## 常见问题

**Miciri 是做什么的？** 它对你的话筒信号做几个简短的测试，告诉你想录好人声最该先改的一件事，并给出背后的数值。

**它会改我的声音吗？** 不会。音频逐位直通，Miciri 不会静音、滤波或处理信号。

**它会录音或上传我的音频吗？** 不会。音频边收边在内存里分析，不保留，什么都不会离开你的电脑。

**它能在哪些 DAW 里用？** 能加载 VST3 或 Audio Unit 的宿主都可以尝试加载，但目前没有任何宿主有完整的测试记录。见
[docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md) 里的表。

**为什么 macOS 会拦它？** macOS 版没有签名和公证，下载下来的文件会被 Gatekeeper 拦住。使用指南里写了怎样解除。

**用了 Miciri 就不用处理房间了吗？** 不是。它只指出最可能有帮助的那一处改动，并给出数值，方便你自己核对改完的效果。

## 文档

- [使用指南](docs/USER_GUIDE.zh-CN.md)（[English](docs/USER_GUIDE.md)）：安装、每一步怎么做、词汇表、常见问题
- [已知问题](docs/KNOWN_ISSUES.md)、[隐私](docs/PRIVACY.zh-CN.md)
- [更新记录](CHANGELOG.md)、[第三方声明](THIRD-PARTY-NOTICES.md)、[安全政策](SECURITY.md)
- [架构](docs/ARCHITECTURE.md)、[契约](docs/contracts/)、[参与贡献](CONTRIBUTING.md)

## 相关项目

[SCVB](https://github.com/synchain-oss/scvb)：同一个团队做的多歌手人声自动声像与电平平衡插件。

## 许可证

[GPL-3.0-or-later](LICENSE)。第三方组件保留各自的许可证，见 [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)。由 [Synchain](https://synchain.ca) 制作。
