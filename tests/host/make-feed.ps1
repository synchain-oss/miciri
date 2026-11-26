#Requires -Version 7.0
# SPDX-License-Identifier: GPL-3.0-or-later
#
# 宿主测试用的合成输入信号，给电平表的目视检查用（reaper-harness.json 里的 meter-steady、meter-clip-lamp 场景循环播放它们）。
# 全是现场算出来的合成信号，不含任何实录；文件写在被忽略的 build-host-feeds/ 下，不进仓。
#   pwsh tests/host/make-feed.ps1 [-OutDir build-host-feeds] [-SampleRate 48000]
# 输出两个单声道 16 位 WAV（工具只认 pcm16、pcm24、float32，采样率要和场景的 sr 一致，不会重采样）：
#   meter-steady.wav     1 kHz 正弦，峰值 −9 dBFS（整数个周期，循环无缝）。真峰值读 −9，均方根读 −12.0。
#   meter-clip-lamp.wav  12 秒一圈：开头 50 ms 满幅方波（±满刻度，连续多个样本过削波门限），其余是 −30 dBFS 的 1 kHz 正弦
#                        （整数个周期，循环无缝）。每圈削波一次，削波灯亮 5 秒再熄，12 秒的周期让一次保持里能看到亮、熄、再亮。
# 只覆盖本脚本自己写的两个文件，不删除任何东西；输出目录必须在仓内。
[CmdletBinding()]
param(
    [string]$OutDir = 'build-host-feeds',
    [int]$SampleRate = 48000
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if ($SampleRate % 1000 -ne 0 -or $SampleRate -lt 8000 -or $SampleRate -gt 384000) { throw '采样率要是 1000 的整数倍（1 kHz 的正弦才有整数个周期），在 8000 到 384000 之间' }
$out = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath((Join-Path $Root $OutDir))
$rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
if (-not ([IO.Path]::GetFullPath($out) + [IO.Path]::DirectorySeparatorChar).StartsWith($rootFull, [StringComparison]::OrdinalIgnoreCase)) { throw "输出目录必须在仓内：$OutDir" }
New-Item -ItemType Directory -Force -Path $out | Out-Null

$period = $SampleRate / 1000   # 1 kHz 一个周期的样本数
function New-Sine([double]$PeakDb) {
    $amp = 32768.0 * [math]::Pow(10, $PeakDb / 20)
    $one = [int16[]]::new($period)
    for ($i = 0; $i -lt $period; $i++) { $one[$i] = [int16][math]::Round($amp * [math]::Sin(2 * [math]::PI * $i / $period)) }
    , $one
}
function Join-Periods([int16[]]$One, [int]$Count) {
    $all = [int16[]]::new($One.Length * $Count)
    for ($k = 0; $k -lt $Count; $k++) { [Array]::Copy($One, 0, $all, $k * $One.Length, $One.Length) }
    , $all
}
function Write-Wav([string]$Path, [int16[]]$Samples) {
    $data = [byte[]]::new($Samples.Length * 2)
    [Buffer]::BlockCopy($Samples, 0, $data, 0, $data.Length)
    $ms = [IO.MemoryStream]::new()
    $w = [IO.BinaryWriter]::new($ms)
    $w.Write([Text.Encoding]::ASCII.GetBytes('RIFF')); $w.Write([int](36 + $data.Length)); $w.Write([Text.Encoding]::ASCII.GetBytes('WAVE'))
    $w.Write([Text.Encoding]::ASCII.GetBytes('fmt ')); $w.Write([int]16); $w.Write([int16]1); $w.Write([int16]1)
    $w.Write([int]$SampleRate); $w.Write([int]($SampleRate * 2)); $w.Write([int16]2); $w.Write([int16]16)
    $w.Write([Text.Encoding]::ASCII.GetBytes('data')); $w.Write([int]$data.Length); $w.Write($data)
    $w.Flush()
    [IO.File]::WriteAllBytes($Path, $ms.ToArray())
}

# 稳态：1 秒，1000 个周期
Write-Wav (Join-Path $out 'meter-steady.wav') (Join-Periods (New-Sine -PeakDb -9) 1000)

# 削波灯：50 ms 满幅方波（1 kHz，半周期 period/2 个样本，上下各到满刻度）+ 11.95 秒 −30 dBFS 正弦 = 12 秒
$half = $period / 2
$sq = [int16[]]::new($period)
for ($i = 0; $i -lt $period; $i++) { $sq[$i] = if ($i -lt $half) { [int16]32767 } else { [int16]-32768 } }
$burst = Join-Periods $sq 50
$tone = Join-Periods (New-Sine -PeakDb -30) 11950
$clip = [int16[]]::new($burst.Length + $tone.Length)
[Array]::Copy($burst, 0, $clip, 0, $burst.Length)
[Array]::Copy($tone, 0, $clip, $burst.Length, $tone.Length)
Write-Wav (Join-Path $out 'meter-clip-lamp.wav') $clip

foreach ($f in 'meter-steady.wav', 'meter-clip-lamp.wav') {
    $p = Join-Path $out $f
    Write-Host ('{0}: {1} 字节，sha256 {2}' -f $f, (Get-Item -LiteralPath $p).Length, (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.Substring(0, 12).ToLower())
}
