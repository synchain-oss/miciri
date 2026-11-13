#Requires -Version 7.4
# SPDX-License-Identifier: GPL-3.0-or-later
#
# REAPER 宿主矩阵（Windows、VST3），只在本机跑，不进 CI。用法与前提见 tests/host/README.md。
#   pwsh tests/host/run-host-matrix.ps1 -HarnessDir <reaper-harness 的克隆> -CfgDir <私有 REAPER 配置目录>
#       [-JucePath <目录>] [-PluginDir <含 Miciri.vst3 的目录，给了就不重建>] [-ResultDir <目录>]
#       [-SkipMarker <文件>]... [-Only <场景 id 的正则>] [-Repeat <n>]
# 做的事：核对 reaper-harness 的钉版提交 → 构建探针版插件 → 开跑前确认没有 reaper.exe → 在新建的临时目录里
# 设 USERPROFILE 与 WEBVIEW2_USER_DATA_FOLDER 再跑 reaper-harness → 把插件真实写入的目录跑前跑后各列一次 → 打印每一行的判定与数值。
# 退出码沿用 reaper-harness：0 全 PASS，1 有 FAIL，2 有 INCOMPLETE，3 整套被 SKIPPED，64 被守卫或用法错误拒绝；
# 其余都通过（0）时，跑完仍有 reaper.exe 在也报 2；已经是 1、2、3、64 时保持原码，残留进程只打印；插件数据目录的前后差异只报告，不当失败。
[CmdletBinding()]
param(
    [string]$HarnessDir = $env:MICIRI_REAPER_HARNESS,
    [string]$CfgDir = $env:MICIRI_REAPER_CFG,
    [string]$JucePath = $env:MICIRI_JUCE_PATH,
    [string]$PluginDir = '',
    [string]$ResultDir = '',
    [string[]]$SkipMarker = @(),
    [string]$Only = '',
    [int]$Repeat = 1
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
# 相对路径按调用者当前所在的目录解析（下面要进仓根）；路径不必已存在
function Resolve-Abs([string]$Path) { if ($Path) { $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path) } else { '' } }
$HarnessDir = Resolve-Abs $HarnessDir; $CfgDir = Resolve-Abs $CfgDir; $JucePath = Resolve-Abs $JucePath; $PluginDir = Resolve-Abs $PluginDir; $ResultDir = Resolve-Abs $ResultDir
$SkipMarker = @($SkipMarker | ForEach-Object { Resolve-Abs $_ })
Set-Location $Root

if (-not $HarnessDir) { throw '需要 -HarnessDir 或环境变量 MICIRI_REAPER_HARNESS（reaper-harness 的单独克隆，不放进本仓）' }
if (-not $CfgDir) { throw '需要 -CfgDir 或环境变量 MICIRI_REAPER_CFG（用 reaper-harness 的 doctor.ps1 -Init 建的私有配置目录，不是 REAPER 默认目录）' }

# 1. 钉版：每次结果都要对应到产生它的工具版本
$pin = (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'reaper-harness.commit') -Raw).Trim()
$have = "$(git -C $HarnessDir rev-parse HEAD)".Trim()
if ($have -ne $pin) { throw "reaper-harness 检出是 $have，应为 tests/host/reaper-harness.commit 的 $pin" }

# 2. 不与用户自己的 REAPER 抢：有 reaper.exe 就不开始（reaper-harness 自己也会拒绝）
function Get-ReaperPids { @(Get-Process -Name reaper -ErrorAction SilentlyContinue | ForEach-Object Id) }
# 跳过标记文件已存在时整套会被 reaper-harness 跳过、不启动 REAPER，用户自己的 REAPER 在不在都不碍事
$markerOn = @($SkipMarker | Where-Object { Test-Path -LiteralPath $_ }).Count -gt 0
if (-not $markerOn -and @(Get-ReaperPids).Count) { throw '已有 reaper.exe 在运行：不开始，也不替你结束它。先关掉它再试' }

# 3. 探针构建（只用于宿主测试，不得发布）
if (-not $PluginDir) {
    $built = @(& pwsh -NoProfile -File (Join-Path $PSScriptRoot 'build-probe.ps1') -JucePath $JucePath)
    if ($LASTEXITCODE) { $built | Select-Object -Last 20 | ForEach-Object { Write-Host $_ }; throw '探针版构建失败，见上面的输出' }
    $PluginDir = $built | Select-Object -Last 1
}
if (-not (Test-Path -LiteralPath (Join-Path $PluginDir 'Miciri.vst3/Contents/Resources/moduleinfo.json'))) { throw "$PluginDir 里没有可测的 Miciri.vst3" }
if (-not $ResultDir) { $ResultDir = Join-Path $Root 'build-host-probe/host-results' }
New-Item -ItemType Directory -Force -Path $ResultDir | Out-Null

# 4. 插件真实写入的目录的列表（名字、大小、修改时间）。插件用 JUCE 的已知文件夹接口取这两个目录，
#    不读 APPDATA、LOCALAPPDATA 环境变量，所以改环境变量挡不住它，只能前后对比、如实报告。不删任何东西。
function Get-DirListing([string]$Dir) {
    $map = @{}
    if (Test-Path -LiteralPath $Dir) {
        Get-ChildItem -LiteralPath $Dir -Recurse -Force -ErrorAction SilentlyContinue | Where-Object { -not $_.PSIsContainer } | ForEach-Object {
            $map[$_.FullName.Substring($Dir.Length).TrimStart('\')] = "$($_.Length)@$($_.LastWriteTimeUtc.Ticks)"
        }
    }
    $map
}
$watch = @{
    'LocalAppData/Synchain/Miciri' = (Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Synchain\Miciri')
    'AppData/Synchain/Miciri'      = (Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'Synchain\Miciri')
}
$before = @{}; foreach ($k in $watch.Keys) { $before[$k] = Get-DirListing $watch[$k] }

# 5. 本进程新建的临时目录：只给 USERPROFILE 与 WEBVIEW2_USER_DATA_FOLDER 用，目录由本进程新建，不做任何删除。
#    不重定向 APPDATA、LOCALAPPDATA：reaper-harness 的守卫用 $env:APPDATA 判断私有目录不在 REAPER 默认目录里并列出它，
#    扫描盾用 %LOCALAPPDATA% 找系统 VST3 目录；改掉它们，守卫就去查一个假目录，而 REAPER 自己仍按真实目录扫描。
$scratch = Join-Path ([IO.Path]::GetTempPath()) ('miciri-host-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null
$saved = @{ USERPROFILE = $env:USERPROFILE; WEBVIEW2_USER_DATA_FOLDER = $env:WEBVIEW2_USER_DATA_FOLDER }
$env:USERPROFILE = Join-Path $scratch 'profile'; New-Item -ItemType Directory -Path $env:USERPROFILE | Out-Null
$env:WEBVIEW2_USER_DATA_FOLDER = Join-Path $scratch 'webview2'; New-Item -ItemType Directory -Path $env:WEBVIEW2_USER_DATA_FOLDER | Out-Null

$runArgs = @('-NoProfile', '-File', (Join-Path $HarnessDir 'run.ps1'), '-Config', (Join-Path $Root 'reaper-harness.json'),
    '-CfgDir', $CfgDir, '-PluginDir', $PluginDir, '-ResultDir', $ResultDir, '-BuildLabel', 'host-matrix', '-Repeat', $Repeat)
foreach ($m in $SkipMarker) { $runArgs += @('-SkipMarker', $m) }
if ($Only) { $runArgs += @('-Only', $Only) }
$stamp = Get-Date
try { & pwsh @runArgs; $code = $LASTEXITCODE }
finally { foreach ($k in $saved.Keys) { Set-Item -Path "env:$k" -Value $saved[$k] } }

# 6. 跑后核对
$problems = @()
# 刚被结束的进程要一会儿才从列表里消失：最多等 15 秒
for ($i = 0; $i -lt 30 -and -not $markerOn -and @(Get-ReaperPids).Count; $i++) { Start-Sleep -Milliseconds 500 }
if (-not $markerOn -and @(Get-ReaperPids).Count) { $problems += "跑完仍有 reaper.exe 在：$((Get-ReaperPids) -join ',')（不替你结束它）" }
foreach ($k in $watch.Keys) {
    $after = Get-DirListing $watch[$k]; $changed = @()
    foreach ($f in $after.Keys) { if (-not $before[$k].ContainsKey($f)) { $changed += "added:$f" } elseif ($before[$k][$f] -ne $after[$f]) { $changed += "changed:$f" } }
    foreach ($f in $before[$k].Keys) { if (-not $after.ContainsKey($f)) { $changed += "removed:$f" } }
    Write-Host ("插件数据目录 {0}：跑前 {1} 个文件，跑后 {2} 个，差异 {3} 项" -f $k, $before[$k].Count, $after.Count, $changed.Count)
    $changed | Select-Object -First 8 | ForEach-Object { Write-Host "  $_" }
}
$result = Get-ChildItem -LiteralPath $ResultDir -Filter '*-miciri-host-host-matrix.json' -File | Where-Object { $_.LastWriteTime -ge $stamp } |
    Sort-Object LastWriteTime | Select-Object -Last 1
if ($result) {
    $r = Get-Content -LiteralPath $result.FullName -Raw | ConvertFrom-Json
    Write-Host ("REAPER {0}，判定 {1}，启动 {2} 次，工具提交 {3}" -f $r.reaper, $r.verdict, $r.launches, $pin.Substring(0, 7))
    $runs = @($r.runs)
    for ($n = 0; $n -lt $runs.Count; $n++) {
        if ($runs.Count -gt 1) { Write-Host "-- 第 $($n + 1) 轮 --" }
        foreach ($row in $runs[$n].scenarios) {
            Write-Host ("[{0}] {1} ({2:N1}s)" -f $row.status, $row.id, $row.seconds)
            foreach ($part in ($row.detail -split ' \| ')) { Write-Host "    $part" }
        }
    }
} else { $problems += '没找到这次的结果文件' }
foreach ($p in $problems) { Write-Host "问题：$p" }
if ($code -eq 0 -and $problems.Count) { $code = 2 }
exit $code
