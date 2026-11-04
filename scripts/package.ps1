#Requires -Version 7.0
# SPDX-License-Identifier: GPL-3.0-or-later
#
# 发行打包：Windows x64 出 Inno Setup 安装包，macOS arm64 出 pkg（VST3 + AU）。不签名、不公证。
#   pwsh scripts/package.ps1 [-BuildDir build] [-OutDir <目录>] [-Iscc <ISCC.exe>] [-Verify] [-Release] [-NoReconfigure]
#   pwsh scripts/package.ps1 -Collect <目录> -Tag vX.Y.Z [-Notes <文件>]     汇总两个平台的安装包：核清单、写 SHA256SUMS 与发布说明
#   pwsh scripts/package.ps1 -SelfTest
# 打包一个平台的步骤：
#   ① 版本只读 CMakeLists.txt 的 project(Miciri VERSION …)，产物名由它算出（Get-ArtifactNames），不写版本字面量。
#   ② 构建目录里重新配置一次 -DMICIRI_HOST_PROBE=OFF 并增量构建（正常是空操作），再核缓存：必须是 Release、探针 OFF、不是只建 core。
#   ③ 构建树里恰好一个 Miciri.vst3（macOS 另有恰好一个 Miciri.component，且只含 arm64）；多一个（比如探针变体）就拒。
#   ④ 暂存到 build/package/<平台>/：插件原样复制，许可与第三方声明、INSTALL.txt 放在插件包之外（macOS 的包体改动会破坏签名封印）。
#   ⑤ 暂存的根条目与 packaging/manifest-*.txt 逐项比对，多一个少一个都红。
#   ⑥ Windows 用钉版 Inno Setup 编译 packaging/win/miciri.iss；macOS 用 pkgbuild + productbuild。
#   ⑦ -Verify：Windows 静默安装到临时目录、逐文件核哈希、再静默卸载并确认清干净；macOS 展开 pkg 逐文件核哈希。
# 发布（-Release 或环境变量 MICIRI_PACKAGE_RELEASE=1）更严：THIRD-PARTY-NOTICES.md 与二进制里链接的每一份许可全文必须在。
# 退出码：0 通过；1 失败；2 没有东西可跑（没找到 Inno Setup、没有构建出的插件）：调用方记 SKIP，不当作通过。
[CmdletBinding()]
param(
    [string]$BuildDir = 'build',
    [string]$OutDir = '',
    [string]$Iscc = $env:MICIRI_ISCC,
    [switch]$Verify, [switch]$Release, [switch]$NoReconfigure,
    [string]$Collect = '', [string]$Tag = '', [string]$Notes = '',
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root
Import-Module (Join-Path $PSScriptRoot 'package-common.psm1') -Force

function Skip([string]$Why) { Write-Host "SKIP: $Why"; exit 2 }
function Invoke-Native([string]$Exe, [string[]]$Arguments) {
    & $Exe @Arguments
    if ($LASTEXITCODE) { throw "$Exe 失败（退出码 $LASTEXITCODE）" }
}
function Assert-NoErrors([string]$What, $Errors) {
    $e = @($Errors)
    if ($e.Count) { throw "$What 不通过：`n  " + ($e -join "`n  ") }
}

if ($SelfTest) {
    $fails = @(Invoke-PackageSelfTest)
    if ($fails.Count) { $fails | ForEach-Object { Write-Host "自测未通过：$_" }; exit 1 }
    Write-Host 'package 自测通过'
    exit 0
}

$version = Get-ProjectVersion (Get-Content -LiteralPath 'CMakeLists.txt' -Raw)
$names = Get-ArtifactNames $version
$isRelease = $Release -or ($env:MICIRI_PACKAGE_RELEASE -eq '1')

function Read-Sha256File([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }

function Get-SourceFacts {
    $webview2 = [regex]::Match((Get-Content -LiteralPath 'src/plugin/MiciriWebView2.cmake' -Raw), 'MICIRI_WEBVIEW2_VERSION\s+"([^"]+)"').Groups[1].Value
    if (-not $webview2) { throw '读不到 MiciriWebView2.cmake 里的 WebView2 版本' }
    @{
        Version      = $version
        Commit       = "$(git rev-parse HEAD)".Trim()
        JuceVersion  = (Get-Content -LiteralPath '.juce-version' -Raw).Trim()
        JuceCommit   = (Get-Content -LiteralPath '.juce-commit' -Raw).Trim()
        WebView2     = $webview2
    }
}

# ---- -Collect：汇总两个平台的安装包 --------------------------------------------------------------------------
if ($Collect) {
    if (-not $Tag) { throw '-Collect 需要 -Tag' }
    $t = Split-ReleaseTag $Tag
    if ($t.Version -cne $version) { throw "tag $Tag 的版本 $($t.Version) 与工程版本 $version 不一致" }
    $dir = (Resolve-Path -LiteralPath $Collect).Path
    $listing = @{}
    foreach ($i in Get-ChildItem -LiteralPath $dir -Force) { $listing[$i.Name] = $i.PSIsContainer ? -1 : $i.Length }
    Assert-NoErrors '产物清单' (Test-CollectListing $listing $version)
    $hashes = @{}
    foreach ($n in @($names.Win, $names.Mac)) { $hashes[$n] = Read-Sha256File (Join-Path $dir $n) }
    [IO.File]::WriteAllText((Join-Path $dir $names.Sums), (Format-ShaSums $hashes), [Text.UTF8Encoding]::new($false))
    if ($Notes) {
        $f = Get-SourceFacts
        $text = New-ReleaseNotes @{ Tag = $Tag; Version = $version; Suffix = $t.Suffix; WinName = $names.Win; MacName = $names.Mac; Commit = $f.Commit; JuceVersion = $f.JuceVersion; JuceCommit = $f.JuceCommit; WebView2 = $f.WebView2 }
        [IO.File]::WriteAllText([IO.Path]::GetFullPath($Notes), $text, [Text.UTF8Encoding]::new($false))
    }
    Write-Host "产物清单通过：$($names.Win)、$($names.Mac)、$($names.Sums)"
    exit 0
}

# ---- 打包一个平台 ---------------------------------------------------------------------------------------------
if (-not ($IsWindows -or $IsMacOS)) { Skip '只在 Windows 与 macOS 上打包' }
$platform = $IsWindows ? 'win' : 'mac'
$manifestFile = $IsWindows ? 'packaging/manifest-win64.txt' : 'packaging/manifest-macos-arm64.txt'
$manifest = Read-Manifest (Get-Content -LiteralPath $manifestFile -Raw)
$outDirFull = [IO.Path]::GetFullPath($OutDir ? $OutDir : (Join-Path $Root 'build/package/out'))
$stageRoot = [IO.Path]::GetFullPath((Join-Path $Root "build/package/$platform"))

function Reset-Stage {
    # 只清仓内写死的 build/package/<平台>，路径前缀在这里核对
    $fixed = [IO.Path]::GetFullPath((Join-Path $Root 'build/package'))
    if (-not $stageRoot.StartsWith($fixed + [IO.Path]::DirectorySeparatorChar, [StringComparison]::Ordinal)) { throw "拒绝清理 $stageRoot：不在 $fixed 之下" }
    if (Test-Path -LiteralPath $stageRoot) {
        # miciri-delete-ok: 仓内写死的输出目录 build/package 之下，上面已核对解析后的路径前缀
        Remove-Item -LiteralPath $stageRoot -Recurse -Force
    }
    $null = New-Item -ItemType Directory -Force -Path $stageRoot
}

function Sync-Build {
    if ($IsWindows -and -not (Get-Command cl -ErrorAction SilentlyContinue)) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if (-not $vs) { throw '找不到带 C++ 工具集的 Visual Studio' }
        foreach ($line in (cmd /c "`"$(Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat')`" >nul && set")) {
            if ($line -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
        }
    }
    if ($env:MICIRI_NINJA) { $env:PATH = (Split-Path -Parent $env:MICIRI_NINJA) + [IO.Path]::PathSeparator + $env:PATH }
    # 探针开关显式写 OFF：发行包不许带探针，不靠它的默认值
    Invoke-Native cmake @('-S', '.', '-B', $BuildDir, '-DMICIRI_HOST_PROBE=OFF')
    Invoke-Native cmake @('--build', $BuildDir, '--parallel')
}

function Get-Bundles([string]$Filter) {
    @(Get-ChildItem -LiteralPath $BuildDir -Recurse -Directory -Filter $Filter | Where-Object { $_.FullName -notmatch '[\\/]_deps[\\/]' })
}
function Get-OneBundle([string]$Name) {
    $found = @(Get-Bundles "*$([IO.Path]::GetExtension($Name))")
    if ($found.Count -ne 1 -or $found[0].Name -cne $Name) {
        throw "构建树里应当恰好一个 $Name，现在是：$(($found | ForEach-Object Name) -join '、')"
    }
    $found[0].FullName
}

# 许可、第三方声明与 INSTALL.txt：放在插件包之外
function Copy-Compliance([string]$Dest, [string]$TemplateFile) {
    $null = New-Item -ItemType Directory -Force -Path $Dest
    foreach ($n in (Get-RequiredLicenses $isRelease $IsMacOS)) {
        if (-not (Test-Path -LiteralPath "LICENSES/$n.txt")) { throw "LICENSES/$n.txt 不存在，发行包必须带这份许可全文" }
    }
    Copy-Item -LiteralPath 'LICENSE' -Destination $Dest
    $lic = Join-Path $Dest 'LICENSES'
    $null = New-Item -ItemType Directory -Force -Path $lic
    foreach ($f in Get-ChildItem -LiteralPath 'LICENSES' -File) { Copy-Item -LiteralPath $f.FullName -Destination $lic }
    foreach ($d in Get-ChildItem -LiteralPath 'third_party' -Directory) {
        foreach ($f in Get-ChildItem -LiteralPath $d.FullName -File | Where-Object { $_.Name -match '^(LICENSE|COPYING|NOTICE|SOURCE)' }) {
            $to = Join-Path $Dest "third_party/$($d.Name)"
            $null = New-Item -ItemType Directory -Force -Path $to
            Copy-Item -LiteralPath $f.FullName -Destination $to
        }
    }
    if (Test-Path -LiteralPath 'THIRD-PARTY-NOTICES.md') { Copy-Item -LiteralPath 'THIRD-PARTY-NOTICES.md' -Destination $Dest }
    $f = Get-SourceFacts
    $text = Format-Template (Get-Content -LiteralPath $TemplateFile -Raw) @{ VERSION = $f.Version; COMMIT = $f.Commit; JUCE_VERSION = $f.JuceVersion; JUCE_COMMIT = $f.JuceCommit; WEBVIEW2_VERSION = $f.WebView2 }
    [IO.File]::WriteAllText((Join-Path $Dest 'INSTALL.txt'), $text, [Text.UTF8Encoding]::new($false))
}

function Test-StageRoots([string]$Area, [string]$Dir, [string[]]$Extra = @()) {
    $want = @($manifest[$Area]) + $Extra
    Assert-NoErrors "清单 [$Area]" (Compare-RootEntries (Get-RootEntries $Dir) $want $isRelease $Area)
}

if (-not $NoReconfigure) {
    if (-not (Test-Path -LiteralPath (Join-Path $BuildDir 'CMakeCache.txt'))) { Skip "$BuildDir 里没有 CMakeCache.txt，没有构建出的插件可打包" }
    Sync-Build
}
Assert-NoErrors '构建缓存' (Test-BuildCache (Get-Content -LiteralPath (Join-Path $BuildDir 'CMakeCache.txt') -Raw))
$null = New-Item -ItemType Directory -Force -Path $outDirFull
Reset-Stage

if ($IsWindows) {
    # ---- Windows：Inno Setup ----
    $vst3 = Get-OneBundle 'Miciri.vst3'
    if (-not (Test-Path -LiteralPath (Join-Path $vst3 'Contents/x86_64-win/Miciri.vst3'))) { throw 'Miciri.vst3 里没有 Contents/x86_64-win/Miciri.vst3' }
    $stageVst = Join-Path $stageRoot 'vst3'
    $stageApp = Join-Path $stageRoot 'app'
    $null = New-Item -ItemType Directory -Force -Path $stageVst
    Copy-Item -LiteralPath $vst3 -Destination $stageVst -Recurse
    Copy-Compliance $stageApp 'packaging/win/INSTALL.txt.in'
    Test-StageRoots 'vst3' $stageVst
    Test-StageRoots 'app' $stageApp

    if (-not $Iscc) { $c = Get-Command ISCC.exe -ErrorAction SilentlyContinue; if ($c) { $Iscc = $c.Source } }
    if (-not $Iscc -or -not (Test-Path -LiteralPath $Iscc)) { Skip 'Inno Setup 不在本机（MICIRI_ISCC 或 -Iscc 指向 ISCC.exe），安装包的编译与静默安装只在 CI 验证；暂存目录与清单已通过' }
    $pin = (Get-Content -LiteralPath 'packaging/win/innosetup.sha256' -Raw).Trim().Split(' ')[0]
    $have = (Get-Item -LiteralPath $Iscc).VersionInfo.FileVersion
    if ("$have" -notlike "$pin*") { Write-Warning "ISCC 的文件版本是「$have」，钉版是 $pin；CI 按哈希装钉版，本机的请自行确认" }
    $baseName = [IO.Path]::GetFileNameWithoutExtension($names.Win)
    # 参数经环境变量传给 miciri.iss（ISPP 的 GetEnv），子进程继承
    $env:MICIRI_ISS_VERSION = $version
    $env:MICIRI_ISS_STAGE = $stageRoot
    $env:MICIRI_ISS_OUTDIR = $outDirFull
    $env:MICIRI_ISS_OUTNAME = $baseName
    Invoke-Native $Iscc @('/Q', 'packaging/win/miciri.iss')
    $installer = Join-Path $outDirFull $names.Win
    if (-not (Test-Path -LiteralPath $installer)) { throw "ISCC 没有产出 $installer" }
    Write-Host "已生成 $($names.Win)（$((Get-Item -LiteralPath $installer).Length) 字节）"

    if ($Verify) {
        $tmp = [IO.Directory]::CreateTempSubdirectory('miciri-verify-')
        $base = $tmp.FullName
        try {
            $app = Join-Path $base 'app'
            $vst = Join-Path $base 'vst3'
            $log = Join-Path $base 'setup.log'
            $p = Start-Process -FilePath $installer -Wait -PassThru -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', "/DIR=`"$app`"", "/Vst3Dir=`"$vst`"", "/LOG=`"$log`"")
            if ($p.ExitCode -ne 0) { if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log -Tail 30 }; throw "静默安装失败（退出码 $($p.ExitCode)）" }
            $errs = @()
            $errs += Compare-RootEntries (Get-RootEntries $vst) $manifest['vst3'] $isRelease 'installed-vst3'
            $errs += Compare-Trees (Get-TreeHashes $stageVst) (Get-TreeHashes $vst) 'installed-vst3'
            $unins = @('unins000.exe', 'unins000.dat')
            $errs += Compare-RootEntries (Get-RootEntries $app) (@($manifest['app']) + $unins) $isRelease 'installed-app'
            $actual = Get-TreeHashes $app
            foreach ($u in $unins) { $null = $actual.Remove($u) }
            $errs += Compare-Trees (Get-TreeHashes $stageApp) $actual 'installed-app'
            Assert-NoErrors '安装结果' $errs
            Write-Host '静默安装通过：VST3 与 app 目录的文件和暂存逐个一致'

            $un = Join-Path $app 'unins000.exe'
            $null = Start-Process -FilePath $un -Wait -PassThru -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART')
            # Inno 的卸载程序会把自己复制到临时目录再退出，真正的删除是异步的
            $deadline = (Get-Date).AddSeconds(90)
            while (((Test-Path -LiteralPath (Join-Path $vst 'Miciri.vst3')) -or (Test-Path -LiteralPath $un)) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
            if (Test-Path -LiteralPath (Join-Path $vst 'Miciri.vst3')) { throw '卸载后 Miciri.vst3 还在' }
            if (Test-Path -LiteralPath $un) { throw '卸载后 unins000.exe 还在' }
            $left = @(Get-ChildItem 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall' -ErrorAction SilentlyContinue | Get-ItemProperty | Where-Object { $_.PSObject.Properties['DisplayName'] -and $_.DisplayName -like 'Miciri*' })
            if ($left.Count) { throw '卸载后「程序和功能」里还有 Miciri 的条目' }
            Write-Host '静默卸载通过：插件包、卸载程序与卸载条目都已清除'
        }
        finally {
            # miciri-delete-ok: 本进程用 CreateTempSubdirectory 新建的 miciri-verify- 目录，下一行先核对前缀再删
            if ($base.StartsWith([IO.Path]::GetTempPath(), [StringComparison]::OrdinalIgnoreCase) -and (Split-Path -Leaf $base) -like 'miciri-verify-*') { Remove-Item -LiteralPath $base -Recurse -Force -ErrorAction SilentlyContinue }
        }
    }
}
else {
    # ---- macOS：pkgbuild + productbuild ----
    $vst3 = Get-OneBundle 'Miciri.vst3'
    $comp = Get-OneBundle 'Miciri.component'
    foreach ($b in @($vst3, $comp)) {
        $archs = "$(lipo -archs (Join-Path $b 'Contents/MacOS/Miciri'))".Trim()
        if ($archs -cne 'arm64') { throw "$(Split-Path -Leaf $b) 的架构是「$archs」，发行包只许 arm64" }
        $plist = Join-Path $b 'Contents/Info.plist'
        $bv = "$(plutil -extract CFBundleShortVersionString raw -o - $plist 2>$null)".Trim()
        if ($bv -and $bv -cne $version) { throw "$(Split-Path -Leaf $b) 的 CFBundleShortVersionString 是 $bv，工程版本是 $version" }
    }
    $payload = Join-Path $stageRoot 'payload'
    $areas = [ordered]@{ vst3 = 'Library/Audio/Plug-Ins/VST3'; au = 'Library/Audio/Plug-Ins/Components'; docs = 'Library/Application Support/Synchain/Miciri' }
    foreach ($a in $areas.Values) { $null = New-Item -ItemType Directory -Force -Path (Join-Path $payload $a) }
    Invoke-Native ditto @($vst3, (Join-Path $payload "$($areas.vst3)/Miciri.vst3"))
    Invoke-Native ditto @($comp, (Join-Path $payload "$($areas.au)/Miciri.component"))
    Copy-Compliance (Join-Path $payload $areas.docs) 'packaging/macos/INSTALL.txt.in'
    foreach ($a in $areas.Keys) { Test-StageRoots $a (Join-Path $payload $areas[$a]) }
    Assert-NoErrors '暂存目录' (@(Get-ChildItem -LiteralPath $payload -Recurse -Force -File | Where-Object { $_.Name -in @('.DS_Store') -or $_.Name -like '._*' } | ForEach-Object { "多余文件 $($_.Name)" }))

    $id = 'com.synchain.miciri.installer'
    $plistPath = Join-Path $stageRoot 'component.plist'
    $entries = foreach ($rel in @("$($areas.vst3)/Miciri.vst3", "$($areas.au)/Miciri.component")) {
        "<dict><key>BundleHasStrictIdentifier</key><true/><key>BundleIsRelocatable</key><false/><key>BundleIsVersionChecked</key><false/><key>BundleOverwriteAction</key><string>upgrade</string><key>RootRelativeBundlePath</key><string>$rel</string></dict>"
    }
    [IO.File]::WriteAllText($plistPath, "<?xml version=`"1.0`" encoding=`"UTF-8`"?>`n<!DOCTYPE plist PUBLIC `"-//Apple//DTD PLIST 1.0//EN`" `"http://www.apple.com/DTDs/PropertyList-1.0.dtd`">`n<plist version=`"1.0`"><array>$($entries -join '')</array></plist>`n", [Text.UTF8Encoding]::new($false))
    $componentPkg = Join-Path $stageRoot 'component.pkg'
    Invoke-Native pkgbuild @('--root', $payload, '--component-plist', $plistPath, '--identifier', $id, '--version', $version, '--install-location', '/', '--ownership', 'recommended', $componentPkg)

    $res = Join-Path $stageRoot 'resources'
    $null = New-Item -ItemType Directory -Force -Path $res
    Copy-Item -LiteralPath 'LICENSE' -Destination (Join-Path $res 'LICENSE.txt')
    $dist = @"
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>Miciri $version</title>
  <license file="LICENSE.txt" mime-type="text/plain"/>
  <options customize="never" require-scripts="false" hostArchitectures="arm64"/>
  <allowed-os-versions><os-version min="11.0"/></allowed-os-versions>
  <domains enable_anywhere="false" enable_currentUserHome="false" enable_localSystem="true"/>
  <choices-outline><line choice="default"><line choice="$id"/></line></choices-outline>
  <choice id="default"/>
  <choice id="$id" visible="false"><pkg-ref id="$id"/></choice>
  <pkg-ref id="$id" version="$version" onConclusion="none">component.pkg</pkg-ref>
</installer-gui-script>
"@
    $distPath = Join-Path $stageRoot 'distribution.xml'
    [IO.File]::WriteAllText($distPath, $dist, [Text.UTF8Encoding]::new($false))
    $installer = Join-Path $outDirFull $names.Mac
    Invoke-Native productbuild @('--distribution', $distPath, '--resources', $res, '--package-path', $stageRoot, $installer)
    Write-Host "已生成 $($names.Mac)（$((Get-Item -LiteralPath $installer).Length) 字节）"

    if ($Verify) {
        $expanded = Join-Path $stageRoot 'expanded'
        Invoke-Native pkgutil @('--expand-full', $installer, $expanded)
        $payloads = @(Get-ChildItem -LiteralPath $expanded -Recurse -Directory -Filter 'Payload')
        if ($payloads.Count -ne 1) { throw "展开后应当恰好一个 Payload 目录，现在是 $($payloads.Count)" }
        Assert-NoErrors '展开的 pkg' (Compare-Trees (Get-TreeHashes $payload) (Get-TreeHashes $payloads[0].FullName) 'pkg-payload')
        $info = Get-Content -LiteralPath (Join-Path $payloads[0].Parent.FullName 'PackageInfo') -Raw
        $errs = @()
        if ($info -notmatch ('identifier="{0}"' -f [regex]::Escape($id))) { $errs += "PackageInfo 里没有 identifier=$id" }
        if ($info -notmatch ('version="{0}"' -f [regex]::Escape($version))) { $errs += "PackageInfo 里没有 version=$version" }
        if ($info -notmatch 'install-location="/"') { $errs += 'PackageInfo 的 install-location 不是 /' }
        if ($info -match '<relocate') { $errs += 'PackageInfo 里有 <relocate>：插件包不该可重定位（升级会装到别处）' }
        Assert-NoErrors 'PackageInfo' $errs
        Write-Host 'pkg 展开核对通过：载荷与暂存逐文件一致，标识、版本与安装位置正确，插件包不可重定位'
    }
}
Write-Host "打包完成：$outDirFull"
