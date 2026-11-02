#Requires -Version 7.0
# SPDX-License-Identifier: GPL-3.0-or-later
# Derived from synchain-oss/rinne; see upstream/gates.json
#
# 构建、测试与验证的同一份配方：本机 gates 与 CI 的 build-win、build-mac、core-sanitizers 都调它。
#   pwsh scripts/build.ps1 [-JucePath <目录>] [-BuildDir build] [-Config Release] [-CoreOnly] [-Sanitizer asan_ubsan|tsan]
#                          [-NoBuild] [-Test] [-Golden] [-Canary] [-Validate] [-Stage <目录>]
# 不给 -Test / -Golden / -Canary / -Validate / -Stage 时只配置并构建；给了其中任何一个且带 -NoBuild，就只跑那几步。
# 与 CMake 的约定（CMake 由契约那张卡建立，建好后先对照这里核对、再改正）：
#   -DJUCE_PATH=<目录>、-DMICIRI_CORE_ONLY=ON|OFF（ON 时不需要 JUCE，只建 core 与它的测试）、
#   -DMICIRI_SANITIZER=none|asan_ubsan|tsan、-DMICIRI_WARNINGS_AS_ERRORS=ON（第一方零 warning）；
#   测试都登记进 ctest：DSP golden 带标签 golden，分配计数器与它的反例带标签 alloc。
# JUCE 只经 -JucePath 或环境变量 MICIRI_JUCE_PATH 传入，检出必须恰好是 .juce-commit。
# 退出码：0 通过；1 失败；2 没有东西可跑（没有 CMakeLists.txt、没有 golden 夹具等）：调用方记 SKIP 并写明原因，
# 不当作通过。CI 的 job 在没有 CMakeLists.txt 时自己判红，不靠这个退出码。
[CmdletBinding()]
param(
    [string]$JucePath = $env:MICIRI_JUCE_PATH,
    [string]$BuildDir = 'build',
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')][string]$Config = 'Release',
    [switch]$CoreOnly,
    [ValidateSet('', 'asan_ubsan', 'tsan')][string]$Sanitizer = '',
    [switch]$NoBuild, [switch]$Test, [switch]$Golden, [switch]$Canary, [switch]$Validate,
    [string]$Stage = ''
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

function Skip([string]$Why) { Write-Host "SKIP: $Why"; exit 2 }
function Invoke-Native([string]$Exe, [string[]]$Arguments) {
    & $Exe @Arguments
    if ($LASTEXITCODE) { throw "$Exe 失败（退出码 $LASTEXITCODE）" }
}

if (-not (Test-Path -LiteralPath 'CMakeLists.txt')) { Skip '没有 CMakeLists.txt，没有东西可构建' }

# Windows：不在 VS 开发者环境里时，按 vswhere 找到 VS 并导入 vcvars64 的环境变量
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

if (-not $NoBuild) {
    $args1 = @('-S', '.', '-B', $BuildDir, '-G', 'Ninja', "-DCMAKE_BUILD_TYPE=$Config", '-DCMAKE_COLOR_DIAGNOSTICS=OFF',
        '-DMICIRI_WARNINGS_AS_ERRORS=ON', "-DMICIRI_CORE_ONLY=$($CoreOnly ? 'ON' : 'OFF')",
        "-DMICIRI_SANITIZER=$($Sanitizer ? $Sanitizer : 'none')")
    if (-not $CoreOnly) {
        if (-not $JucePath) { throw '需要 -JucePath 或环境变量 MICIRI_JUCE_PATH' }
        $want = (Get-Content -LiteralPath '.juce-commit' -Raw).Trim()
        $have = "$(git -C $JucePath rev-parse HEAD)".Trim()
        if ($have -ne $want) { throw "JUCE 检出是 $have，应为 .juce-commit 的 $want" }
        $args1 += "-DJUCE_PATH=$JucePath"
    }
    Invoke-Native cmake $args1
    Invoke-Native cmake @('--build', $BuildDir, '--parallel')
}

if ($Test) {
    Invoke-Native ctest @('--test-dir', $BuildDir, '--output-on-failure', '--no-tests=error', '--timeout', '300')
}
if ($Golden) {
    if (-not (Test-Path -LiteralPath 'tests/golden/dsp')) { Skip 'DSP golden：tests/golden/dsp 还不存在' }
    Invoke-Native ctest @('--test-dir', $BuildDir, '-L', 'golden', '--output-on-failure', '--no-tests=error', '--timeout', '300')
}
if ($Canary) {
    if (-not (Test-Path -LiteralPath 'tests/alloc')) { Skip '分配计数器：tests/alloc 还不存在' }
    Invoke-Native ctest @('--test-dir', $BuildDir, '-L', 'alloc', '--output-on-failure', '--no-tests=error', '--timeout', '300')
}

function Get-Bundles([string]$Filter) {
    @(Get-ChildItem -LiteralPath $BuildDir -Recurse -Directory -Filter $Filter |
            Where-Object { $_.FullName -notmatch '[\\/]_deps[\\/]' })
}
if ($Validate) {
    if ($CoreOnly) { Skip '-CoreOnly 没有插件可验证' }
    $pv = $env:MICIRI_PLUGINVAL ? $env:MICIRI_PLUGINVAL : 'pluginval'
    $bundles = Get-Bundles '*.vst3'
    if (-not $bundles.Count) { Skip '构建目录里没有 .vst3，没有插件可验证' }
    foreach ($b in $bundles) {
        Invoke-Native $pv @('--strictness-level', '5', '--timeout-ms', '60000', '--skip-gui-tests', $b.FullName)
    }
}
if ($Stage) {
    $dest = [IO.Path]::GetFullPath($Stage)
    $null = New-Item -ItemType Directory -Force -Path $dest
    $all = @(Get-Bundles '*.vst3') + @(Get-Bundles '*.component')
    if (-not $all.Count) { throw '构建目录里没有可收集的插件' }
    foreach ($b in $all) { Copy-Item -LiteralPath $b.FullName -Destination $dest -Recurse }
}
