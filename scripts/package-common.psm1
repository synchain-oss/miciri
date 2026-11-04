# SPDX-License-Identifier: GPL-3.0-or-later
# Derived from synchain-oss/rinne; see upstream/packaging.json
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

# 产物名固定，版本只来自工程版本；四个产物同一个前缀 Miciri-v<版本>-，与家族其他产品的命名一致。
function Get-ArtifactNames([string]$Version) {
    @{
        Win    = "Miciri-v$Version-win64-setup.exe"
        Mac    = "Miciri-v$Version-macos-arm64.pkg"
        WinZip = "Miciri-v$Version-win64.zip"
        MacZip = "Miciri-v$Version-macos-arm64.zip"
        Sums   = 'SHA256SUMS'
    }
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
    # CMake 认的「假」：0、OFF、NO、FALSE、N、IGNORE、NOTFOUND、空串、以 -NOTFOUND 结尾，大小写不分。其余一律按 ON 处理（宁可误拒）
    if ($null -ne $probe -and $probe -notmatch '(?i)^(0|OFF|NO|FALSE|N|IGNORE|NOTFOUND|.*-NOTFOUND)?$') {
        $errors += "构建目录的 MICIRI_HOST_PROBE 是「$probe」：这是宿主探针的测试构建（探针只供宿主测试，不得发布），发行包必须是 OFF"
    }
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

# 汇总目录的清单校验：恰好是四个产物（两个安装包、两个 zip），各自不能是空壳。Listing：名字 → 字节数，目录记 -1。
function Test-CollectListing([hashtable]$Listing, [string]$Version) {
    $n = Get-ArtifactNames $Version
    $want = @($n.Win, $n.Mac, $n.WinZip, $n.MacZip)
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

## Downloads

- Installers, recommended if you are new to plug-ins. They copy the plug-in into the standard folders for you.
  - Windows x64: ``$($V.WinName)`` installs the VST3.
  - macOS arm64: ``$($V.MacName)`` installs the VST3 and the Audio Unit. It is not signed or notarized.
- Zip files, for people who manage their plug-in folders themselves. They hold only the plug-in files and an INSTALL.txt that says where to copy them. On macOS, INSTALL.txt also has the command that clears the quarantine flag.
  - Windows x64: ``$($V.WinZipName)``
  - macOS arm64: ``$($V.MacZipName)``
- ``SHA256SUMS`` lists the checksum of each file. Verify with ``sha256sum -c SHA256SUMS``.

## 下载

- 安装包：新手用这个。它把插件装到标准目录。
  - Windows x64：``$($V.WinName)``，安装 VST3，安装向导有简体中文。
  - macOS arm64：``$($V.MacName)``，安装 VST3 和 AU。没有签名和公证。
- zip：熟悉插件目录的人用这个。里面只有插件文件和 INSTALL.txt，自己复制到插件目录。macOS 的 INSTALL.txt 还写了解除隔离的命令。
  - Windows x64：``$($V.WinZipName)``
  - macOS arm64：``$($V.MacZipName)``
- ``SHA256SUMS`` 记录每个文件的校验值，用 ``sha256sum -c SHA256SUMS`` 核对。

## License

Miciri is GPL-3.0-or-later. The binaries are distributed as a combined work under GPLv3, and the JUCE parts under AGPLv3. License texts and third-party notices are shipped inside every installer and zip.

Corresponding source (GPLv3 section 6(d)):

- Miciri $($V.Version): https://github.com/synchain-oss/miciri at tag ``$($V.Tag)``, commit ``$($V.Commit)``
- JUCE $($V.JuceVersion): https://github.com/juce-framework/JUCE at commit ``$($V.JuceCommit)``
- Microsoft.Web.WebView2 $($V.WebView2) (binary loader linked into the Windows build, BSD-3-Clause)
- Build steps: INSTALL.txt inside each installer and zip.
"@ + "`n"
}

# DOS 时间的下限是 1980-01-01、精度 2 秒：取整到偶数秒，读回来的时间才与写进去的一致。
# 取值顺序：SOURCE_DATE_EPOCH > 当前提交的时间 > 1980-01-01。
function Get-ReproEpoch([string]$FromEnv, [long]$HeadTime) {
    $e = if ($FromEnv -match '^[0-9]+$') { [long]$FromEnv } elseif ($HeadTime -gt 0) { $HeadTime } else { 0 }
    $e = [Math]::Max($e, 315532800) # 1980-01-01T00:00:00Z，恰为偶数
    $e - ($e % 2)
}

# macOS 的 zip 里每个条目的 Unix 权限写死：Contents/MacOS 下可执行（0755），其余 0644。0x8000 是「普通文件」类型位。
function Get-ZipUnixAttributes([string]$RelPath) {
    $mode = if ($RelPath -clike '*/Contents/MacOS/*') { 493 } else { 420 } # 0755 / 0644
    (0x8000 -bor $mode) -shl 16
}

# 可重现的 zip：只写文件（不写目录条目），按序数排序，时间与属性写死，压缩级别固定。
# 同一份输入、同一个 .NET 运行时下重打，字节一致。Unix = true 写 Unix 权限（macOS 用），否则写 DOS 的「存档」属性 32。
# 条目时间取 UTC 的墙上时间：DateTimeOffset 的偏移为 0，.DateTime 就是 UTC 值，换时区不变。
function New-ReproZip([string]$SourceDir, [string]$ZipPath, [long]$Epoch, [bool]$Unix) {
    Add-Type -AssemblyName System.IO.Compression
    $base = (Resolve-Path -LiteralPath $SourceDir).Path.TrimEnd('\', '/')
    $byRel = @{}
    foreach ($f in Get-ChildItem -LiteralPath $SourceDir -Recurse -File -Force) {
        $byRel[$f.FullName.Substring($base.Length + 1).Replace('\', '/')] = $f.FullName
    }
    if (-not $byRel.Count) { throw "$SourceDir 里没有文件，打不出 zip" }
    $rels = [string[]]$byRel.Keys
    [Array]::Sort($rels, [StringComparer]::Ordinal)
    $time = [DateTimeOffset]::FromUnixTimeSeconds($Epoch)
    $fs = [IO.File]::Open($ZipPath, [IO.FileMode]::Create, [IO.FileAccess]::Write)
    try {
        $zip = [IO.Compression.ZipArchive]::new($fs, [IO.Compression.ZipArchiveMode]::Create, $false)
        try {
            foreach ($rel in $rels) {
                $e = $zip.CreateEntry($rel, [IO.Compression.CompressionLevel]::Optimal)
                $e.LastWriteTime = $time
                $e.ExternalAttributes = $Unix ? (Get-ZipUnixAttributes $rel) : 32
                $out = $e.Open()
                try {
                    $in = [IO.File]::OpenRead($byRel[$rel])
                    try { $in.CopyTo($out) } finally { $in.Dispose() }
                }
                finally { $out.Dispose() }
            }
        }
        finally { $zip.Dispose() }
    }
    finally { $fs.Dispose() }
}

# zip 条目名的第一段：目录带尾斜杠（里面有更深的路径），顶层文件不带。
function Get-ZipRootEntries([string[]]$Names) {
    $set = [System.Collections.Generic.SortedSet[string]]::new([StringComparer]::Ordinal)
    foreach ($n in $Names) {
        $i = $n.IndexOf('/')
        [void]$set.Add($i -lt 0 ? $n : $n.Substring(0, $i + 1))
    }
    , @($set)
}

# 重新打开 zip 逐条核对：条目名不重复、不是绝对路径或带盘符、没有探针；时间和属性是写死的值；
# 每个条目的字节哈希与暂存目录里的文件一致（多一个少一个都算）；根条目与清单相同。
function Test-ZipContents([string]$ZipPath, [string]$StageDir, [string[]]$Manifest, [bool]$Release, [string]$Area, [long]$Epoch, [bool]$Unix) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $errors = @()
    $want = [DateTimeOffset]::FromUnixTimeSeconds($Epoch).DateTime
    $names = @()
    $actual = @{}
    $zip = [IO.Compression.ZipFile]::OpenRead($ZipPath)
    try {
        foreach ($e in $zip.Entries) {
            $names += $e.FullName
            if ($e.FullName -match '^/|\\|^[A-Za-z]:|(^|/)\.\.(/|$)') { $errors += "[$Area] 条目名不干净：$($e.FullName)" }
            if ($e.FullName.EndsWith('/')) { $errors += "[$Area] 不该有目录条目：$($e.FullName)"; continue }
            if ($e.FullName -match '(?i)probe') { $errors += "[$Area] zip 里出现了探针条目：$($e.FullName)" }
            if ($e.LastWriteTime.DateTime -ne $want) { $errors += "[$Area] $($e.FullName) 的时间是 $($e.LastWriteTime.DateTime.ToString('u'))，应为固定的 $($want.ToString('u'))" }
            $wantAttr = $Unix ? (Get-ZipUnixAttributes $e.FullName) : 32
            if ($e.ExternalAttributes -ne $wantAttr) { $errors += "[$Area] $($e.FullName) 的属性是 $($e.ExternalAttributes)，应为 $wantAttr" }
            $st = $e.Open()
            try { $actual[$e.FullName] = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($st)).ToLowerInvariant() }
            finally { $st.Dispose() }
        }
    }
    finally { $zip.Dispose() }
    if ($names.Count -ne @($names | Sort-Object -Unique -CaseSensitive).Count) { $errors += "[$Area] 条目名有重复" }
    $errors += Compare-Trees (Get-TreeHashes $StageDir) $actual $Area
    $errors += Compare-RootEntries (Get-ZipRootEntries $names) $Manifest $Release $Area
    , $errors
}

# 自测与打包里的临时目录：只删本进程新建的 miciri- 目录，先核对真实路径在系统临时目录之下。
function Remove-OwnTempDir([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    $tmp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if (-not $full.StartsWith($tmp, [StringComparison]::OrdinalIgnoreCase) -or ((Split-Path -Leaf $full) -notlike 'miciri-*')) { throw "拒绝删除 $full：不是本进程新建的 miciri- 临时目录" }
    # miciri-delete-ok: 本进程用 CreateTempSubdirectory 新建的 miciri- 目录，上面已核对前缀在系统临时目录之下
    Remove-Item -LiteralPath $full -Recurse -Force -ErrorAction SilentlyContinue
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
    & $check '产物名固定' (($n.Win -ceq 'Miciri-v0.1.0-win64-setup.exe') -and ($n.Mac -ceq 'Miciri-v0.1.0-macos-arm64.pkg') -and ($n.WinZip -ceq 'Miciri-v0.1.0-win64.zip') -and ($n.MacZip -ceq 'Miciri-v0.1.0-macos-arm64.zip') -and ($n.Sums -ceq 'SHA256SUMS'))

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
    & $check '缓存：没有探针项也行（选项还没合入时）' ((Test-BuildCache "CMAKE_BUILD_TYPE:STRING=Release`n").Count -eq 0)
    foreach ($v in @('off', 'Off', '', 'NO', 'false', 'N', '0', 'FOO-NOTFOUND')) {
        & $check "缓存：探针 $v 算关" ((Test-BuildCache ($good -replace 'PROBE:UNINITIALIZED=OFF', "PROBE:BOOL=$v")).Count -eq 0)
    }
    foreach ($v in @('ON', 'on', 'TRUE', 'YES', '1', 'Y', 'yes', 'anything')) {
        & $check "缓存：探针 $v 判错" ((Test-BuildCache ($good -replace 'PROBE:UNINITIALIZED=OFF', "PROBE:BOOL=$v")).Count -eq 1)
    }
    & $check '缓存：探针 ON 的报错点明原因' ((Test-BuildCache ($good -replace 'PROBE:UNINITIALIZED=OFF', 'PROBE:BOOL=ON'))[0] -match 'MICIRI_HOST_PROBE.*不得发布')
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

    $ok = @{ $n.Win = 5MB; $n.Mac = 5MB; $n.WinZip = 5MB; $n.MacZip = 5MB }
    & $check '汇总：吻合' ((Test-CollectListing $ok '0.1.0').Count -eq 0)
    $noMacZip = $ok.Clone(); $noMacZip.Remove($n.MacZip)
    & $check '汇总：缺 mac zip' ((Test-CollectListing $noMacZip '0.1.0').Count -eq 1)
    $noWin = $ok.Clone(); $noWin.Remove($n.Win)
    & $check '汇总：缺 Windows 安装包' ((Test-CollectListing $noWin '0.1.0').Count -eq 1)
    & $check '汇总：多一个文件' ((Test-CollectListing ($ok + @{ 'extra.txt' = 10 }) '0.1.0').Count -eq 1)
    & $check '汇总：旧命名（没有 v）判错' ((Test-CollectListing @{ 'Miciri-0.1.0-win64-setup.exe' = 5MB; 'Miciri-0.1.0-macos-arm64.pkg' = 5MB; $n.WinZip = 5MB; $n.MacZip = 5MB } '0.1.0').Count -eq 4)
    & $check '汇总：版本对不上算缺也算多' ((Test-CollectListing $ok '0.2.0').Count -eq 8)
    $tiny = $ok.Clone(); $tiny[$n.MacZip] = 12
    & $check '汇总：空壳' ((Test-CollectListing $tiny '0.1.0').Count -eq 1)
    & $check '汇总：目录混进来' ((Test-CollectListing ($ok + @{ 'sub/' = -1 }) '0.1.0').Count -eq 1)

    $notes = New-ReleaseNotes @{ Tag = 'v0.1.0'; Version = '0.1.0'; Suffix = ''; WinName = $n.Win; MacName = $n.Mac; WinZipName = $n.WinZip; MacZipName = $n.MacZip; Commit = ('d' * 40); JuceVersion = '8.0.8'; JuceCommit = ('e' * 40); WebView2 = '1.0.2957.106' }
    & $check '说明含源码指引' ($notes.Contains('d' * 40) -and $notes.Contains('e' * 40) -and $notes.Contains('1.0.2957.106'))
    & $check '说明列出四个产物' ($notes.Contains($n.Win) -and $notes.Contains($n.Mac) -and $notes.Contains($n.WinZip) -and $notes.Contains($n.MacZip))
    & $check '说明中英都有' ($notes.Contains('## Downloads') -and $notes.Contains('## 下载'))
    & $check '说明里没有本机路径' ($notes -notmatch '[A-Za-z]:\\|/Users/|/home/')

    # zip：时间取整、根条目、可重现、核对能抓到哪些毛病
    & $check '时间：偶数秒取整' ((Get-ReproEpoch '1700000001' 0) -eq 1700000000)
    & $check '时间：环境变量优先于提交时间' ((Get-ReproEpoch '1700000000' 1800000000) -eq 1700000000)
    & $check '时间：没有环境变量用提交时间' ((Get-ReproEpoch '' 1800000002) -eq 1800000002)
    & $check '时间：都没有取 1980-01-01' ((Get-ReproEpoch '' 0) -eq 315532800)
    & $check '时间：早于 1980 的抬到 1980' ((Get-ReproEpoch '5' 0) -eq 315532800)
    & $check 'Unix 属性：MacOS 下可执行' ((Get-ZipUnixAttributes 'Miciri.vst3/Contents/MacOS/Miciri') -eq (0x81ED -shl 16))
    & $check 'Unix 属性：其他 0644' ((Get-ZipUnixAttributes 'Miciri.vst3/Contents/Info.plist') -eq (0x81A4 -shl 16))
    & $check 'zip 根条目' ((Get-ZipRootEntries @('Miciri.vst3/a/b', 'INSTALL.txt', 'Miciri.vst3/c', 'LICENSES/x')) -join ',' -ceq 'INSTALL.txt,LICENSES/,Miciri.vst3/')

    $tmp = [IO.Directory]::CreateTempSubdirectory('miciri-selftest-')
    try {
        $stage = Join-Path $tmp.FullName 'stage'
        foreach ($f in @('Miciri.vst3/Contents/MacOS/Miciri', 'Miciri.vst3/Contents/Info.plist', 'LICENSE', 'LICENSES/GPL-3.0-or-later.txt', 'INSTALL.txt')) {
            $path = Join-Path $stage $f
            $null = New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path)
            [IO.File]::WriteAllText($path, "content of $f " + ('x' * 2000))
        }
        $man = @('Miciri.vst3/', 'INSTALL.txt', 'LICENSE', 'LICENSES/', 'THIRD-PARTY-NOTICES.md?')
        $epoch = 1700000000
        foreach ($unix in @($false, $true)) {
            $z1 = Join-Path $tmp.FullName "a-$unix.zip"
            $z2 = Join-Path $tmp.FullName "b-$unix.zip"
            New-ReproZip $stage $z1 $epoch $unix
            New-ReproZip $stage $z2 $epoch $unix
            $h1 = (Get-FileHash -LiteralPath $z1 -Algorithm SHA256).Hash
            & $check "zip(unix=$unix)：同输入同字节" ($h1 -eq (Get-FileHash -LiteralPath $z2 -Algorithm SHA256).Hash)
            & $check "zip(unix=$unix)：核对通过" ((Test-ZipContents $z1 $stage $man $false 't' $epoch $unix).Count -eq 0)
            & $check "zip(unix=$unix)：发布时缺可选项判错" ((Test-ZipContents $z1 $stage $man $true 't' $epoch $unix).Count -eq 1)
            & $check "zip(unix=$unix)：时间不对判错" ((Test-ZipContents $z1 $stage $man $false 't' ($epoch + 2) $unix).Count -ge 1)
            & $check "zip(unix=$unix)：清单少登记一项判错" ((Test-ZipContents $z1 $stage @('Miciri.vst3/', 'INSTALL.txt', 'LICENSE') $false 't' $epoch $unix).Count -eq 1)
            & $check "zip(unix=$unix)：属性口径不对判错" ((Test-ZipContents $z1 $stage $man $false 't' $epoch (-not $unix)).Count -ge 1)
        }
        $zu = Join-Path $tmp.FullName 'a-True.zip'
        $zw = Join-Path $tmp.FullName 'a-False.zip'
        $zt = Join-Path $tmp.FullName 'c.zip'
        New-ReproZip $stage $zt ($epoch + 86400) $true
        & $check 'zip：换时间字节就变' ((Get-FileHash -LiteralPath $zu -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $zt -Algorithm SHA256).Hash)
        & $check 'zip：Windows 与 Unix 口径字节不同' ((Get-FileHash -LiteralPath $zu -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $zw -Algorithm SHA256).Hash)
        [IO.File]::WriteAllText((Join-Path $stage 'LICENSE'), 'tampered after zipping')
        & $check 'zip：暂存目录里文件被改判错' ((Test-ZipContents $zu $stage $man $false 't' $epoch $true).Count -eq 1)
        [IO.File]::WriteAllText((Join-Path $stage 'extra.txt'), 'extra')
        & $check 'zip：暂存目录多一个文件判错' ((Test-ZipContents $zu $stage $man $false 't' $epoch $true).Count -ge 2)
        $null = New-Item -ItemType Directory -Force -Path (Join-Path $tmp.FullName 'empty')
        $threw = $false; try { New-ReproZip (Join-Path $tmp.FullName 'empty') (Join-Path $tmp.FullName 'e.zip') $epoch $false } catch { $threw = $true }
        & $check 'zip：空目录打不出' $threw
        $threw = $false; try { Remove-OwnTempDir (Join-Path $tmp.FullName '..') } catch { $threw = $true }
        & $check '临时目录删除：拒绝上级目录' $threw
        $threw = $false; try { Remove-OwnTempDir $env:USERPROFILE } catch { $threw = $true }
        & $check '临时目录删除：拒绝用户目录' $threw
    }
    finally { Remove-OwnTempDir $tmp.FullName }

    $fails.ToArray()
}

Export-ModuleMember -Function Get-ProjectVersion, Split-ReleaseTag, Get-ArtifactNames, Read-Manifest, Get-RootEntries, Compare-RootEntries, Get-TreeHashes, Compare-Trees, Test-BuildCache, Get-RequiredLicenses, Format-Template, Format-ShaSums, Test-CollectListing, New-ReleaseNotes, Get-ReproEpoch, Get-ZipUnixAttributes, New-ReproZip, Get-ZipRootEntries, Test-ZipContents, Remove-OwnTempDir, Invoke-PackageSelfTest
