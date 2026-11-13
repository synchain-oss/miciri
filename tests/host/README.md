<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# 宿主测试（REAPER，Windows，VST3）

用真实的 REAPER 跑插件：走带、arm、监听 12 格，3 种渲染方式 × “通知插件离线状态”偏好的开关，开关编辑器窗口 20 次，
逐位直通，离线标志与分析次数的真值表。**只在本机跑，不进 CI**（CI 上没有 REAPER，也不该有）；结果写进 `docs/DAW_COMPATIBILITY.md`。

驱动是公开的 [reaper-harness](https://github.com/synchain-oss/reaper-harness)，**单独克隆、钉提交**，不复制进本仓。
插件是带探针的测试构建（CMake 选项 `MICIRI_HOST_PROBE=ON`，说明见 `docs/HOST_PROBE.md`），不得发布。

## 文件

| 文件 | 作用 |
|---|---|
| `../../reaper-harness.json` | 场景与判据：每个场景用 `report` 读探针 JSON，逐位直通由工具对比“不挂插件的渲染”得出；另有 4 个自带反例（判据放错必须变红） |
| `reaper-harness.commit` | 钉住的工具提交（40 位）；`run-host-matrix.ps1` 每次开跑前核对 |
| `build-probe.ps1` | 只建带探针的 `Miciri.vst3`，输出目录写在最后一行 |
| `run-host-matrix.ps1` | 一条龙：核对钉版 → 构建 → 确认没有 reaper.exe → 跑 → 打印每一行的判定与数值 → 核对没有残留进程 |

## 准备（一次）

```powershell
git clone https://github.com/synchain-oss/reaper-harness.git <工具目录>
git -C <工具目录> checkout --detach (Get-Content tests/host/reaper-harness.commit)
pwsh <工具目录>/doctor.ps1 -CfgDir <新的私有配置目录> -Init
```

- 私有配置目录必须是新建的或之前由 `-Init` 初始化过的，不能是 REAPER 默认的资源目录（`%APPDATA%\REAPER`）或它下面的任何位置，工具会拒绝。
  不要把它、扫描缓存或结果的工作目录提交进任何仓库。
- 需要 REAPER（x64，默认安装位置）、PowerShell 7.4 以上、Visual Studio 的 C++ 构建工具和与 `.juce-commit` 一致的 JUCE 检出。
- 新钉一个工具提交后，在工具仓里先跑一次 `pwsh run.ps1 -SelfTest`（几分钟，不开 REAPER）。

## 跑

```powershell
$env:MICIRI_JUCE_PATH = '<JUCE 检出>'
pwsh tests/host/run-host-matrix.ps1 -HarnessDir <工具目录> -CfgDir <私有配置目录>
```

- 全套约 10 分钟，启动 REAPER 二十多次，**会在桌面上弹窗口**（编辑器开关 20 次会反复弹浮窗）。跑的时候别用自己的 REAPER：
  开跑前有 `reaper.exe` 在就直接拒绝，也不会替你结束它。需要让它让路时用 `-SkipMarker <文件>`：文件存在期间整套跳过（退出码 3），这时脚本不检查 `reaper.exe`。相对路径按你当前所在的目录解析。
- 只跑一部分：`-Only '<场景 id 的正则>'`，例如 `-Only '^render-'`、`-Only '^transport-12$'`、`-Only '^windows-20$'`、`-Only '^neg-'`。
  第一次在新配置目录里启动 REAPER 会扫描插件，可能让第一次启动失败（INCOMPLETE），重跑即可；REAPER 的评估版提醒也会让单次启动偶尔 INCOMPLETE，工具会自动重试一次。
- 退出码：0 全 PASS，1 有 FAIL，2 有 INCOMPLETE（不算通过，先重跑，不要为了变绿去放宽判据），3 整套被跳过，64 被守卫或用法错误拒绝。全 PASS 但跑完仍有 reaper.exe 在时脚本改报 2；已经是别的非 0 码时保持原码，残留进程只打印。
- 工具的结果文件写在 `-ResultDir`（默认 `build-host-probe/host-results/`，已被 `.gitignore` 的 `build-*/` 覆盖），里面没有本机路径。

## 环境变量与插件的数据目录

脚本在本进程新建的临时目录里设 `USERPROFILE` 和 `WEBVIEW2_USER_DATA_FOLDER` 再启动工具，不重定向 `APPDATA`、`LOCALAPPDATA`：
工具的守卫用 `APPDATA` 判断私有目录不在 REAPER 默认目录里并列出它，扫描盾用 `LOCALAPPDATA` 找系统 VST3 目录，改掉它们守卫就去查一个假目录。
插件自己用 JUCE 的已知文件夹接口取 `%LOCALAPPDATA%\Synchain\Miciri`（WebView2 数据）与 `%APPDATA%\Synchain\Miciri`（界面默认值），不读这些环境变量，
改环境变量挡不住它。所以脚本把这两个目录跑前跑后各列一次（名字、大小、修改时间）并打印差异，如实报告，不删任何东西。
同一台机器上别的进程（比如另一个宿主里的 Miciri）也会改 WebView2 的数据目录，差异不一定来自这次运行。

## 没有覆盖的

- 静音轨自动旁路（`force_auto_bypass`）：工具设不了，兼容表标“未测”。
- 分析次数 `analysisCount`：宿主测试里没有东西会“开始一步”，所以只证明渲染不触发分析；拒绝开始由 `tests/session` 守着。
- 偏好关时“音频快于实时就作废”这一支、真实硬件输入、48 kHz / 512 以外的采样率与缓冲、页面是否渲染成功：见 `docs/DAW_COMPATIBILITY.md` 的“待手测”。
- Cubase、FL Studio、GarageBand / Logic、macOS 上的 REAPER：要在对应机器上手测。
