# SPDX-License-Identifier: GPL-3.0-or-later
#
# Windows + Ninja：保证 ninja 记得到 cl 报出的头文件依赖。
# ninja 靠 rules.ninja 里的 msvc_deps_prefix 认出 cl /showIncludes 的依赖行，按字节比对。这个前缀跟系统语言走
# （中文系统是“注意: 包含文件:”），CMake 第一次配置时探测它，写进 <构建目录>/CMakeFiles/<CMake 版本>/
# CMake{C,CXX}Compiler.cmake。cl 按控制台的输出代码页编码这行字：gates 为了日志把控制台切到 UTF-8 后，
# cl 输出 UTF-8，CMake 却按系统代码页解码，记下的前缀成了乱码；之后 ninja 认不出依赖行，改了头文件，
# 增量构建还沿用旧目标（测试会假红或假绿）。
# 办法：配置和构建时把控制台的输出代码页临时切回系统 OEM 代码页（Invoke-WithOemConsole），并把已经记成乱码的
# 探测结果删掉让 CMake 重新探测（Repair-MsvcDepsPrefix，只删单个文件，不做递归删除）。
# 英文等前缀只有 ASCII 的系统不受代码页影响，两个函数在那里都不改变什么。

# 系统的 OEM 与 ANSI 代码页：读注册表里的系统设置，不随用户的格式区域变。取不到时返回 $null，调用方退化为空操作。
function Get-SystemCodePages {
    if (-not $IsWindows) { return $null }
    try {
        [Text.Encoding]::RegisterProvider([Text.CodePagesEncodingProvider]::Instance)
        $nls = Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control\Nls\CodePage' -ErrorAction Stop
        return [pscustomobject]@{
            Oem  = [Text.Encoding]::GetEncoding([int]$nls.OEMCP)
            Ansi = [Text.Encoding]::GetEncoding([int]$nls.ACP)
        }
    } catch { return $null }
}

# 乱码的特征：UTF-8 字节被当成系统代码页解码过。所以把前缀按这些代码页之一编回字节，正好是一段合法的 UTF-8，
# 解出来还含非 ASCII 字符。正确的本地化前缀编回去不是合法 UTF-8（中文的 GBK 字节就不是）；纯 ASCII 的前缀永远不算。
function Test-MojibakePrefix([string]$Prefix, [Text.Encoding[]]$Encodings) {
    if ($Prefix -notmatch '[^\x00-\x7F]') { return $false }
    $strict = [Text.UTF8Encoding]::new($false, $true)
    foreach ($enc in $Encodings) {
        try {
            if ($strict.GetString($enc.GetBytes($Prefix)) -match '[^\x00-\x7F]') { return $true }
        } catch { $null = $_ }
    }
    return $false
}

function Repair-MsvcDepsPrefix {
    param([Parameter(Mandatory)][string]$BuildDir)
    if (-not $IsWindows) { return }
    $cmakeFiles = Join-Path $BuildDir 'CMakeFiles'
    if (-not (Test-Path -LiteralPath $cmakeFiles)) { return }
    $cp = Get-SystemCodePages
    if (-not $cp) { Write-Warning '取不到系统代码页，跳过 /showIncludes 前缀检查'; return }
    foreach ($dir in Get-ChildItem -LiteralPath $cmakeFiles -Directory) {
        foreach ($name in 'CMakeCCompiler.cmake', 'CMakeCXXCompiler.cmake') {
            $file = Join-Path $dir.FullName $name
            if (-not (Test-Path -LiteralPath $file)) { continue }
            $hit = Select-String -LiteralPath $file -Encoding utf8 -Pattern '^set\(CMAKE_\w+_CL_SHOWINCLUDES_PREFIX "(.*)"\)' |
                Select-Object -First 1
            if ($hit -and (Test-MojibakePrefix $hit.Matches[0].Groups[1].Value @($cp.Oem, $cp.Ansi))) {
                Write-Host "$file 里的 /showIncludes 前缀是乱码，删掉让 CMake 重新探测"
                Remove-Item -LiteralPath $file -Force
            }
        }
    }
}

function Invoke-WithOemConsole {
    param([Parameter(Mandatory)][scriptblock]$Body)
    $saved = $null
    $cp = Get-SystemCodePages
    if ($cp) {
        $current = $null
        try { $current = [Console]::OutputEncoding } catch { $current = $null }
        if ($current -and $current.CodePage -ne $cp.Oem.CodePage) {
            try { [Console]::OutputEncoding = $cp.Oem; $saved = $current }
            catch { Write-Warning "没能把控制台切到系统代码页 $($cp.Oem.CodePage)（$($_.Exception.Message)），ninja 可能记不到头文件依赖" }
        }
    }
    try { & $Body }
    finally { if ($saved) { try { [Console]::OutputEncoding = $saved } catch { $null = $_ } } }
}

Export-ModuleMember -Function Repair-MsvcDepsPrefix, Invoke-WithOemConsole, Test-MojibakePrefix
