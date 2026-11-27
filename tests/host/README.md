<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# 宿主测试（REAPER，Windows，VST3）

用真实的 REAPER 跑插件：走带、arm、监听 12 格，3 种渲染方式 × “通知插件离线状态”偏好的开关，开关编辑器窗口 20 次，
逐位直通，离线标志与分析次数的真值表，以及界面能否启动完成（开窗保持 10 秒，并在开窗后的几个时刻截图），和电平表、削波灯在有输入时的样子（循环喂合成信号，开窗保持并截图）。**只在本机跑，不进 CI**（CI 上没有 REAPER，也不该有）；结果写进 `docs/DAW_COMPATIBILITY.md`。

驱动是公开的 [reaper-harness](https://github.com/synchain-oss/reaper-harness)，**单独克隆、钉提交**，不复制进本仓。
插件是带探针的测试构建（CMake 选项 `MICIRI_HOST_PROBE=ON`，说明见 `docs/HOST_PROBE.md`），不得发布。

## 文件

| 文件 | 作用 |
|---|---|
| `../../reaper-harness.json` | 场景与判据：每个场景用 `report` 读探针 JSON，逐位直通由工具对比“不挂插件的渲染”得出；另有 5 个自带反例（判据放错必须变红） |
| `reaper-harness.commit` | 钉住的工具提交（40 位）；`run-host-matrix.ps1` 每次开跑前核对 |
| `build-probe.ps1` | 只建带探针的 `Miciri.vst3`，输出目录写在最后一行 |
| `make-feed.ps1` | 现场生成电平表场景要循环播放的两个合成 WAV（写在被忽略的 `build-host-feeds/` 下）；`run-host-matrix.ps1` 开跑前会调它 |
| `run-host-matrix.ps1` | 一条龙：核对钉版 → 构建 → 生成合成输入 → 确认没有 reaper.exe → 跑 → 打印每一行的判定与数值（含界面截图的张数与位置）→ 核对没有残留进程 |

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

- 全套约 15 分钟，启动 REAPER 三十次左右，**会在桌面上弹窗口**（编辑器开关 20 次会反复弹浮窗）。跑的时候别用自己的 REAPER：
  开跑前有 `reaper.exe` 在就直接拒绝，也不会替你结束它。需要让它让路时用 `-SkipMarker <文件>`：文件存在期间整套跳过（退出码 3），这时脚本不检查 `reaper.exe`。相对路径按你当前所在的目录解析。
- 只跑一部分：`-Only '<场景 id 的正则>'`，例如 `-Only '^render-'`、`-Only '^transport-12$'`、`-Only '^windows-20$'`、`-Only '^ui-start、`-Only '^neg-'`。
  第一次在新配置目录里启动 REAPER 会扫描插件，可能让第一次启动失败（INCOMPLETE），重跑即可；REAPER 的评估版提醒也会让单次启动偶尔 INCOMPLETE，工具会自动重试一次。
- 退出码：0 全 PASS，1 有 FAIL，2 有 INCOMPLETE（不算通过，先重跑，不要为了变绿去放宽判据），3 整套被跳过，64 被守卫或用法错误拒绝。全 PASS 但跑完仍有 reaper.exe 在时脚本改报 2；已经是别的非 0 码时保持原码，残留进程只打印。
- 工具的结果文件写在 `-ResultDir`（默认 `build-host-probe/host-results/`，已被 `.gitignore` 的 `build-*/` 覆盖），里面没有本机路径。

## 界面启动（`ui-start`）

别的场景只看音频和计数，不等页面。页面要等 WebView2 起来再加载，冷启动要几秒，所以这个场景让工具把插件浮窗开着不动（`holdMs` 10000，十秒），
再看测试构建的探针里界面计数在开窗前后的**增量**（计数从处理器创建起累计，不清零，所以不能判绝对值）：

| 字段 | 判据 | 说明 |
|---|---|---|
| `uiReady` | 增量 1 | 页面拿到第一份快照，完成启动 |
| `uiFallback` | 增量 0 | 没有切到兜底面板 |
| `bridgeRoundTrips` | 增量至少 1 | 页面调通过桥（兜底面板回的 not-ready 也计数，所以单看它不够，要和 `uiReady` 一起判） |
| `editorOpens`、`editorCloses` | 各增量 1 | 这十秒里窗口开了一次、关了一次 |

- 两个状态：`ui-cold-start` 是这个 REAPER 进程里第一次开窗，`ui-reopen` 是关掉再开一次。每个状态在开窗后 300、2000、8000 ms 各截一张图（只截插件浮窗的客户区，
  上面是 REAPER 自己的一行控件）。`lastIsNonRealtime` 是布尔值，不进 `fields`，也不拿来判。
- **图没截齐就是 INCOMPLETE**（工具判的，退出码 2），不当通过。图的内容不判：判据只看计数。
- 图写在 `<ResultDir>/renders/<时间戳>/run<轮次>/ui-start.shots/<状态>-<毫秒>.png`，脚本跑完会打印每个状态的张数和相对位置。
  **图只留在本机：不提交，不贴到 PR 或评论里**（默认的结果目录在 `build-*/` 下，已被 `.gitignore` 覆盖）。
- 反例：`neg-ui-ready-count-off-by-one` 把 `uiReady` 的期望写成 2，必须变红，说明判据读得到这个计数。更直接的反例是临时去掉 `src/plugin/CMakeLists.txt` 里对
  `check_native_interop.js` 的打包、只在本地构建、用它跑 `-Only '^ui-start$'`：页面的桥脚本加载失败，`uiReady` 增量 0、`uiFallback` 增量 1、`bridgeRoundTrips` 增量 0，
  场景变红，图里是兜底面板。跑完把改动还原，不提交。
- 十秒比页面的启动时间宽得多（本机 REAPER 7.82 上两秒的那张图里页面已经完整）。页面一直不动、又没来得及放弃的情况，由 `uiReady` 增量 0 判红，不靠 `uiFallback`。

## 先全零、后有信号的再开窗（`ui-signal-return`）

`ui-start` 的两个状态输入都是全零，看不出信号回来之后界面会不会自己恢复。这个场景在同一个 REAPER 进程里走两个状态，喂的是 `meter-steady` 那个合成正弦：

| 状态 | 走带、arm、监听 | 开窗保持与截图 | 该看到 |
|---|---|---|---|
| `ui-zero` | 停、不 arm、关（喂的信号进不来，插件收到全零） | 保持 10 秒，2000、8000 ms | 接通页“样本全是零”，“开始增益测试”是灰的 |
| `ui-signal` | 播放、arm、开（喂的信号进来了） | 保持 12 秒，300、2000、5000、8000、11000 ms | 再开窗落在接通页（不跳到增益）；后台的那一轮接通观察做完后（约 6 秒）变成“收到信号了”，左栏药丸转绿，按钮可点 |

- 判据和 `ui-start` 一样只看计数，另加输入的前提：`ui-zero` 里插件被调用、收到的全是静音，`ui-signal` 里每块都有信号、两路相同、监听生效。图的内容不判，要人看。
- 关窗时信号不正常，再开窗回到接通页、信号恢复后自己转绿，这些行为由页面冒烟（`web-preview/tests/smoke-flow-page.mjs` 的“关窗再开”）判；这个场景是在真宿主里看一遍。

## 电平表与削波灯（`meter-steady`、`meter-clip-lamp`）

别的场景里电平表没有输入，什么也不画。这两个场景在“走带播放、arm、监听开”的状态下，用工具的 `feed` 往输入循环喂一个合成 WAV（`channels: "same"`，单声道文件两路相同），
同时把窗口开着不动、定时截图，看电平表实际画成什么样。WAV 由 `make-feed.ps1` 现场算出，只含合成信号：

| 场景 | 信号 | 开窗保持与截图 | 该看到 |
|---|---|---|---|
| `meter-steady` | 1 kHz 正弦，峰值 −9 dBFS，整数个周期 | 保持 10 秒，2000、5000、8000 ms | 液面顶和峰值线约 −9，淡色内芯约 −12（正弦的均方根比峰值低 3 dB），两路相同，灯不亮 |
| `meter-clip-lamp` | 12 秒一圈：开头 50 ms 满幅方波，其余 −30 dBFS 的 1 kHz 正弦 | 保持 32 秒，每 4 秒一张，从 2000 到 30000 ms 共 8 张 | 每圈削波一次：灯亮 5 秒再熄，12 秒后再亮；削波后液面和内芯以每秒 30 dB 落回 −30、−33，峰值线先停住约 2 秒再落 |

- **工具判的只有这些**：窗口保持到时间、图拿全（没拿全是 INCOMPLETE）、`uiReady` 增量 1、`uiFallback` 增量 0、`bridgeRoundTrips` 增量至少 1，以及喂的信号确实到了输入（两路相同）。
  **电平表画得对不对不进通过判据**：探针不报电平，工具也不判图里的内容；从图里按像素读液面、峰值线、内芯和灯，要依赖主题颜色和版面尺寸，这些一改读数的办法就要跟着改，所以那是人看图或本机脚本的事，不是矩阵的红绿。
- 为什么 12 秒一圈、每 4 秒截一张：削波灯只在编辑器开着时才跟着读，打开之前的削波不会亮；灯亮 5 秒，截图间隔取 4 秒，保证每次削波至少落进一张亮着的图，而 12 秒的周期又让一次保持里有亮、有熄、有再亮。工具一个状态最多 8 张图。
- 合成信号的特征：削波用的是 ±满刻度的 16 位方波（连续多个样本过 0.999 的门限），不是实录，不含任何人声。

## 环境变量与插件的数据目录

脚本在本进程新建的临时目录里设 `USERPROFILE` 和 `WEBVIEW2_USER_DATA_FOLDER` 再启动工具，不重定向 `APPDATA`、`LOCALAPPDATA`：
工具的守卫用 `APPDATA` 判断私有目录不在 REAPER 默认目录里并列出它，扫描盾用 `LOCALAPPDATA` 找系统 VST3 目录，改掉它们守卫就去查一个假目录。
插件自己用 JUCE 的已知文件夹接口取 `%LOCALAPPDATA%\Synchain\Miciri`（WebView2 数据）与 `%APPDATA%\Synchain\Miciri`（界面默认值），不读这些环境变量，
改环境变量挡不住它。所以脚本把这两个目录跑前跑后各列一次（名字、大小、修改时间）并打印差异，如实报告，不删任何东西。
同一台机器上别的进程（比如另一个宿主里的 Miciri）也会改 WebView2 的数据目录，差异不一定来自这次运行。

## 改了兼容表之后

`docs/DAW_COMPATIBILITY.md` 里已测的行要有版本、日期、证据，超过 6 个月的行视为未验证。`scripts/checks.d/compat-table.mjs` 核对这些：
日常检查（`node scripts/run-checks.mjs --group docs`）里过期只提示 `WARN`，不判红；发布前用严格模式核对：

```powershell
$env:MICIRI_COMPAT_STRICT = '1'
node scripts/run-checks.mjs --only compat-table
```

`MICIRI_TODAY=YYYY-MM-DD` 可以指定“今天”，用来提前看到哪些行到期。

## 没有覆盖的

- 静音轨自动旁路（`force_auto_bypass`）：工具设不了，兼容表标“未测”。
- 分析次数 `analysisCount`：宿主测试的脚本不开始任何一步，所以只证明渲染不触发分析；拒绝开始由 `tests/session` 守着。开着编辑器时，信号还不正常界面就会自己开接通观察（停在哪一页都一样，还没到“信号正常”就一轮接一轮，关窗即取消），一轮做完会计入分析次数：`windows-20` 每次开窗都短于页面就绪再加一轮观察窗，所以增量仍判 0；开窗保持 10 秒以上的 `ui-start`、`meter-*` 不判这一项。
- 偏好关时“音频快于实时就作废”这一支、真实硬件输入、48 kHz / 512 以外的采样率与缓冲：见 `docs/DAW_COMPATIBILITY.md` 的“待手测”。
- 界面截图的内容：只判计数，不判图里画了什么，图要人看（电平表的读数同样是人看图，见上面的“电平表与削波灯”）；截图只在 96 DPI 下量过。
- Cubase、FL Studio、GarageBand / Logic、macOS 上的 REAPER：要在对应机器上手测。
`、`-Only '^ui-signal-return、`-Only '^neg-'`。
  第一次在新配置目录里启动 REAPER 会扫描插件，可能让第一次启动失败（INCOMPLETE），重跑即可；REAPER 的评估版提醒也会让单次启动偶尔 INCOMPLETE，工具会自动重试一次。
- 退出码：0 全 PASS，1 有 FAIL，2 有 INCOMPLETE（不算通过，先重跑，不要为了变绿去放宽判据），3 整套被跳过，64 被守卫或用法错误拒绝。全 PASS 但跑完仍有 reaper.exe 在时脚本改报 2；已经是别的非 0 码时保持原码，残留进程只打印。
- 工具的结果文件写在 `-ResultDir`（默认 `build-host-probe/host-results/`，已被 `.gitignore` 的 `build-*/` 覆盖），里面没有本机路径。

## 界面启动（`ui-start`）

别的场景只看音频和计数，不等页面。页面要等 WebView2 起来再加载，冷启动要几秒，所以这个场景让工具把插件浮窗开着不动（`holdMs` 10000，十秒），
再看测试构建的探针里界面计数在开窗前后的**增量**（计数从处理器创建起累计，不清零，所以不能判绝对值）：

| 字段 | 判据 | 说明 |
|---|---|---|
| `uiReady` | 增量 1 | 页面拿到第一份快照，完成启动 |
| `uiFallback` | 增量 0 | 没有切到兜底面板 |
| `bridgeRoundTrips` | 增量至少 1 | 页面调通过桥（兜底面板回的 not-ready 也计数，所以单看它不够，要和 `uiReady` 一起判） |
| `editorOpens`、`editorCloses` | 各增量 1 | 这十秒里窗口开了一次、关了一次 |

- 两个状态：`ui-cold-start` 是这个 REAPER 进程里第一次开窗，`ui-reopen` 是关掉再开一次。每个状态在开窗后 300、2000、8000 ms 各截一张图（只截插件浮窗的客户区，
  上面是 REAPER 自己的一行控件）。`lastIsNonRealtime` 是布尔值，不进 `fields`，也不拿来判。
- **图没截齐就是 INCOMPLETE**（工具判的，退出码 2），不当通过。图的内容不判：判据只看计数。
- 图写在 `<ResultDir>/renders/<时间戳>/run<轮次>/ui-start.shots/<状态>-<毫秒>.png`，脚本跑完会打印每个状态的张数和相对位置。
  **图只留在本机：不提交，不贴到 PR 或评论里**（默认的结果目录在 `build-*/` 下，已被 `.gitignore` 覆盖）。
- 反例：`neg-ui-ready-count-off-by-one` 把 `uiReady` 的期望写成 2，必须变红，说明判据读得到这个计数。更直接的反例是临时去掉 `src/plugin/CMakeLists.txt` 里对
  `check_native_interop.js` 的打包、只在本地构建、用它跑 `-Only '^ui-start$'`：页面的桥脚本加载失败，`uiReady` 增量 0、`uiFallback` 增量 1、`bridgeRoundTrips` 增量 0，
  场景变红，图里是兜底面板。跑完把改动还原，不提交。
- 十秒比页面的启动时间宽得多（本机 REAPER 7.82 上两秒的那张图里页面已经完整）。页面一直不动、又没来得及放弃的情况，由 `uiReady` 增量 0 判红，不靠 `uiFallback`。

## 电平表与削波灯（`meter-steady`、`meter-clip-lamp`）

别的场景里电平表没有输入，什么也不画。这两个场景在“走带播放、arm、监听开”的状态下，用工具的 `feed` 往输入循环喂一个合成 WAV（`channels: "same"`，单声道文件两路相同），
同时把窗口开着不动、定时截图，看电平表实际画成什么样。WAV 由 `make-feed.ps1` 现场算出，只含合成信号：

| 场景 | 信号 | 开窗保持与截图 | 该看到 |
|---|---|---|---|
| `meter-steady` | 1 kHz 正弦，峰值 −9 dBFS，整数个周期 | 保持 10 秒，2000、5000、8000 ms | 液面顶和峰值线约 −9，淡色内芯约 −12（正弦的均方根比峰值低 3 dB），两路相同，灯不亮 |
| `meter-clip-lamp` | 12 秒一圈：开头 50 ms 满幅方波，其余 −30 dBFS 的 1 kHz 正弦 | 保持 32 秒，每 4 秒一张，从 2000 到 30000 ms 共 8 张 | 每圈削波一次：灯亮 5 秒再熄，12 秒后再亮；削波后液面和内芯以每秒 30 dB 落回 −30、−33，峰值线先停住约 2 秒再落；具体哪几张图里灯亮取决于开窗相对循环的相位，每次运行不同 |

- **工具判的只有这些**：窗口保持到时间、图拿全（没拿全是 INCOMPLETE）、`uiReady` 增量 1、`uiFallback` 增量 0、`bridgeRoundTrips` 增量至少 1，以及喂的信号确实到了输入（两路相同）。
  **电平表画得对不对不进通过判据**：探针不报电平，工具也不判图里的内容；从图里按像素读液面、峰值线、内芯和灯，要依赖主题颜色和版面尺寸，这些一改读数的办法就要跟着改，所以那是人看图或本机脚本的事，不是矩阵的红绿。
- 为什么 12 秒一圈、每 4 秒截一张：削波灯只在编辑器开着时才跟着读，打开之前的削波不会亮；灯亮 5 秒，截图间隔取 4 秒，所以每次削波之后只要 5 秒内还有图要拍，就至少落进一张亮着的图，12 秒的周期又让一次保持里有亮、有熄、有再亮。开窗相对循环的相位每次运行不同，哪几张图里灯亮不固定（跑过的三次里序列有两种），所以不要把灯的序列当判据。工具一个状态最多 8 张图。
- 合成信号的特征：削波用的是 ±满刻度的 16 位方波（连续多个样本过 0.999 的门限），不是实录，不含任何人声。

## 环境变量与插件的数据目录

脚本在本进程新建的临时目录里设 `USERPROFILE` 和 `WEBVIEW2_USER_DATA_FOLDER` 再启动工具，不重定向 `APPDATA`、`LOCALAPPDATA`：
工具的守卫用 `APPDATA` 判断私有目录不在 REAPER 默认目录里并列出它，扫描盾用 `LOCALAPPDATA` 找系统 VST3 目录，改掉它们守卫就去查一个假目录。
插件自己用 JUCE 的已知文件夹接口取 `%LOCALAPPDATA%\Synchain\Miciri`（WebView2 数据）与 `%APPDATA%\Synchain\Miciri`（界面默认值），不读这些环境变量，
改环境变量挡不住它。所以脚本把这两个目录跑前跑后各列一次（名字、大小、修改时间）并打印差异，如实报告，不删任何东西。
同一台机器上别的进程（比如另一个宿主里的 Miciri）也会改 WebView2 的数据目录，差异不一定来自这次运行。

## 改了兼容表之后

`docs/DAW_COMPATIBILITY.md` 里已测的行要有版本、日期、证据，超过 6 个月的行视为未验证。`scripts/checks.d/compat-table.mjs` 核对这些：
日常检查（`node scripts/run-checks.mjs --group docs`）里过期只提示 `WARN`，不判红；发布前用严格模式核对：

```powershell
$env:MICIRI_COMPAT_STRICT = '1'
node scripts/run-checks.mjs --only compat-table
```

`MICIRI_TODAY=YYYY-MM-DD` 可以指定“今天”，用来提前看到哪些行到期。

## 没有覆盖的

- 静音轨自动旁路（`force_auto_bypass`）：工具设不了，兼容表标“未测”。
- 分析次数 `analysisCount`：宿主测试的脚本不开始任何一步，所以只证明渲染不触发分析；拒绝开始由 `tests/session` 守着。开着编辑器时，信号还不正常界面就会自己开接通观察（停在哪一页都一样，还没到“信号正常”就一轮接一轮，关窗即取消），一轮做完会计入分析次数：`windows-20` 每次开窗都短于页面就绪再加一轮观察窗，所以增量仍判 0；开窗保持 10 秒以上的 `ui-start`、`meter-*` 不判这一项。
- 偏好关时“音频快于实时就作废”这一支、真实硬件输入、48 kHz / 512 以外的采样率与缓冲：见 `docs/DAW_COMPATIBILITY.md` 的“待手测”。
- 界面截图的内容：只判计数，不判图里画了什么，图要人看（电平表的读数同样是人看图，见上面的“电平表与削波灯”）；截图只在 96 DPI 下量过。
- Cubase、FL Studio、GarageBand / Logic、macOS 上的 REAPER：要在对应机器上手测。
`、`-Only '^meter-'`、`-Only '^neg-'`。
  第一次在新配置目录里启动 REAPER 会扫描插件，可能让第一次启动失败（INCOMPLETE），重跑即可；REAPER 的评估版提醒也会让单次启动偶尔 INCOMPLETE，工具会自动重试一次。
- 退出码：0 全 PASS，1 有 FAIL，2 有 INCOMPLETE（不算通过，先重跑，不要为了变绿去放宽判据），3 整套被跳过，64 被守卫或用法错误拒绝。全 PASS 但跑完仍有 reaper.exe 在时脚本改报 2；已经是别的非 0 码时保持原码，残留进程只打印。
- 工具的结果文件写在 `-ResultDir`（默认 `build-host-probe/host-results/`，已被 `.gitignore` 的 `build-*/` 覆盖），里面没有本机路径。

## 界面启动（`ui-start`）

别的场景只看音频和计数，不等页面。页面要等 WebView2 起来再加载，冷启动要几秒，所以这个场景让工具把插件浮窗开着不动（`holdMs` 10000，十秒），
再看测试构建的探针里界面计数在开窗前后的**增量**（计数从处理器创建起累计，不清零，所以不能判绝对值）：

| 字段 | 判据 | 说明 |
|---|---|---|
| `uiReady` | 增量 1 | 页面拿到第一份快照，完成启动 |
| `uiFallback` | 增量 0 | 没有切到兜底面板 |
| `bridgeRoundTrips` | 增量至少 1 | 页面调通过桥（兜底面板回的 not-ready 也计数，所以单看它不够，要和 `uiReady` 一起判） |
| `editorOpens`、`editorCloses` | 各增量 1 | 这十秒里窗口开了一次、关了一次 |

- 两个状态：`ui-cold-start` 是这个 REAPER 进程里第一次开窗，`ui-reopen` 是关掉再开一次。每个状态在开窗后 300、2000、8000 ms 各截一张图（只截插件浮窗的客户区，
  上面是 REAPER 自己的一行控件）。`lastIsNonRealtime` 是布尔值，不进 `fields`，也不拿来判。
- **图没截齐就是 INCOMPLETE**（工具判的，退出码 2），不当通过。图的内容不判：判据只看计数。
- 图写在 `<ResultDir>/renders/<时间戳>/run<轮次>/ui-start.shots/<状态>-<毫秒>.png`，脚本跑完会打印每个状态的张数和相对位置。
  **图只留在本机：不提交，不贴到 PR 或评论里**（默认的结果目录在 `build-*/` 下，已被 `.gitignore` 覆盖）。
- 反例：`neg-ui-ready-count-off-by-one` 把 `uiReady` 的期望写成 2，必须变红，说明判据读得到这个计数。更直接的反例是临时去掉 `src/plugin/CMakeLists.txt` 里对
  `check_native_interop.js` 的打包、只在本地构建、用它跑 `-Only '^ui-start$'`：页面的桥脚本加载失败，`uiReady` 增量 0、`uiFallback` 增量 1、`bridgeRoundTrips` 增量 0，
  场景变红，图里是兜底面板。跑完把改动还原，不提交。
- 十秒比页面的启动时间宽得多（本机 REAPER 7.82 上两秒的那张图里页面已经完整）。页面一直不动、又没来得及放弃的情况，由 `uiReady` 增量 0 判红，不靠 `uiFallback`。

## 电平表与削波灯（`meter-steady`、`meter-clip-lamp`）

别的场景里电平表没有输入，什么也不画。这两个场景在“走带播放、arm、监听开”的状态下，用工具的 `feed` 往输入循环喂一个合成 WAV（`channels: "same"`，单声道文件两路相同），
同时把窗口开着不动、定时截图，看电平表实际画成什么样。WAV 由 `make-feed.ps1` 现场算出，只含合成信号：

| 场景 | 信号 | 开窗保持与截图 | 该看到 |
|---|---|---|---|
| `meter-steady` | 1 kHz 正弦，峰值 −9 dBFS，整数个周期 | 保持 10 秒，2000、5000、8000 ms | 液面顶和峰值线约 −9，淡色内芯约 −12（正弦的均方根比峰值低 3 dB），两路相同，灯不亮 |
| `meter-clip-lamp` | 12 秒一圈：开头 50 ms 满幅方波，其余 −30 dBFS 的 1 kHz 正弦 | 保持 32 秒，每 4 秒一张，从 2000 到 30000 ms 共 8 张 | 每圈削波一次：灯亮 5 秒再熄，12 秒后再亮；削波后液面和内芯以每秒 30 dB 落回 −30、−33，峰值线先停住约 2 秒再落 |

- **工具判的只有这些**：窗口保持到时间、图拿全（没拿全是 INCOMPLETE）、`uiReady` 增量 1、`uiFallback` 增量 0、`bridgeRoundTrips` 增量至少 1，以及喂的信号确实到了输入（两路相同）。
  **电平表画得对不对不进通过判据**：探针不报电平，工具也不判图里的内容；从图里按像素读液面、峰值线、内芯和灯，要依赖主题颜色和版面尺寸，这些一改读数的办法就要跟着改，所以那是人看图或本机脚本的事，不是矩阵的红绿。
- 为什么 12 秒一圈、每 4 秒截一张：削波灯只在编辑器开着时才跟着读，打开之前的削波不会亮；灯亮 5 秒，截图间隔取 4 秒，保证每次削波至少落进一张亮着的图，而 12 秒的周期又让一次保持里有亮、有熄、有再亮。工具一个状态最多 8 张图。
- 合成信号的特征：削波用的是 ±满刻度的 16 位方波（连续多个样本过 0.999 的门限），不是实录，不含任何人声。

## 环境变量与插件的数据目录

脚本在本进程新建的临时目录里设 `USERPROFILE` 和 `WEBVIEW2_USER_DATA_FOLDER` 再启动工具，不重定向 `APPDATA`、`LOCALAPPDATA`：
工具的守卫用 `APPDATA` 判断私有目录不在 REAPER 默认目录里并列出它，扫描盾用 `LOCALAPPDATA` 找系统 VST3 目录，改掉它们守卫就去查一个假目录。
插件自己用 JUCE 的已知文件夹接口取 `%LOCALAPPDATA%\Synchain\Miciri`（WebView2 数据）与 `%APPDATA%\Synchain\Miciri`（界面默认值），不读这些环境变量，
改环境变量挡不住它。所以脚本把这两个目录跑前跑后各列一次（名字、大小、修改时间）并打印差异，如实报告，不删任何东西。
同一台机器上别的进程（比如另一个宿主里的 Miciri）也会改 WebView2 的数据目录，差异不一定来自这次运行。

## 改了兼容表之后

`docs/DAW_COMPATIBILITY.md` 里已测的行要有版本、日期、证据，超过 6 个月的行视为未验证。`scripts/checks.d/compat-table.mjs` 核对这些：
日常检查（`node scripts/run-checks.mjs --group docs`）里过期只提示 `WARN`，不判红；发布前用严格模式核对：

```powershell
$env:MICIRI_COMPAT_STRICT = '1'
node scripts/run-checks.mjs --only compat-table
```

`MICIRI_TODAY=YYYY-MM-DD` 可以指定“今天”，用来提前看到哪些行到期。

## 没有覆盖的

- 静音轨自动旁路（`force_auto_bypass`）：工具设不了，兼容表标“未测”。
- 分析次数 `analysisCount`：宿主测试的脚本不开始任何一步，所以只证明渲染不触发分析；拒绝开始由 `tests/session` 守着。开着编辑器时，信号还不正常界面就会自己开接通观察（停在哪一页都一样，还没到“信号正常”就一轮接一轮，关窗即取消），一轮做完会计入分析次数：`windows-20` 每次开窗都短于页面就绪再加一轮观察窗，所以增量仍判 0；开窗保持 10 秒以上的 `ui-start`、`meter-*` 不判这一项。
- 偏好关时“音频快于实时就作废”这一支、真实硬件输入、48 kHz / 512 以外的采样率与缓冲：见 `docs/DAW_COMPATIBILITY.md` 的“待手测”。
- 界面截图的内容：只判计数，不判图里画了什么，图要人看（电平表的读数同样是人看图，见上面的“电平表与削波灯”）；截图只在 96 DPI 下量过。
- Cubase、FL Studio、GarageBand / Logic、macOS 上的 REAPER：要在对应机器上手测。
