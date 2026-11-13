<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Third-party notices / 第三方声明

Miciri is free software under GPL-3.0-or-later ([LICENSE](LICENSE)). The binaries combine Miciri with the components below,
each under its own licence. The full texts are in [LICENSES/](LICENSES/); the notice and source note of every component sit in
`third_party/<name>/`. Both folders ship with every installer and zip, except that the Inno Setup licence text in `LICENSES/` goes
with the Windows installer only. A notice folder can therefore sit in a package whose binary does not contain that component; the
tables below say which binaries contain what.

Miciri 自己的代码是 GPL-3.0-or-later。发行包里的二进制还链接了下列组件，各自按自己的许可。许可全文在 `LICENSES/`，
每个组件的许可声明和来源说明在 `third_party/<名字>/`。两处随每个安装包和 zip 一起发，只有 Inno Setup 的许可文本（在 `LICENSES/` 里）只随
Windows 安装包走。所以某个包里可能带着它的二进制并不含的组件的声明目录；下面的表写明了各组件在哪些二进制里。

## In the plug-in binaries / 链接进插件的组件

| Component | Version | Licence (SPDX) | Where it is used | Notice |
|---|---|---|---|---|
| [JUCE](https://github.com/juce-framework/JUCE) | 8.0.8 | AGPL-3.0-only | Plug-in wrapper (VST3, Audio Unit), web view, and the JavaScript helper the window uses to talk to the plug-in. JUCE is dual licensed; Miciri uses the AGPLv3 option. | `third_party/juce/` |
| [Steinberg VST3 SDK](https://github.com/steinbergmedia/vst3sdk) (bundled in JUCE) | as bundled in JUCE 8.0.8 | GPL-3.0-only | VST3 build, and the Audio Unit wrapper through JUCE. The SDK is dual licensed (Steinberg VST3 License or GPLv3); Miciri uses the GPLv3 option. | `third_party/vst3sdk/` |
| [AudioUnitSDK](https://github.com/apple/AudioUnitSDK) (bundled in JUCE) | as bundled in JUCE 8.0.8 | Apache-2.0 | macOS Audio Unit build only. | `third_party/audiounitsdk/` |
| [pocketfft](https://github.com/mreineck/pocketfft) (C++ header-only edition) | commit `4d865cb` | BSD-3-Clause | The spectrum estimate in the mains-hum check. Copied unmodified into `third_party/pocketfft/`. | `third_party/pocketfft/` |
| [Microsoft.Web.WebView2](https://www.nuget.org/packages/Microsoft.Web.WebView2) (loader) | 1.0.2957.106 | BSD-3-Clause | Windows build only: the loader is linked statically as a binary. The WebView2 Runtime itself is not bundled. | `third_party/webview2/` |
| [Space Grotesk](https://github.com/floriankarsten/space-grotesk) | 2.000, subset, weight fixed at 600 | OFL-1.1 | The embedded web interface (all formats): headings and large numbers. | `third_party/fonts/` |
| [IBM Plex Sans](https://github.com/IBM/plex), shipped as Miciri Sans | 3.201, subset, weight 400 and width 100, renamed | OFL-1.1 | The embedded web interface (all formats): body text. | `third_party/fonts/` |
| [IBM Plex Mono](https://github.com/IBM/plex), shipped as Miciri Mono | 2.3 Regular, subset, renamed | OFL-1.1 | The embedded web interface (all formats): labels and readouts. | `third_party/fonts/` |
| [Noto Sans SC](https://github.com/notofonts/noto-cjk) | 2.004, subset, weight axis 300 to 700 | OFL-1.1 | The embedded web interface (all formats): Chinese characters and full-width punctuation. | `third_party/fonts/` |

The combined binary is distributed under the GPLv3, and the JUCE parts remain under the AGPLv3. The corresponding source is
the Miciri commit and the JUCE commit named in the `INSTALL.txt` that comes with every installer and zip.

### Fonts / 字体

The window's web interface uses four font families under the SIL Open Font License 1.1 (`LICENSES/OFL-1.1.txt`). They are
subset from the upstream fonts and compiled into the plug-in binaries, so they ship in every format. The fonts stay under the
OFL: they are bundled with the GPL-licensed program, not relicensed by it.

- **Reserved Font Names.** "Plex" is a Reserved Font Name of IBM Plex, and subsetting makes a Modified Version, so under OFL-1.1
  section 3 the two IBM Plex subsets are named **Miciri Sans** and **Miciri Mono**: the `name` table entries 1, 3, 4, 6, 16 and
  17, the file names and the CSS family names all use the new names. Noto Sans SC has the Reserved Font Name "Source", which its
  subset name does not use; Space Grotesk has none.
- **Notices kept.** The copyright, trademark and licence entries of every font (name IDs 0, 7, 13 and 14) are exactly as upstream
  wrote them, and the upstream `OFL.txt` of each family, with its copyright line and Reserved Font Name statement, is in
  `third_party/fonts/NOTICE.*.txt` (OFL-1.1 section 2).
- **Source and changes.** `scripts/fetch_fonts.py` builds the four files from the upstream fonts in google/fonts at commit
  `bd8f81ddb5c74d5c8897b36ad88b440266245103` and checks the SHA-256 of every source file. The changes are: subsetting to the
  characters the interface uses plus the common Chinese characters (GB 2312 level 1) and the Latin-1 and Latin Extended-A blocks;
  fixing the weight of Space Grotesk at 600 and the weight and width of IBM Plex Sans at 400 and 100; narrowing the weight axis of
  Noto Sans SC to 300 to 700; mapping U+202F (narrow no-break space) onto the thin-space or no-break-space glyph where the font
  lacks it; renaming the two IBM Plex subsets. The files, versions and sizes are listed in `web/fonts/README.md`.
- **Copyright lines**, as recorded in the font files (name ID 0) and in `REUSE.toml` (`scripts/checks.d/fonts.mjs` compares them):

  | File | Copyright line |
  | --- | --- |
  | `web/fonts/SpaceGrotesk.woff2` | `Copyright 2020 The Space Grotesk Project Authors (https://github.com/floriankarsten/space-grotesk)` |
  | `web/fonts/MiciriSans.woff2` | `Copyright 2019 IBM Corp. All rights reserved.` |
  | `web/fonts/MiciriMono.woff2` | `Copyright 2017 IBM Corp. All rights reserved.` |
  | `web/fonts/NotoSansSC.woff2` | `(c) 2014-2021 Adobe (http://www.adobe.com/), with Reserved Font Name 'Source'.` |

界面里的四个字体按 SIL Open Font License 1.1（`LICENSES/OFL-1.1.txt`）授权。它们是上游字体的子集，编进插件二进制，所以每种格式都带着。
字体仍然是 OFL：它们是随 GPL 程序一起分发，不是被 GPL 重新授权。

- **保留字体名。** "Plex" 是 IBM Plex 的保留字体名，子集化属于 Modified Version，按 OFL-1.1 第 3 条，两个 IBM Plex 子集改名为
  **Miciri Sans** 与 **Miciri Mono**：`name` 表的 nameID 1、3、4、6、16、17、文件名和 CSS 族名都用新名字。Noto Sans SC 的保留字体名是
  "Source"，子集名没有用它；Space Grotesk 没有保留字体名。
- **署名原样保留。** 每个字体的版权、商标与许可证记录（nameID 0、7、13、14）与上游逐字相同；每个家族上游的 `OFL.txt`
  （含版权行与保留字体名声明）在 `third_party/fonts/NOTICE.*.txt`，满足 OFL-1.1 第 2 条。
- **来源与改动。** `scripts/fetch_fonts.py` 从 google/fonts 的提交 `bd8f81ddb5c74d5c8897b36ad88b440266245103` 取上游字体生成这四个文件，
  并校验每个源文件的 SHA-256。改动：子集化为界面用到的字符加常用汉字（GB 2312 一级）、Latin-1 与拉丁扩展 A；
  Space Grotesk 固定到字重 600，IBM Plex Sans 固定到字重 400、字宽 100；Noto Sans SC 的字重轴收窄到 300 至 700；
  字体里没有 U+202F（窄不换行空格）时把它指向细空格或不换行空格的字形；两个 IBM Plex 子集改名。
  文件、版本与大小见 `web/fonts/README.md`。
- **版权行**见上表：字体文件里的 nameID 0 与 `REUSE.toml` 的登记逐字一致，由 `scripts/checks.d/fonts.mjs` 核对。

## In the Windows installer only / 只在 Windows 安装包里

| Component | Version | Licence (SPDX) | Where it is used | Notice |
|---|---|---|---|---|
| [Inno Setup](https://jrsoftware.org/isinfo.php) (runtime) | 6.7.4 | LicenseRef-Inno-Setup | The installer and uninstaller programs are built with Inno Setup and contain its runtime. Copyright 1997-2026 Jordan Russell, portions copyright 2000-2026 Martijn Laan. | `third_party/innosetup/`, `LICENSES/LicenseRef-Inno-Setup.txt` |
| [Inno Setup Chinese Simplified translation](https://github.com/kira-96/Inno-Setup-Chinese-Simplified-Translation) | file from the official Inno Setup repository, tag `is-6_7_4`, unchanged | MIT | The Simplified Chinese wizard text. Copyright (c) 2019-2020 kirakira; the file header names Zhenghan Yang as the maintainer. | `third_party/inno-setup-chinese-simplified/` |

The zips and the macOS package contain no Inno Setup code. The installer's licence page stays the English GPL text.

The Chinese translation is under the MIT licence:

> Copyright (c) 2019 - 2020 kirakira
>
> Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation
> files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy,
> modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
> Software is furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
> WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
> COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
> ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

## Used to build and test, not shipped / 只用于构建和测试，不随发行包发

- [Catch2](https://github.com/catchorg/Catch2) (BSL-1.0): unit tests; fetched by CMake at configure time.
- The pinned tools listed in `.node-version`, `.pluginval-version` and the other pin files, and the Node packages under `web-preview/tests/axe/`
  (page accessibility test): run in CI and on a developer machine, never redistributed.

## Keeping this file true / 维护

A pull request that adds, upgrades or removes a component changes this file, `LICENSES/`, `REUSE.toml` and the matching
`third_party/<name>/` folder together. `scripts/package.ps1` fails a release build when `THIRD-PARTY-NOTICES.md` or a licence
text of a linked component is missing; `reuse lint` fails on a licence text that no file refers to.
