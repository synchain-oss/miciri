# SPDX-License-Identifier: GPL-3.0-or-later
# Derived from synchain-oss/rinne; see upstream/gates.json
#
# 本机 gates 的定档：按 diff 里的文件路径算出最低档，规则在 tier-rules.json。
#   Quick     文档、网页、i18n 与其余一切（格式、合规、隐私、提交、文档真源）
#   Standard  改 src、tests、tools、third_party、阈值文件（再加构建、ctest、pluginval、golden、分配计数器）
#   Full      改 CMake、CI、打包、mac 相关（再加页面冒烟与打包检查）
# 卡片声明的档位只能往上抬；选低了照跑，但 gates 记一条 FAIL。

function Get-TierRules {
    Get-Content -LiteralPath (Join-Path $PSScriptRoot 'tier-rules.json') -Raw | ConvertFrom-Json
}

function Get-TierRank([string]$Tier, $Rules) {
    $i = [array]::IndexOf([string[]]$Rules.tiers, $Tier)
    if ($i -lt 0) { throw "未知的档位 $Tier" }
    $i
}

# 返回最低档的名字；路径用正斜杠。命中 full 的最高，其次 standard，其余 Quick。
function Get-MinTier([string[]]$Files, $Rules) {
    $rank = 0
    foreach ($f in $Files) {
        foreach ($p in $Rules.full) { if ($f -match $p) { $rank = 2 } }
        if ($rank -lt 1) { foreach ($p in $Rules.standard) { if ($f -match $p) { $rank = 1 } } }
    }
    $Rules.tiers[$rank]
}

function Get-MaxTier([string[]]$Tiers, $Rules) {
    $best = 0
    foreach ($t in $Tiers) { if ($t) { $best = [Math]::Max($best, (Get-TierRank $t $Rules)) } }
    $Rules.tiers[$best]
}

Export-ModuleMember -Function Get-TierRules, Get-TierRank, Get-MinTier, Get-MaxTier
