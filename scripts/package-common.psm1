# SPDX-License-Identifier: GPL-3.0-or-later
#
# 打包脚本共用的纯函数：版本与 tag 解析、产物命名、清单比对、目录哈希比对、构建缓存判据、校验和与发布说明的渲染。
# 这里不碰文件系统以外的东西（不起进程、不联网），所以都能在 package.ps1 -SelfTest 里直接测。
# scripts/package.ps1 与 scripts/check-release-tag.ps1 共用。
Set-StrictMode -Version 3.0

# 发行包里必须带的许可证全文（LICENSES/<SPDX>.txt）。平时只要求本项目自己的许可；发布（-Release）时
# 二进制里链接的每一份许可都必须在：JUCE 取 AGPL-3.0-only，pocketfft 与 WebView2 loader 是 BSD-3-Clause，
# macOS 的 AudioUnitSDK 是 Apache-2.0。
$script:BaseLicenses = @('GPL-3.0-or-later')
$script:ReleaseLicenses = @('AGPL-3.0-only', 'BSD-3-Clause')
$script:MacReleaseLicenses = @('Apache-2.0')

function Get-ProjectVersion([string]$CMakeText) {
    $m = [regex]::Match($CMakeText, '(?m)^\s*project\(\s*Miciri\s+VERSION\s+(\d+\.\d+\.\d+)[\s)]')
    if (-not $m.Success) { throw 'CMakeLists.txt 里没有 project(Miciri VERSION x.y.z …)' }
    $m.Groups[1].Value
}

# tag 只认 vX.Y.Z，可带预发布后缀（-rc.1）。Version 是去掉 v 和后缀的核心版本。
function Split-ReleaseTag([string]$Tag) {
    $m = [regex]::Match($Tag, '^v(\d+\.\d+\.\d+)(?:-([0-9A-Za-z][0-9A-Za-z.-]*))?$')
    if (-not $m.Success) { throw "tag「$Tag」不是 vX.Y.Z（可带预发布后缀）" }
    @{ Version = $m.Groups[1].Value; Suffix = $m.Groups[2].Value }
}

# 产物名固定，版本只来自工程版本。
function Get-ArtifactNames([string]$Version) {
    @{ Win = "Miciri-$Version-win64-setup.exe"; Mac = "Miciri-$Version-macos-arm64.pkg"; Sums = 'SHA256SUMS' }
}

# 清单格式：[区域] 之后每行一个根条目（目录以 / 结尾），行尾带 ? 的是「发布时才必须有」的可选项。
function Read-Manifest([string]$Text) {
    $m = [ordered]@{}
    $cur = $null
    foreach ($line in ($Text -split "`r?`n")) {
        $t = $line.Trim()
        if (-not $t -or $t.StartsWith('#')) { continue }
        if ($t -match '^\[([a-z0-9]+)\]$') { $cur = $Matches[1]; $m[$cur] = @(); continue }
        if (-not $cur) { throw '清单的第一个条目之前缺 [区域] 标题' }
        $m[$cur] += $t
    }
    $m
}

function Get-RootEntries([string]$Dir) {
    @(Get-ChildItem -LiteralPath $Dir -Force | Sort-Object Name | ForEach-Object { $_.PSIsContainer ? "$($_.Name)/" : $_.Name })
}

# 实际根条目与清单逐项比：少一个、多一个都算错；可选项在发布时按必须算。
function Compare-RootEntries([string[]]$Actual, [string[]]$Manifest, [bool]$Release, [string]$Area) {
    $errors = @()
    $want = @{}
    foreach ($e in $Manifest) { $want[$e.TrimEnd('?')] = $e.EndsWith('?') }
    foreach ($name in $want.Keys) {
        if (($Actual -cnotcontains $name) -and ((-not $want[$name]) -or $Release)) { $errors += "[$Area] 缺 $name" }
    }
    foreach ($a in $Actual) {
        if (-not $want.ContainsKey($a)) { $errors += "[$Area] 清单里没有 $a" }
    }
    , $errors
}

# 相对路径 → sha256 的表，路径用 /，哈希小写。
function Get-TreeHashes([string]$Dir) {
    $h = @{}
    $base = (Resolve-Path -LiteralPath $Dir).Path.TrimEnd('\', '/')
    foreach ($f in Get-ChildItem -LiteralPath $Dir -Recurse -File -Force) {
        $rel = $f.FullName.Substring($base.Length + 1).Replace('\', '/')
        $h[$rel] = (Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    $h
}

function Compare-Trees([hashtable]$Expected, [hashtable]$Actual, [string]$Label) {
    $errors = @()
    foreach ($k in ($Expected.Keys | Sort-Object)) {
        if (-not $Actual.ContainsKey($k)) { $errors += "[$Label] 缺文件 $k" }
        elseif ($Actual[$k] -ne $Expected[$k]) { $errors += "[$Label] 内容不一致 $k" }
    }
    foreach ($k in ($Actual.Keys | Sort-Object)) {
        if (-not $Expected.ContainsKey($k)) { $errors += "[$Label] 多出文件 $k" }
    }
    , $errors
}

# 发行包只许来自 Release 构建，探针开关必须是 OFF（缓存里没有这一项等于默认的 OFF）。
function Test-BuildCache([string]$CacheText) {
    $errors = @()
    $get = {
        param($name)
        $m = [regex]::Match($CacheText, ('(?m)^{0}(?::[A-Z]+)?=(.*?)\r?$' -f $name))
        if ($m.Success) { $m.Groups[1].Value } else { $null }
    }
    $probe = & $get 'MICIRI_HOST_PROBE'
    if ($null -ne $probe -and $probe -notmatch '^(OFF|FALSE|NO|N|0)$') { $errors += "构建目录的 MICIRI_HOST_PROBE 是 $probe，发行包必须是 OFF" }
    $type = & $get 'CMAKE_BUILD_TYPE'
    if ($type -cne 'Release') { $errors += "构建类型是「$type」，发行包必须是 Release" }
    $core = & $get 'MICIRI_CORE_ONLY'
    if ($core -match '^(ON|TRUE|YES|Y|1)$') { $errors += 'MICIRI_CORE_ONLY 是 ON：那个构建里没有插件' }
    , $errors
}

function Get-RequiredLicenses([bool]$Release, [bool]$Mac) {
    $need = @() + $script:BaseLicenses
    if ($Release) { $need += $script:ReleaseLicenses; if ($Mac) { $need += $script:MacReleaseLicenses } }
    , $need
}

# 模板里的 @NAME@ 全部替换；替换完还剩 @X@ 就报错，免得把占位符发给用户。
function Format-Template([string]$Template, [hashtable]$Vars) {
    $out = $Template
    foreach ($k in $Vars.Keys) { $out = $out.Replace("@$k@", [string]$Vars[$k]) }
    $left = [regex]::Match($out, '@[A-Z0-9_]+@')
    if ($left.Success) { throw "模板里有没替换的占位符 $($left.Value)" }
    $out
}

# sha256sum 格式（`sha256sum -c` 能直接校验）：LF 行尾，两个空格。
function Format-ShaSums([hashtable]$HashesByName) {
    $lines = $HashesByName.Keys | Sort-Object { $_ } -CaseSensitive | ForEach-Object { "$($HashesByName[$_])  $_" }
    ($lines -join "`n") + "`n"
}

# 汇总目录的清单校验：恰好是两个安装包，各自不能是空壳。Listing：名字 → 字节数，目录记 -1。
function Test-CollectListing([hashtable]$Listing, [string]$Version) {
    $n = Get-ArtifactNames $Version
    $want = @($n.Win, $n.Mac)
    $errors = @()
    foreach ($name in $want) {
        if (-not $Listing.ContainsKey($name)) { $errors += "缺产物 $name" }
        elseif ($Listing[$name] -lt 65536) { $errors += "$name 只有 $($Listing[$name]) 字节，像是空壳" }
    }
    foreach ($name in $Listing.Keys) {
        if ($want -cnotcontains $name) { $errors += "多出文件 $name：产物清单里没有它" }
    }
    , $errors
}

function New-ReleaseNotes([hashtable]$V) {
    $suffix = if ($V.Suffix) { " (pre-release $($V.Suffix))" } else { '' }
    @"
Miciri $($V.Tag)$suffix

- Windows x64: ``$($V.WinName)`` installs the VST3 plug-in into the standard VST3 folder.
- macOS arm64: ``$($V.MacName)`` installs the VST3 and Audio Unit plug-ins. The package is not signed or notarized.
- ``SHA256SUMS`` lists the checksum of each file; verify with ``sha256sum -c SHA256SUMS``.

License: Miciri is GPL-3.0-or-later. The binaries are distributed as a combined work under GPLv3, and the JUCE parts under AGPLv3. License texts and third-party notices are shipped inside each installer.

Corresponding source (GPLv3 section 6(d)):

- Miciri $($V.Version): https://github.com/synchain-oss/miciri at tag ``$($V.Tag)``, commit ``$($V.Commit)``
- JUCE $($V.JuceVersion): https://github.com/juce-framework/JUCE at commit ``$($V.JuceCommit)``
- Microsoft.Web.WebView2 $($V.WebView2) (binary loader linked into the Windows build, BSD-3-Clause)
- Build steps: INSTALL.txt inside each installer.
"@ + "`n"
}

function Invoke-PackageSelfTest {
    $fails = [System.Collections.Generic.List[string]]::new()
    $check = { param($label, $cond) if (-not $cond) { $fails.Add($label) } }
    $bad = { param($label, [scriptblock]$body) $ok = $false; try { & $body } catch { $ok = $true }; if (-not $ok) { $fails.Add("必须报错却没报：$label") } }
    $h = 'a' * 64

    & $check '读版本' ((Get-ProjectVersion "project(Miciri VERSION 1.2.3 LANGUAGES CXX)") -ceq '1.2.3')
    & $bad '版本缺失' { Get-ProjectVersion 'project(Other VERSION 1.2.3)' }
    & $bad '版本只有两段' { Get-ProjectVersion 'project(Miciri VERSION 1.2)' }
    & $check 'tag 核心版本' ((Split-ReleaseTag 'v0.1.0').Version -ceq '0.1.0')
    & $check 'tag 预发布后缀' ((Split-ReleaseTag 'v0.1.0-rc.1').Suffix -ceq 'rc.1')
    foreach ($t in @('0.1.0', 'v0.1', 'v0.1.0.0', 'v01.0.0x', 'v0.1.0-', 'release-1', 'v0.1.0 ', 'V0.1.0')) {
        & $bad "tag $t" ([scriptblock]::Create("Split-ReleaseTag '$t'"))
    }
    $n = Get-ArtifactNames '0.1.0'
    & $check '产物名固定' (($n.Win -ceq 'Miciri-0.1.0-win64-setup.exe') -and ($n.Mac -ceq 'Miciri-0.1.0-macos-arm64.pkg') -and ($n.Sums -ceq 'SHA256SUMS'))

    $mf = Read-Manifest "# c`n[app]`nA`nB/`nC?`n[vst3]`nV/"
    & $check '清单分区' (($mf.Keys -join ',') -ceq 'app,vst3' -and $mf['app'].Count -eq 3)
    & $bad '清单缺区域标题' { Read-Manifest "A`n" }
    & $check '清单吻合' ((Compare-RootEntries @('A', 'B/') $mf['app'] $false 'app').Count -eq 0)
    & $check '可选项出现也行' ((Compare-RootEntries @('A', 'B/', 'C') $mf['app'] $false 'app').Count -eq 0)
    & $check '少一项判错' ((Compare-RootEntries @('A') $mf['app'] $false 'app').Count -eq 1)
    & $check '多一项判错' ((Compare-RootEntries @('A', 'B/', 'Z') $mf['app'] $false 'app').Count -eq 1)
    & $check '发布时可选项变必须' ((Compare-RootEntries @('A', 'B/') $mf['app'] $true 'app').Count -eq 1)
    & $check '目录与文件不能互相冒充' ((Compare-RootEntries @('A', 'B') $mf['app'] $false 'app').Count -eq 2)

    & $check '目录哈希吻合' ((Compare-Trees @{ 'a' = $h } @{ 'a' = $h } 't').Count -eq 0)
    & $check '目录哈希：内容不同' ((Compare-Trees @{ 'a' = $h } @{ 'a' = ('b' * 64) } 't').Count -eq 1)
    & $check '目录哈希：缺文件' ((Compare-Trees @{ 'a' = $h } @{} 't').Count -eq 1)
    & $check '目录哈希：多文件' ((Compare-Trees @{} @{ 'a' = $h } 't').Count -eq 1)

    $good = "CMAKE_BUILD_TYPE:STRING=Release`nMICIRI_CORE_ONLY:BOOL=OFF`nMICIRI_HOST_PROBE:UNINITIALIZED=OFF`n"
    & $check '缓存：探针 OFF' ((Test-BuildCache $good).Count -eq 0)
    & $check '缓存：没有探针项也行' ((Test-BuildCache "CMAKE_BUILD_TYPE:STRING=Release`n").Count -eq 0)
    & $check '缓存：探针 ON 判错' ((Test-BuildCache ($good -replace 'PROBE:UNINITIALIZED=OFF', 'PROBE:BOOL=ON')).Count -eq 1)
    & $check '缓存：Debug 判错' ((Test-BuildCache ($good -replace '=Release', '=Debug')).Count -eq 1)
    & $check '缓存：没有构建类型判错' ((Test-BuildCache "MICIRI_CORE_ONLY:BOOL=OFF`n").Count -eq 1)
    & $check '缓存：只建 core 判错' ((Test-BuildCache ($good -replace 'CORE_ONLY:BOOL=OFF', 'CORE_ONLY:BOOL=ON')).Count -eq 1)

    & $check '许可：平时只要本项目的' ((Get-RequiredLicenses $false $false).Count -eq 1)
    & $check '许可：发布要 AGPL' ((Get-RequiredLicenses $true $false) -contains 'AGPL-3.0-only')
    & $check '许可：发布的 mac 要 Apache' ((Get-RequiredLicenses $true $true) -contains 'Apache-2.0')
    & $check '许可：发布的 Windows 不要 Apache' ((Get-RequiredLicenses $true $false) -notcontains 'Apache-2.0')

    & $check '模板替换' ((Format-Template 'v@V@ @V@' @{ V = '1' }) -ceq 'v1 1')
    & $bad '模板留下占位符' { Format-Template 'x @MISSING@' @{ V = '1' } }

    $sums = Format-ShaSums @{ 'b.pkg' = $h; 'a.exe' = ('c' * 64) }
    & $check '校验和格式' ($sums -ceq ("$('c' * 64)  a.exe`n$h  b.pkg`n"))
    & $check '校验和是 LF' (-not $sums.Contains("`r"))

    $ok = @{ $n.Win = 5MB; $n.Mac = 5MB }
    & $check '汇总：吻合' ((Test-CollectListing $ok '0.1.0').Count -eq 0)
    & $check '汇总：缺 mac 包' ((Test-CollectListing @{ $n.Win = 5MB } '0.1.0').Count -eq 1)
    & $check '汇总：多一个文件' ((Test-CollectListing ($ok + @{ 'extra.txt' = 10 }) '0.1.0').Count -eq 1)
    & $check '汇总：版本对不上算缺也算多' ((Test-CollectListing $ok '0.2.0').Count -eq 4)
    & $check '汇总：空壳' ((Test-CollectListing @{ $n.Win = 5MB; $n.Mac = 12 } '0.1.0').Count -eq 1)
    & $check '汇总：目录混进来' ((Test-CollectListing ($ok + @{ 'sub/' = -1 }) '0.1.0').Count -eq 1)

    $notes = New-ReleaseNotes @{ Tag = 'v0.1.0'; Version = '0.1.0'; Suffix = ''; WinName = $n.Win; MacName = $n.Mac; Commit = ('d' * 40); JuceVersion = '8.0.8'; JuceCommit = ('e' * 40); WebView2 = '1.0.2957.106' }
    & $check '说明含源码指引' ($notes.Contains($n.Win) -and $notes.Contains('d' * 40) -and $notes.Contains('e' * 40) -and $notes.Contains('1.0.2957.106'))
    & $check '说明里没有本机路径' ($notes -notmatch '[A-Za-z]:\\|/Users/|/home/')

    $fails.ToArray()
}

Export-ModuleMember -Function Get-ProjectVersion, Split-ReleaseTag, Get-ArtifactNames, Read-Manifest, Get-RootEntries, Compare-RootEntries, Get-TreeHashes, Compare-Trees, Test-BuildCache, Get-RequiredLicenses, Format-Template, Format-ShaSums, Test-CollectListing, New-ReleaseNotes, Invoke-PackageSelfTest
