<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Derived from synchain-oss/rinne; see upstream/fonts.json -->

# web/fonts/

界面用的四个离线字体子集。插件完全离线，`web/` 一律经本地 `@font-face`（`web/shared/tokens.css`）引用它们，
不得出现任何在线字体服务的域名。文件由 CMake 编进插件二进制（`src/plugin/CMakeLists.txt` 按 `web/fonts/*` 的 woff2 扩展名收录，
`ResourceProvider` 按 `font/woff2` 提供），不需要改清单。

许可：四款都是 SIL Open Font License 1.1，逐个版权行见 `REUSE.toml`，来源、改动与改名依据见 `THIRD-PARTY-NOTICES.md` 和
`third_party/fonts/`。

| 文件 | 来源 | 字形数 | 字节数 | 用途 |
| --- | --- | --- | --- | --- |
| `SpaceGrotesk.woff2` | Space Grotesk 2.000，固定到 wght=600 | 354 | 19200 | 标题、大数字、主按钮 |
| `MiciriSans.woff2` | IBM Plex Sans 3.201，固定到 wght=400、wdth=100，改名 | 355 | 27820 | 正文 |
| `MiciriMono.woff2` | IBM Plex Mono 2.3 Regular，改名 | 349 | 18612 | 标签、读数、状态灯 |
| `NotoSansSC.woff2` | Noto Sans SC 2.004，字重轴收窄到 300 到 700 | 4172 | 1068812 | 汉字与全角标点，三条字体栈共同的内嵌回退 |

字形数是 cmap 里的码位数；字节数是 `scripts/fetch_fonts.py` 当前产出的大小，重跑后在同一个 PR 里更新这张表。

## 命名

IBM Plex 的保留字体名是 "Plex"。子集化属于 Modified Version，按 OFL-1.1 第 3 条不得再用这个名字，
所以 `name` 表的 nameID 1、3、4、6（以及有则 16、17）、文件名和 CSS 族名都是 Miciri Sans 与 Miciri Mono。
版权、商标声明、许可证声明与网址（nameID 0、7、13、14）原样保留，按第 2 条随分发物带走。
Noto Sans SC 的保留字体名是 "Source"，Space Grotesk 没有保留字体名，这两款只改了字重和字符集，没有改名。

## 字符集

字符集宽覆盖，让界面加词条时不必因为缺字回头改字体。拉丁三款拿到全部非汉字字符，Noto Sans SC 拿到全集：

1. `web/` 下 js、mjs、html、css 去掉注释后出现的全部非 ASCII 字符（上屏文本的真实来源）；
2. GB 2312 一级汉字 3755 字，覆盖通用规范汉字一级 3500 字的绝大部分常用字；
3. ASCII 可打印字符、Latin-1 补充、拉丁扩展 A；
4. 兜底字符：弯引号、破折号、省略号、窄不换行空格、箭头、数学符号、中日韩标点、全角形式。

汉字只覆盖上面的集合，不是通用规范汉字一级 3500 字的精确超集。集合之外的字（例如用户的轨道名里的生僻字）不会出方块：
三条字体栈在 Noto Sans SC 之后依次回退到 `-apple-system`、`"PingFang SC"`、`system-ui`、`"Segoe UI"`、`"Microsoft YaHei"`，
即宿主系统自带的中文字体（Miciri Mono 一栈在 Noto 之后是 `ui-monospace`、`"Cascadia Code"`、`Consolas`、`monospace`），
所以回退显示的是系统字体的字形，字重和字形风格与内嵌字体略有差异。

Noto Sans SC 自己没有的字符由前面的拉丁字体提供，三条字体栈都是「拉丁族、Noto Sans SC、系统字体」。

法语排版用窄不换行空格 U+202F。Space Grotesk、Miciri Mono 与 Noto Sans SC 没有这个字形，
`fetch_fonts.py` 把它指向细空格（没有就指向不换行空格）的字形，这样宽度在整条字体栈里是确定的。

## 字重

字重合同只有三条：Space Grotesk 600；Miciri Sans 与 Miciri Mono 400；Noto Sans SC 300 到 700。
`tokens.css` 里 `@font-face` 声明的区间比实际内嵌的字重宽，是为了让区间内的字重请求落到内嵌字形而不是整族回退，
配合 `font-synthesis: none`，缺字重时宁可走字体栈，也不合成伪粗体。拉丁文需要加重时用 Space Grotesk 600，不要请求 Miciri Sans 的 500、600。

## 核对

```
node scripts/run-checks.mjs --only fonts
```

`scripts/checks.d/fonts.mjs` 直接读四个 woff2 的 `name`、`cmap`、`OS/2`、`fvar` 表（只需要钉版 Node），核对：

- 四个字体都在，`web/` 下没有它们之外的字体文件；
- 族名与 PostScript 名等于登记名，署名记录（nameID 0、13、14）都在，署名记录之外没有保留字体名（Plex、Source）；
- nameID 0 的版权行与 `REUSE.toml` 里该文件的登记逐字一致；
- `web/` 里去掉注释后的每个非 ASCII 字符，四个字体合起来至少有一个有字形；
- 每个 `@font-face` 指向登记的文件，族名一致，声明的字重区间盖住内嵌的字重，四个字体都被声明；
- 单个 woff2 不超过 1.5 MiB，合计不超过 2 MiB（它们编进每个插件二进制）。

这些检查在 `compliance` 组里，随每个 PR 跑。词条里新增了字体里没有的字，它会判红并列出字符。

## 重新生成

只在字符覆盖检查报红、或升级上游字体时，在能联网的开发机上运行；禁止手改 woff2：

```
python scripts/fetch_fonts.py
```

需要 Python 3 与 `fontTools`、`brotli`，装进独立的虚拟环境，不要装进全局；当前产物由 fontTools 4.62.1 与 brotli 1.2.1 生成，
升级它们可能让输出字节变化。脚本从 google/fonts 的同一个钉死的提交下载上游字体并校验 sha256，同样的输入产出逐字节相同的文件。
已经下载好源文件时可以用 `--source-dir 目录` 离线重跑，`--print-charset` 只打印字符集统计、不联网、不写文件。
网络不通又必须改词条时，先让词条用现有字符集里有的字，网络恢复后再重跑本脚本。

重跑之后同一个 PR 里：更新上面表格的字形数与字节数；改了上游提交、字符集或改动方式时同步 `THIRD-PARTY-NOTICES.md`
与 `third_party/fonts/SOURCE.txt`；改了登记名时同步 `scripts/checks.d/fonts.mjs` 里的期望值。
