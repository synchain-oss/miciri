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
# 装了 Visual Studio 英文语言包的机器上，VSLANG=1033 让 cl 直接输出英文，前缀只有 ASCII，不受代码页影响。

function Get-OemEncoding {
    $cp = [Globalization.CultureInfo]::CurrentCulture.TextInfo.OEMCodePage
    return [Text.Encoding]::GetEncoding($cp)
}

# 乱码的特征：把前缀按系统代码页编回字节，正好是一段合法的、含非 ASCII 的 UTF-8（UTF-8 字节被当成系统代码页解码过）。
# 正确的本地化前缀编回系统代码页后不是合法 UTF-8（中文的 GBK 字节就不是）。
function Test-MojibakePrefix([string]$Prefix, [Text.Encoding]$Oem) {
    if ($Prefix -notmatch '[^\x00-\x7F]') { return $false }
    $strict = [Text.UTF8Encoding]::new($false, $true)
    try { $null = $strict.GetString($Oem.GetBytes($Prefix)); return $true } catch { return $false }
}

function Repair-MsvcDepsPrefix {
    param([Parameter(Mandatory)][string]$BuildDir)
    if (-not $IsWindows) { return }
    $env:VSLANG = '1033'
    $cmakeFiles = Join-Path $BuildDir 'CMakeFiles'
    if (-not (Test-Path -LiteralPath $cmakeFiles)) { return }
    $oem = Get-OemEncoding
    foreach ($dir in Get-ChildItem -LiteralPath $cmakeFiles -Directory) {
        foreach ($name in 'CMakeCCompiler.cmake', 'CMakeCXXCompiler.cmake') {
            $file = Join-Path $dir.FullName $name
            if (-not (Test-Path -LiteralPath $file)) { continue }
            $hit = Select-String -LiteralPath $file -Encoding utf8 -Pattern '^set\(CMAKE_\w+_CL_SHOWINCLUDES_PREFIX "(.*)"\)' |
                Select-Object -First 1
            if ($hit -and (Test-MojibakePrefix $hit.Matches[0].Groups[1].Value $oem)) {
                Write-Host "$file 里的 /showIncludes 前缀是乱码，删掉让 CMake 重新探测"
                Remove-Item -LiteralPath $file -Force
            }
        }
    }
}

function Invoke-WithOemConsole {
    param([Parameter(Mandatory)][scriptblock]$Body)
    if (-not $IsWindows) { & $Body; return }
    $saved = $null
    try { $saved = [Console]::OutputEncoding; [Console]::OutputEncoding = Get-OemEncoding } catch { $saved = $null }
    try { & $Body }
    finally { if ($saved) { try { [Console]::OutputEncoding = $saved } catch { $null = $_ } } }
}

Export-ModuleMember -Function Repair-MsvcDepsPrefix, Invoke-WithOemConsole
