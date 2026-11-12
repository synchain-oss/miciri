#Requires -Version 7.0
# SPDX-License-Identifier: GPL-3.0-or-later
# Derived from synchain-oss/rinne; see upstream/gates.json
#
# 本地 gates 运行器。子 PR 上的 CI 不编译，构建、ctest 与 pluginval 只在这里跑。
#   pwsh scripts/gates.ps1 [-Quick | -Standard | -Full] [-CardTier <档>] [-Base origin/feature/<名>] [-Fetch]
#        [-ResultJson <文件> | -LogDir <目录>] [-JucePath <目录>] [-BuildDir build]
#   pwsh scripts/gates.ps1 -SelfTest
# 定档见 lib/Tiers.psm1 与 lib/tier-rules.json：<Base>...HEAD 的 diff 算出最低档，与 -CardTier 取高；不给档位就跑最低档，
# 给的档更低时照最低档跑，并另记一条 FAIL。-Base 只收 origin/feature/*。
#   Quick     G1 预检、G2 格式、G3 合规与隐私、G4 提交、G5 文档真源
#   Standard  + G6 构建、G7 ctest、G8 pluginval、G9 DSP golden、G10 分配计数器
#   Full      + G11 页面冒烟、G12 打包
# 没有东西可跑的 gate（还没有 CMakeLists.txt、golden 夹具，或本机没有 Inno Setup）记 SKIP 并写明原因，不算通过；EXIT 只看 FAIL。
# 开跑与收尾各读一次工作区，中途变脏判 FAIL。结果 json 写进 LogDir（缺省 MICIRI_LOG_DIR，再缺省仓内被忽略的 build/gates）：
# gates-<sha40>.json。最后一行固定为 `PASS n / FAIL n / SKIP n / FLAKY n  EXIT=x  @<sha7>  <档>  <耗时>`，不含路径。
# 本机工具：各取环境变量、再取 git config（仓内不写本机路径）：MICIRI_NODE / miciri.node、MICIRI_GITLEAKS、MICIRI_ACTIONLINT、
# MICIRI_REUSE、MICIRI_CLANG_FORMAT、MICIRI_PRETTIER（prettier.cjs）、MICIRI_PRIVACY_LOCAL、MICIRI_PLUGINVAL、MICIRI_NINJA。
# axe 冒烟的依赖目录（装有 axe-core 与 playwright，浏览器在其下 pw-browsers）：MICIRI_AXE_DEPS / miciri.axedeps；缺它时 G11 记 SKIP。
# 退出码：0 通过；1 有 FAIL 或工作区不干净。
[CmdletBinding()]
param(
    [switch]$Quick, [switch]$Standard, [switch]$Full, [switch]$SelfTest, [switch]$Fetch,
    [ValidateSet('', 'Quick', 'Standard', 'Full')][string]$CardTier = '',
    [string]$Base = 'origin/feature/v1', [string]$ResultJson = '', [string]$LogDir = $env:MICIRI_LOG_DIR,
    [string]$JucePath = $env:MICIRI_JUCE_PATH, [string]$BuildDir = 'build'
)

$ErrorActionPreference = 'Stop'
try { [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false) } catch { $null = $_ }
$OutputEncoding = [Text.UTF8Encoding]::new($false)
$Root = Split-Path -Parent $PSScriptRoot
Import-Module (Join-Path $PSScriptRoot 'lib/Tiers.psm1') -Force -ErrorAction Stop
$Rules = Get-TierRules

function Get-Setting([string]$EnvName, [string]$GitKey) {
    $v = [Environment]::GetEnvironmentVariable($EnvName)
    $v ? $v : "$(git -C $Root config --get $GitKey 2>$null)".Trim()
}
function Format-Duration([TimeSpan]$T) {
    $s = [int][Math]::Round($T.TotalSeconds)
    ($s -lt 60) ? "${s}s" : ('{0}m{1:00}s' -f [Math]::Floor($s / 60), ($s % 60))
}
function Format-Summary($Count, [int]$Exit, [string]$Sha, [string]$Tier, [TimeSpan]$T) {
    'PASS {0} / FAIL {1} / SKIP {2} / FLAKY {3}  EXIT={4}  @{5}  {6}  {7}' -f $Count.PASS, $Count.FAIL, $Count.SKIP, $Count.FLAKY,
    $Exit, ($Sha + '0000000').Substring(0, 7), $Tier, (Format-Duration $T)
}
# 打印前脱敏：仓根写成 .，用户目录写成 ~，两种斜杠都认。
function Format-Redacted([string]$Text) {
    foreach ($p in @(@($Root, '.'), @($HOME, '~'))) {
        if (-not $p[0]) { continue }
        foreach ($v in @(($p[0] -replace '/', '\'), ($p[0] -replace '\\', '/'))) { $Text = $Text -ireplace [regex]::Escape($v), $p[1] }
    }
    $Text
}
function New-Result([string]$Status, [string]$Reason = '', $Lines = @()) { @{ Status = $Status; Reason = $Reason; Lines = @($Lines) } }

# 起一个外部命令，合并 stdout 与 stderr；返回 @{ Code; Lines }。找不到命令返回 Code = -1。
function Invoke-Tool([string]$Exe, [string[]]$Arguments, [string]$Cwd = $Root) {
    if (-not (Get-Command $Exe -ErrorAction SilentlyContinue)) { return @{ Code = -1; Lines = @("找不到 $Exe") } }
    Push-Location $Cwd
    try { $out = & $Exe @Arguments 2>&1 | ForEach-Object { "$_" }; @{ Code = $LASTEXITCODE; Lines = @($out) } } finally { Pop-Location }
}
function Get-NodeExe { $n = Get-Setting 'MICIRI_NODE' 'miciri.node'; $n ? $n : 'node' }
function Invoke-Node([string[]]$Arguments) { Invoke-Tool (Get-NodeExe) $Arguments }
function Invoke-Build([string[]]$Arguments) {
    $a = @('-NoProfile', '-File', (Join-Path $PSScriptRoot 'build.ps1'), '-BuildDir', $BuildDir) + $Arguments
    if ($JucePath) { $a += @('-JucePath', $JucePath) }
    $r = Invoke-Tool (Get-Process -Id $PID).Path $a
    if ($r.Code -eq 2) { return New-Result 'SKIP' (($r.Lines | Where-Object { $_ -like 'SKIP:*' } | Select-Object -First 1) -replace '^SKIP:\s*', '') }
    if ($r.Code -ne 0) { return New-Result 'FAIL' "退出码 $($r.Code)" ($r.Lines | Select-Object -Last 25) }
    New-Result 'PASS'
}
# run-all 末行形如 `run-all：通过 a / 跳过 b / 失败 c（内部跳过项 d）`。两个跳过数缺一个就返回 $null，调用方按失败算，不当 0 处理
function Read-RunAllSummary([string]$Line) {
    if ($Line -notmatch '跳过 (\d+) /') { return $null }
    $file = [int]$Matches[1]
    if ($Line -notmatch '内部跳过项 (\d+)') { return $null }
    @{ File = $file; Inner = [int]$Matches[1] }
}
function Get-Gates {
    @(
        @{ Id = 'G1'; Tier = 'Quick'; Name = '预检（身份、hooks、Node）'; Run = {
                $bad = @()
                if ("$(git -C $Root config user.name)".Trim() -ne 'DLsnows') { $bad += 'user.name 不是 DLsnows' }
                if ("$(git -C $Root config user.email)".Trim() -ne 'noreply@synchain.ca') { $bad += 'user.email 不是 noreply@synchain.ca' }
                $hp = "$(git -C $Root config --get core.hooksPath)".Trim()
                if (-not $hp) { $bad += 'core.hooksPath 没设（pwsh scripts/setup-local.ps1）' }
                elseif (-not ((Test-Path -LiteralPath (Join-Path $Root "$hp/commit-msg")) -and (Test-Path -LiteralPath (Join-Path $Root "$hp/pre-push")))) { $bad += 'core.hooksPath 下缺 commit-msg 或 pre-push' }
                $v = Invoke-Node @('--version')
                $want = (Get-Content -LiteralPath (Join-Path $Root '.node-version') -Raw).Trim().Split('.')[0]
                if ($v.Code -ne 0 -or "$($v.Lines[0])" -notmatch "^v$want\.") { $bad += "Node 主版本应为 $want（设置 MICIRI_NODE）" }
                if ($bad) { return New-Result 'FAIL' ($bad -join '；') }
                New-Result 'PASS'
            } },
        @{ Id = 'G1b'; Tier = 'Quick'; Name = 'gh 账号是 DLsnows'; Run = {
                if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { return New-Result 'SKIP' '没有 gh' }
                $who = Invoke-Tool 'gh' @('api', 'user', '--jq', '.login')
                if ($who.Code -ne 0) { return New-Result 'SKIP' 'gh 查不到账号（离线？）' }
                if ("$($who.Lines[0])".Trim() -ne 'DLsnows') { return New-Result 'FAIL' 'gh 当前账号不是 DLsnows' }
                New-Result 'PASS'
            } },
        @{ Id = 'G2'; Tier = 'Quick'; Name = 'clang-format'; Run = {
                $cxx = @(git -C $Root ls-files -z '*.c' '*.cc' '*.cpp' '*.h' '*.hpp' '*.mm' | ForEach-Object { $_ -split "`0" } | Where-Object { $_ -and $_ -notmatch '^third_party/' })
                if (-not $cxx.Count) { return New-Result 'SKIP' '仓里还没有 C++ 源文件' }
                $cf = Get-Setting 'MICIRI_CLANG_FORMAT' 'miciri.clangFormat'
                $r = Invoke-Tool ($cf ? $cf : 'clang-format') (@('--dry-run', '--Werror', '--style=file') + $cxx)
                if ($r.Code -eq -1) { return New-Result 'SKIP' '没找到 clang-format（设置 MICIRI_CLANG_FORMAT）' }
                if ($r.Code -ne 0) { return New-Result 'FAIL' 'clang-format' ($r.Lines | Select-Object -First 10) }
                New-Result 'PASS'
            } },
        @{ Id = 'G2b'; Tier = 'Quick'; Name = 'prettier'; Run = {
                $pt = Get-Setting 'MICIRI_PRETTIER' 'miciri.prettier'
                if (-not $pt) { return New-Result 'SKIP' 'prettier 没配置（MICIRI_PRETTIER）；CI 会跑' }
                $r = Invoke-Node @($pt, '--check', '.')
                if ($r.Code -ne 0) { return New-Result 'FAIL' 'prettier' ($r.Lines | Select-Object -First 10) }
                New-Result 'PASS'
            } },
        @{ Id = 'G3'; Tier = 'Quick'; Name = '合规与隐私（SPDX、REUSE、离线、隐私）'; Run = {
                $r = Invoke-Node @('scripts/run-checks.mjs', '--group', 'compliance', '--needles', 'local')
                if ($r.Code -ne 0) { return New-Result 'FAIL' 'compliance 组' ($r.Lines | Select-Object -Last 25) }
                New-Result 'PASS'
            } },
        @{ Id = 'G3b'; Tier = 'Quick'; Name = 'gitleaks'; Run = {
                $gl = Get-Setting 'MICIRI_GITLEAKS' 'miciri.gitleaks'
                $g = Invoke-Tool ($gl ? $gl : 'gitleaks') @('dir', '.', '--no-banner', '--redact')
                if ($g.Code -eq -1) { return New-Result 'SKIP' '没找到 gitleaks（设置 MICIRI_GITLEAKS）；CI 会跑' }
                if ($g.Code -ne 0) { return New-Result 'FAIL' 'gitleaks' ($g.Lines | Select-Object -Last 15) }
                New-Result 'PASS'
            } },
        @{ Id = 'G4'; Tier = 'Quick'; Name = '提交（DCO、署名、格式、隐私）'; Run = {
                $r = Invoke-Node @('scripts/check-commits.mjs', 'commits', '--range', "$($script:BaseSha)..HEAD", '--tier', 'same', '--needles', 'local')
                if ($r.Code -ne 0) { return New-Result 'FAIL' '提交检查' ($r.Lines | Select-Object -Last 25) }
                New-Result 'PASS'
            } },
        @{ Id = 'G5'; Tier = 'Quick'; Name = '文档真源（workflow 加固、action 锁、钉版、自测）'; Run = {
                $a = Invoke-Node @('scripts/run-checks.mjs', '--self-test')
                if ($a.Code -ne 0) { return New-Result 'FAIL' '检查自测' ($a.Lines | Select-Object -Last 25) }
                $b = Invoke-Node @('scripts/run-checks.mjs', '--group', 'docs')
                if ($b.Code -ne 0) { return New-Result 'FAIL' 'docs 组' ($b.Lines | Select-Object -Last 25) }
                New-Result 'PASS'
            } },
        @{ Id = 'G6'; Tier = 'Standard'; Name = '构建（零 warning）'; Run = { Invoke-Build @() } },
        @{ Id = 'G7'; Tier = 'Standard'; Name = 'ctest'; Run = { Invoke-Build @('-NoBuild', '-Test') } },
        @{ Id = 'G8'; Tier = 'Standard'; Name = 'pluginval（strictness 5，无 GUI）'; Run = { Invoke-Build @('-NoBuild', '-Validate') } },
        @{ Id = 'G9'; Tier = 'Standard'; Name = 'DSP golden'; Run = { Invoke-Build @('-NoBuild', '-Golden') } },
        @{ Id = 'G10'; Tier = 'Standard'; Name = '分配计数器（alloc canary）'; Run = { Invoke-Build @('-NoBuild', '-Canary') } },
        @{ Id = 'G11'; Tier = 'Full'; Name = '页面冒烟（含 axe）'; Run = {
                if (-not (Test-Path -LiteralPath (Join-Path $Root 'web-preview/tests/run-all.mjs'))) { return New-Result 'SKIP' 'web-preview/tests/run-all.mjs 还不存在' }
                # axe 的依赖（axe-core、playwright、浏览器）装在仓外：有配置就传给子进程，用完还原。没装时 axe 冒烟会整套跳过
                $axe = Get-Setting 'MICIRI_AXE_DEPS' 'miciri.axedeps'
                $saved = @{}
                foreach ($k in 'MICIRI_AXE_DEPS', 'PLAYWRIGHT_BROWSERS_PATH') { $saved[$k] = [Environment]::GetEnvironmentVariable($k) }
                try {
                    if ($axe) {
                        [Environment]::SetEnvironmentVariable('MICIRI_AXE_DEPS', $axe)
                        [Environment]::SetEnvironmentVariable('PLAYWRIGHT_BROWSERS_PATH', (Join-Path $axe 'pw-browsers'))
                    }
                    $r = Invoke-Node @('web-preview/tests/run-all.mjs')
                } finally {
                    foreach ($k in $saved.Keys) { [Environment]::SetEnvironmentVariable($k, $saved[$k]) }
                }
                if ($r.Code -ne 0) { return New-Result 'FAIL' '页面冒烟' ($r.Lines | Select-Object -Last 25) }
                # 有脚本整套跳过或套内跳过项时不能记 PASS：写明是哪一项、为什么
                $sum = @($r.Lines | Where-Object { $_ -match '^run-all：' }) | Select-Object -Last 1
                if (-not $sum) { return New-Result 'FAIL' 'run-all 没有输出汇总行' ($r.Lines | Select-Object -Last 10) }
                $skips = Read-RunAllSummary $sum
                if (-not $skips) { return New-Result 'FAIL' 'run-all 汇总行格式无法解析，不能确认有没有跳过' @($sum) }
                if ($skips.File + $skips.Inner -gt 0) {
                    $why = @($r.Lines | Where-Object { $_ -match '^\[跳过' })
                    return New-Result 'SKIP' "页面冒烟有跳过（整套 $($skips.File)、套内 $($skips.Inner)），不记通过" ($why + $sum)
                }
                # 记 PASS 就要能看出 axe 真的跑了：没有它的通过记录（脚本被删、被过滤）按失败算
                $axeLine = @($r.Lines | Where-Object { $_ -match '^\[通过\] smoke-axe-page\.mjs' })
                if (-not $axeLine) { return New-Result 'FAIL' 'run-all 输出里没有 axe 冒烟的通过记录' ($r.Lines | Select-Object -Last 10) }
                New-Result 'PASS' '' ($axeLine + $sum)
            } },
        @{ Id = 'G12'; Tier = 'Full'; Name = '打包'; Run = {
                $pw = (Get-Process -Id $PID).Path
                foreach ($script in 'package.ps1', 'check-release-tag.ps1') {
                    $st = Invoke-Tool $pw @('-NoProfile', '-File', (Join-Path $Root "scripts/$script"), '-SelfTest')
                    if ($st.Code -ne 0) { return New-Result 'FAIL' "$script 自测" ($st.Lines | Select-Object -Last 25) }
                }
                # 本机不带 -Verify：静默安装要管理员权限，由 CI 验证。没有 Inno Setup 时脚本停在编译之前并退出 2，
                # 此时暂存目录与清单已经核过，gate 记 SKIP 并写明编译留给 CI
                $r = Invoke-Tool $pw @('-NoProfile', '-File', (Join-Path $Root 'scripts/package.ps1'), '-BuildDir', $BuildDir)
                if ($r.Code -eq 2) { return New-Result 'SKIP' (($r.Lines | Where-Object { $_ -like 'SKIP:*' } | Select-Object -First 1) -replace '^SKIP:\s*', '') }
                if ($r.Code -ne 0) { return New-Result 'FAIL' "退出码 $($r.Code)" ($r.Lines | Select-Object -Last 25) }
                New-Result 'PASS'
            } }
    )
}

function Invoke-SelfTest {
    $ok = $true
    $check = { param($label, $cond) if (-not $cond) { Write-Host "自测未通过：$label"; $script:ok = $false } }
    $min = { param($files) Get-MinTier $files $Rules }
    & $check '文档只要 Quick' ((& $min @('README.md', 'docs/a.md', 'web/app.js')) -ceq 'Quick')
    & $check '改 src 要 Standard' ((& $min @('README.md', 'src/core/level/a.cpp')) -ceq 'Standard')
    & $check '改阈值要 Standard' ((& $min @('thresholds.json')) -ceq 'Standard')
    & $check '改 CMake 要 Full' ((& $min @('src/a.cpp', 'CMakeLists.txt')) -ceq 'Full')
    & $check '改 workflow 要 Full' ((& $min @('.github/workflows/build.yml')) -ceq 'Full')
    & $check '改打包要 Full' ((& $min @('packaging/win/a.iss')) -ceq 'Full')
    & $check '卡片只能往上抬' ((Get-MaxTier @('Full', 'Quick') $Rules) -ceq 'Full')
    $line = Format-Summary @{ PASS = 5; FAIL = 0; SKIP = 3; FLAKY = 0 } 0 ('a' * 40) 'Standard' ([TimeSpan]::FromSeconds(185))
    & $check '汇总行格式' ($line -ceq 'PASS 5 / FAIL 0 / SKIP 3 / FLAKY 0  EXIT=0  @aaaaaaa  Standard  3m05s')
    & $check '脱敏：仓根写成 .' ((Format-Redacted "$Root\scripts\x.ps1") -ceq '.\scripts\x.ps1')
    $sk = Read-RunAllSummary 'run-all：通过 9 / 跳过 1 / 失败 0（内部跳过项 2）'
    & $check 'run-all 汇总行：读出整套与套内跳过数' ($sk -and $sk.File -eq 1 -and $sk.Inner -eq 2)
    & $check 'run-all 汇总行：缺字段时返回 $null，不当 0' (-not (Read-RunAllSummary 'run-all：通过 9 / 失败 0') -and -not (Read-RunAllSummary 'run-all：通过 9 / 跳过 0 / 失败 0'))
    $runAll = Join-Path $Root 'web-preview/tests/run-all.mjs'
    & $check 'run-all.mjs 仍按解析器认的格式写汇总行' ((-not (Test-Path -LiteralPath $runAll)) -or ((Get-Content -LiteralPath $runAll -Raw -Encoding utf8) -match [regex]::Escape('跳过 ${count.skip} / 失败 ${count.fail}（内部跳过项 ${inner.length}）')))
    $ids = @(Get-Gates | ForEach-Object { $_.Id })
    & $check 'gate 编号不重复' ($ids.Count -eq ($ids | Sort-Object -Unique).Count)
    & $check '每个 gate 的档位都在规则里' (@(Get-Gates | Where-Object { $_.Tier -notin $Rules.tiers }).Count -eq 0)
    if ($ok) { Write-Host 'gates 自测通过' }
    exit ($ok ? 0 : 1)
}
if ($SelfTest) { Invoke-SelfTest }

# ---- 定档 ----
$started = Get-Date
$results = [System.Collections.Generic.List[object]]::new()
$count = @{ PASS = 0; FAIL = 0; SKIP = 0; FLAKY = 0 }
function Add-Gate([string]$Id, [string]$Name, [string]$Tier, $R, [double]$Seconds) {
    $count[$R.Status]++
    $results.Add([ordered]@{ id = $Id; name = $Name; tier = $Tier; status = $R.Status; reason = $R.Reason; seconds = [Math]::Round($Seconds, 1) })
    Write-Host ('{0,-5} {1} {2}{3}' -f $R.Status, $Id, $Name, ($R.Reason ? "：$(Format-Redacted $R.Reason)" : ''))
    foreach ($l in $R.Lines) { Write-Host "      $(Format-Redacted ([string]$l))" }
}

$sha = "$(git -C $Root rev-parse HEAD)".Trim()
$dirty0 = [bool]"$(git -C $Root status --porcelain)".Trim()
$requested = if ($Full) { 'Full' } elseif ($Standard) { 'Standard' } elseif ($Quick) { 'Quick' } else { '' }
$setupProblem = ''
if ($Base -notmatch '^origin/feature/[\w.-]+$') { $setupProblem = "-Base 只收 origin/feature/*：$Base" }
if (-not $setupProblem -and $Fetch) { git -C $Root fetch --quiet origin ($Base -replace '^origin/', '') }
$script:BaseSha = ''
if (-not $setupProblem) {
    $script:BaseSha = "$(git -C $Root rev-parse --verify --quiet "$Base^{commit}")".Trim()
    if (-not $script:BaseSha) { $setupProblem = "找不到基线 $Base（先 git fetch，或加 -Fetch）" }
}
$files = @()
if (-not $setupProblem) { $files = @(git -C $Root diff --name-only "$($script:BaseSha)...HEAD" | Where-Object { $_ }) }
$autoTier = Get-MinTier $files $Rules
$minTier = Get-MaxTier @($autoTier, $CardTier) $Rules
$runTier = Get-MaxTier @($minTier, $requested) $Rules
$minRank = Get-TierRank $minTier $Rules
Write-Host ("档位：自动 {0}，卡片 {1}，最低 {2}，运行 {3}（{4} 个改动文件）" -f $autoTier, ($CardTier ? $CardTier : '-'), $minTier, $runTier, $files.Count)

if ($setupProblem) { Add-Gate 'G0' '定档' 'Quick' (New-Result 'FAIL' $setupProblem) 0 }
elseif ($requested -and (Get-TierRank $requested $Rules) -lt $minRank) {
    Add-Gate 'G0' '定档' 'Quick' (New-Result 'FAIL' "指定的 $requested 低于最低档 $minTier，已按 $runTier 跑") 0
}
$runRank = Get-TierRank $runTier $Rules
foreach ($g in Get-Gates) {
    if ((Get-TierRank $g.Tier $Rules) -gt $runRank) { continue }
    if ($setupProblem -and $g.Id -eq 'G4') { Add-Gate $g.Id $g.Name $g.Tier (New-Result 'SKIP' '没有基线，无法取提交区间') 0; continue }
    $t0 = Get-Date
    try { $r = & $g.Run } catch { $r = New-Result 'FAIL' $_.Exception.Message }
    Add-Gate $g.Id $g.Name $g.Tier $r ((Get-Date) - $t0).TotalSeconds
}

$sha1 = "$(git -C $Root rev-parse HEAD)".Trim()
$dirty1 = [bool]"$(git -C $Root status --porcelain)".Trim()
$dirty = $dirty0 -or $dirty1 -or ($sha1 -ne $sha)
if ($dirty) { Add-Gate 'G-dirty' '工作区在运行前后保持干净且 HEAD 不变' 'Quick' (New-Result 'FAIL' '工作区不干净，或运行期间 HEAD 变了') 0 }
$exit = ($count.FAIL -gt 0 -or $dirty) ? 1 : 0
$finished = Get-Date

$target = $ResultJson
if (-not $target) {
    $dir = $LogDir ? $LogDir : (Join-Path $Root 'build/gates')
    $null = New-Item -ItemType Directory -Force -Path $dir
    $target = Join-Path ([IO.Path]::GetFullPath($dir)) "gates-$sha.json"
}
[ordered]@{
    sha = $sha; base = $Base; baseSha = $script:BaseSha; autoTier = $autoTier; cardTier = $CardTier; minTier = $minTier; tier = $runTier
    dirty = $dirty; pass = $count.PASS; fail = $count.FAIL; skip = $count.SKIP; flaky = $count.FLAKY; exit = $exit
    started = $started.ToUniversalTime().ToString('o'); finished = $finished.ToUniversalTime().ToString('o'); gates = $results
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $target -Encoding utf8

Write-Output (Format-Summary $count $exit $sha $runTier ($finished - $started))
exit $exit
