#Requires -Version 7.0
# SPDX-License-Identifier: GPL-3.0-or-later
#
# 在当前工作树里装好本机的钩子与工具路径。只写本工作树自己的 git 配置（--worktree），不动全局配置，也不动别的工作树：
#   core.hooksPath          指向 scripts/hooks（commit-msg、pre-push）
#   miciri.node             钉版 Node（主版本见 .node-version；全局 Node 版本不对时必须给）
#   miciri.gitleaks / miciri.actionlint / miciri.reuse / miciri.clangFormat / miciri.prettier
#                           钉版工具的路径（prettier 给的是它的 prettier.cjs）
#   miciri.privacyLocal     个人隐私针文件的路径。只记下路径，这个脚本不读它的内容。
# 路径是本机的东西，只进 git 配置或环境变量，不进仓库里的任何文件。不给的参数保持原样。
#   pwsh scripts/setup-local.ps1 -Node <node> [-Gitleaks <f>] [-Actionlint <f>] [-Reuse <f>] [-ClangFormat <f>] [-Prettier <f>] [-PrivacyLocal <f>]
#   pwsh scripts/setup-local.ps1 -Show      列出当前设置（只打印是否已设，不打印路径）
[CmdletBinding()]
param(
    [string]$Node, [string]$Gitleaks, [string]$Actionlint, [string]$Reuse, [string]$ClangFormat, [string]$Prettier, [string]$PrivacyLocal,
    [switch]$Show
)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$keys = [ordered]@{
    'miciri.node' = $Node; 'miciri.gitleaks' = $Gitleaks; 'miciri.actionlint' = $Actionlint; 'miciri.reuse' = $Reuse
    'miciri.clangFormat' = $ClangFormat; 'miciri.prettier' = $Prettier; 'miciri.privacyLocal' = $PrivacyLocal
}

if ($Show) {
    $hp = "$(git -C $Root config --get core.hooksPath)".Trim()
    Write-Host ('core.hooksPath: ' + ($hp ? $hp : '未设'))
    foreach ($k in $keys.Keys) { Write-Host ('{0}: {1}' -f $k, ("$(git -C $Root config --get $k)".Trim() ? '已设' : '未设')) }
    exit 0
}

git -C $Root config extensions.worktreeConfig true
git -C $Root config --worktree core.hooksPath scripts/hooks
foreach ($k in $keys.Keys) {
    $v = $keys[$k]
    if (-not $v) { continue }
    if (-not (Test-Path -LiteralPath $v)) { throw "$k 指向的路径不存在" }
    git -C $Root config --worktree $k ((Resolve-Path -LiteralPath $v).Path -replace '\\', '/')
}
Write-Host '已写入本工作树的 git 配置。核对：pwsh scripts/setup-local.ps1 -Show'
