<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Third-party notices / 第三方声明

Miciri is free software under GPL-3.0-or-later ([LICENSE](LICENSE)). The binaries combine Miciri with the components below,
each under its own licence. The full texts are in [LICENSES/](LICENSES/); the notice and source note of every component sit in
`third_party/<name>/`, and both folders ship with the installers and the zips.

Miciri 自己的代码是 GPL-3.0-or-later。发行包里的二进制还链接了下列组件，各自按自己的许可。许可全文在 `LICENSES/`，
每个组件的许可声明和来源说明在 `third_party/<名字>/`，两处都随安装包和 zip 一起发。

## In the plug-in binaries / 链接进插件的组件

| Component | Version | Licence (SPDX) | Where it is used | Notice |
|---|---|---|---|---|
| [JUCE](https://github.com/juce-framework/JUCE) | 8.0.8 | AGPL-3.0-only | Plug-in wrapper (VST3, Audio Unit), web view, and the JavaScript helper the window uses to talk to the plug-in. JUCE is dual licensed; Miciri uses the AGPLv3 option. | `third_party/juce/` |
| [Steinberg VST3 SDK](https://github.com/steinbergmedia/vst3sdk) (bundled in JUCE) | as bundled in JUCE 8.0.8 | GPL-3.0-only | VST3 build, and the Audio Unit wrapper through JUCE. The SDK is dual licensed (Steinberg VST3 License or GPLv3); Miciri uses the GPLv3 option. | `third_party/vst3sdk/` |
| [AudioUnitSDK](https://github.com/apple/AudioUnitSDK) (bundled in JUCE) | as bundled in JUCE 8.0.8 | Apache-2.0 | macOS Audio Unit build only. | `third_party/audiounitsdk/` |
| [pocketfft](https://github.com/mreineck/pocketfft) (C++ header-only edition) | commit `4d865cb` | BSD-3-Clause | The spectrum estimate in the mains-hum check. Copied unmodified into `third_party/pocketfft/`. | `third_party/pocketfft/` |
| [Microsoft.Web.WebView2](https://www.nuget.org/packages/Microsoft.Web.WebView2) (loader) | 1.0.2957.106 | BSD-3-Clause | Windows build only: the loader is linked statically as a binary. The WebView2 Runtime itself is not bundled. | `third_party/webview2/` |

The combined binary is distributed under the GPLv3, and the JUCE parts remain under the AGPLv3. The corresponding source is
the Miciri commit and the JUCE commit named in the `INSTALL.txt` that comes with every installer and zip.

## In the Windows installer only / 只在 Windows 安装包里

| Component | Version | Licence (SPDX) | Where it is used | Notice |
|---|---|---|---|---|
| [Inno Setup](https://jrsoftware.org/isinfo.php) (runtime) | 6.7.4 | LicenseRef-Inno-Setup | The installer and uninstaller programs are built with Inno Setup and contain its runtime. Copyright 1997-2026 Jordan Russell, portions copyright 2000-2026 Martijn Laan. | `third_party/innosetup/`, `LICENSES/LicenseRef-Inno-Setup.txt` |
| [Inno Setup Chinese Simplified translation](https://github.com/kira-96/Inno-Setup-Chinese-Simplified-Translation) | file from the official Inno Setup repository, tag `is-6_7_4`, unchanged | MIT | The Simplified Chinese wizard text. Copyright (c) 2019-2020 kirakira. | `third_party/inno-setup-chinese-simplified/` |

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
