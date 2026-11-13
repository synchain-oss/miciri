#Requires -Version 7.0
# SPDX-License-Identifier: GPL-3.0-or-later
#
# 宿主测试用的探针构建：只建带宿主探针的 VST3（CMake 选项 MICIRI_HOST_PROBE=ON），不跑 ctest，不打包。
# 这个构建只用于本机宿主测试，不得发布；发布版永远是 OFF（见 docs/HOST_PROBE.md）。
#   pwsh tests/host/build-probe.ps1 [-JucePath <目录>] [-BuildDir build-host-probe] [-Config Release]
# 成功时最后一行输出 VST3 目录（里面是 Miciri.vst3）；run-host-matrix.ps1 把它当 -PluginDir。
[CmdletBinding()]
param(
    [string]$JucePath = $env:MICIRI_JUCE_PATH,
    [string]$BuildDir = 'build-host-probe',
    [ValidateSet('Release', 'RelWithDebInfo')][string]$Config = 'Release'
)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $Root

if (-not $JucePath) { throw '需要 -JucePath 或环境变量 MICIRI_JUCE_PATH' }
$want = (Get-Content -LiteralPath '.juce-commit' -Raw).Trim()
$have = "$(git -C $JucePath rev-parse HEAD)".Trim()
if ($have -ne $want) { throw "JUCE 检出是 $have，应为 .juce-commit 的 $want" }

# 与 scripts/build.ps1 同一套：不在 VS 开发者环境里时按 vswhere 导入 vcvars64
if ($IsWindows -and -not (Get-Command cl -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw '找不到带 C++ 工具集的 Visual Studio' }
    $bat = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
    foreach ($line in (cmd /c "`"$bat`" >nul && set")) {
        if ($line -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
    }
}
if ($env:MICIRI_NINJA) { $env:PATH = (Split-Path -Parent $env:MICIRI_NINJA) + [IO.Path]::PathSeparator + $env:PATH }

function Invoke-Native([string]$Exe, [string[]]$Arguments) {
    & $Exe @Arguments
    if ($LASTEXITCODE) { throw "$Exe 失败（退出码 $LASTEXITCODE）" }
}
Invoke-Native cmake @('-S', '.', '-B', $BuildDir, '-G', 'Ninja', "-DCMAKE_BUILD_TYPE=$Config", '-DCMAKE_COLOR_DIAGNOSTICS=OFF',
    '-DMICIRI_WARNINGS_AS_ERRORS=ON', '-DMICIRI_CORE_ONLY=OFF', '-DMICIRI_SANITIZER=none', "-DJUCE_PATH=$JucePath", '-DMICIRI_HOST_PROBE=ON')
Invoke-Native cmake @('--build', $BuildDir, '--target', 'Miciri_VST3', '--parallel')

$bundle = Get-ChildItem -LiteralPath $BuildDir -Recurse -Directory -Filter 'Miciri.vst3' |
    Where-Object { $_.FullName -notmatch '[\\/]_deps[\\/]' } | Select-Object -First 1
if (-not $bundle) { throw "构建完成但找不到 Miciri.vst3（$BuildDir）" }
if (-not (Test-Path -LiteralPath (Join-Path $bundle.FullName 'Contents/Resources/moduleinfo.json'))) {
    throw 'Miciri.vst3 里没有 Contents/Resources/moduleinfo.json，reaper-harness 不能测它'
}
$bundle.Parent.FullName
