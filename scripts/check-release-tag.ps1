#Requires -Version 7.0
# SPDX-License-Identifier: GPL-3.0-or-later
#
# 发版 tag 门禁（release.yml 的第一步，维护者在本机打 tag 前也可以先跑）：
#   pwsh scripts/check-release-tag.ps1 -Tag vX.Y.Z[-后缀] [-ProdRef origin/prod] [-SkipProd]
#   pwsh scripts/check-release-tag.ps1 -SelfTest
# ① tag 形如 vX.Y.Z，可带预发布后缀；② 核心版本必须等于 CMakeLists.txt 的工程版本；
# ③ 当前提交（HEAD，即被 tag 的提交）必须在 -ProdRef 的历史上：tag 只打在 prod 上。-SkipProd 只给本机预检用，跳过第三项。
# 调用前要有完整历史并已取到 prod（release.yml 里是 fetch-depth: 0 加 git fetch origin prod）。
[CmdletBinding()]
param(
    [string]$Tag = '', [string]$ProdRef = 'origin/prod', [switch]$SkipProd, [switch]$SelfTest
)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
Import-Module (Join-Path $PSScriptRoot 'package-common.psm1') -Force

function Test-ReleaseTag([string]$Tag, [string]$CMakeText) {
    $t = Split-ReleaseTag $Tag
    $v = Get-ProjectVersion $CMakeText
    if ($t.Version -cne $v) { throw "tag $Tag 的版本 $($t.Version) 与 CMakeLists.txt 的工程版本 $v 不一致" }
}

if ($SelfTest) {
    $fails = @()
    $cm = 'project(Miciri VERSION 0.1.0 LANGUAGES CXX)'
    $ok = { param($tag, $cmake) try { Test-ReleaseTag $tag $cmake; $true } catch { $false } }
    if (-not (& $ok 'v0.1.0' $cm)) { $fails += '版本一致的 tag 被拒了' }
    if (-not (& $ok 'v0.1.0-rc.1' $cm)) { $fails += '带预发布后缀的 tag 被拒了' }
    if (& $ok 'v0.1.1' $cm) { $fails += '版本不一致的 tag 被放行了' }
    if (& $ok '0.1.0' $cm) { $fails += '没有 v 前缀的 tag 被放行了' }
    if (& $ok 'v0.1' $cm) { $fails += '只有两段的 tag 被放行了' }
    if (& $ok 'v0.1.0' 'project(Other VERSION 0.1.0)') { $fails += '读不到工程版本却放行了' }
    if ($fails.Count) { $fails | ForEach-Object { Write-Host "自测未通过：$_" }; exit 1 }
    Write-Host 'check-release-tag 自测通过'
    exit 0
}

if (-not $Tag) { throw '需要 -Tag' }
Test-ReleaseTag $Tag (Get-Content -LiteralPath (Join-Path $Root 'CMakeLists.txt') -Raw)
if (-not $SkipProd) {
    git -C $Root merge-base --is-ancestor HEAD $ProdRef
    if ($LASTEXITCODE -ne 0) { throw "tag $Tag 指向的提交不在 $ProdRef 的历史上" }
}
Write-Host "tag $Tag 通过：形状、版本与分支都对"
