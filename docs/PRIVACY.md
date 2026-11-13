<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

**English** | [简体中文](PRIVACY.zh-CN.md)

# Privacy

Miciri listens to your microphone signal so that it can measure it. This page says exactly what happens to that signal and
what the plug-in keeps. If any of it changes, this page changes in the same pull request.

## In short

- Audio is analysed in memory. It is **never written to disk, never uploaded, never logged**.
- The plug-in does not use the network: no telemetry, no accounts, no update check, no crash upload.
- Audio passes through the plug-in unchanged, bit for bit.

## What stays in memory

- The audio of a test step is analysed while it arrives and then discarded. The plug-in does not record a take.
- The results of your tests (levels, ratios, bands, and the evidence the detail view draws: marked times, frequencies and
  levels, and the waveform envelope behind the evidence view) are held in memory while the plug-in is loaded.
  Closing the plug-in window keeps them; unloading the plug-in discards them.
- As of this version the plug-in saves nothing in your DAW project.

## What is written to disk

- One small per-user settings file, in your per-user application-data folder under `Synchain/Miciri`
  (on macOS: `~/Library/Application Support/Synchain/Miciri`). It holds three things: the interface language,
  whether you have seen the in-app guide, and the window scale. No audio and no measurement results.
- The web view that draws the plug-in window (Microsoft Edge WebView2 on Windows, WKWebView on macOS) keeps its own
  profile and cache folder on your machine, like any browser does. The plug-in only loads pages that ship inside it,
  so that folder holds those pages and nothing from your recordings.

## What the network sees

Nothing from the plug-in. The window loads only local pages that are packed into the plug-in, and a check in the project's
CI rejects network code and external addresses in the plug-in and its pages. If you click a link, the plug-in hands the
address to your system browser; it makes no request itself. The Microsoft WebView2 Runtime on Windows is Microsoft's
component and follows Microsoft's own terms.

## Things that are not in the released plug-in

- The host-test build (CMake option `MICIRI_HOST_PROBE`, see [HOST_PROBE.md](HOST_PROBE.md)) can write a small JSON file of
  counters to a path named in an environment variable. It is off by default, and the released binaries do not contain that code.
  The counters hold block counts and flags, never audio.
- The command-line tool `miciri-analyze` reads a WAV file that you give it, prints numbers on your screen, and does not upload anything.

## Reporting a problem

Do not attach recordings to issues or pull requests. If a measurement looks wrong, paste only the numeric JSON that
`miciri-analyze` exports. Security problems go through the channel in [SECURITY.md](../SECURITY.md).

Contact: **contact@synchain.ca**
