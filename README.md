**English** | [简体中文](README.zh-CN.md)

# Synchain Miciri

> An open-source plugin that checks your vocal recording setup: gain, distance, mic direction, room and noise.

**Miciri** (pronounced *MIC-i-ri*; Chinese name 见切, "judging the distance") is for people who are new to recording vocals. It
walks you through a few short tests, then tells you the one thing worth changing first and shows the numbers behind it.
Audio passes through unchanged: Miciri only listens, it never edits your sound.

> **Status: in development.** There is no release yet. Work happens on the `feature/v1` branch.

## What it does

You put Miciri on the track you record vocals on and follow the steps in its window. Each step is a few seconds to about a minute.

| Step | You do | Miciri looks at |
|---|---|---|
| Connect | Pick your mic type (or skip) and speak | Whether signal arrives, left and right channels, sample rate and buffer size |
| Gain | Sing at your loudest for about 8 seconds | Peak level, clipping, how much to turn the gain up or down |
| Quiet | Stay quiet for about 8 seconds | Noise floor, mains hum, signs that the device processes the sound |
| Room | Clap hard from about 1 metre away | Reverberation time (T20) in four bands |
| Position | From your singing spot: clap lightly, read two short word lists, then speak a short passage | Direct-to-room ratio, plosives, sibilance, mouth noise, and a too-close / too-far scale |
| Direction (optional) | Speak toward four directions of the mic | Whether the current direction is the loudest and brightest |

The report starts with **the one thing worth changing first** and one to three concrete actions. Each result shows a single
word and a single number; the readings, ratios and evidence are one click deeper. After you change something, retest only the
affected items and see "last time → now".

<!-- Screenshots are added with the first release. -->

## Requirements

- Windows 10 or 11 (x64) with the Microsoft Edge WebView2 Runtime (part of current Windows), or macOS on Apple silicon (arm64).
- A host that loads VST3 plug-ins (Windows, macOS) or Audio Units (macOS).
- Mono and stereo tracks. See [Known issues](docs/KNOWN_ISSUES.md) for what is and is not supported.

## Supported DAWs

No host has a full test record yet, so every host is listed as **not tested** in [docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md). The
v1 targets are REAPER, Cubase, FL Studio and GarageBand / Logic (Audio Unit). Results are added there with the host version and
the date as they are verified.

## Install

Release files will be named `Miciri-v<version>-win64-setup.exe`, `Miciri-v<version>-win64.zip`,
`Miciri-v<version>-macos-arm64.pkg` and `Miciri-v<version>-macos-arm64.zip`. New to plug-ins: use the installer. If you manage
your plug-in folders yourself: use the zip. Both are unsigned and not notarized. The [user guide](docs/USER_GUIDE.md) has the
step-by-step instructions, including how to clear the macOS quarantine flag.

## Quick start

1. Put Miciri on the track you record into, and turn on monitoring so audio reaches the plug-in. Wear headphones.
2. Open the plug-in window and speak. When the level bar on the left moves, the signal is arriving.
3. Follow the steps from Gain to Position. Direction is optional.
4. Read the report, change the one thing it names, and retest that part.

## Privacy

Audio is analysed in memory only. It is never written to disk, uploaded or logged, and the plug-in makes no network requests.
Details: [docs/PRIVACY.md](docs/PRIVACY.md).

## Build from source

You need CMake, a C++17 compiler and a JUCE 8.0.8 checkout (the commit is in `.juce-commit`).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DJUCE_PATH=<JUCE checkout>
cmake --build build
```

`-DMICIRI_CORE_ONLY=ON` builds only the analysis core and its tests, and does not need JUCE. Contributors should also read
[CONTRIBUTING.md](CONTRIBUTING.md) for the local checks.

## FAQ

**What does Miciri do?** It runs a few short tests on your microphone signal and tells you the one thing worth changing first
for a better vocal recording, with the numbers behind it.

**Does it change my sound?** No. Audio passes through bit for bit. Miciri never mutes, filters or processes the signal.

**Does it record or upload my audio?** No. Audio is analysed in memory while it arrives and is not kept. Nothing leaves your computer.

**Which DAWs does it work in?** Hosts that load VST3 or Audio Units can load it, but none has a full test record yet. See the
table in [docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md).

**Why does macOS block it?** The macOS builds are not signed or notarized, so Gatekeeper blocks a downloaded copy. The user
guide shows how to clear the flag.

**Is Miciri a replacement for treating my room?** No. It points at the one change likely to help most, and shows the numbers so
you can check the result yourself.

## Documentation

- [User guide](docs/USER_GUIDE.md) ([简体中文](docs/USER_GUIDE.zh-CN.md)): install, every step, glossary, FAQ
- [Known issues](docs/KNOWN_ISSUES.md), [Privacy](docs/PRIVACY.md)
- [Changelog](CHANGELOG.md), [Third-party notices](THIRD-PARTY-NOTICES.md), [Security policy](SECURITY.md)
- [Architecture](docs/ARCHITECTURE.md), [Contracts](docs/contracts/), [Contributing](CONTRIBUTING.md)

## Related projects

[SCVB](https://github.com/synchain-oss/scvb): automatic pan and level balancing for multi-singer vocal arrangements, from the same team.

## License

Licensed under [GPL-3.0-or-later](LICENSE). Third-party components keep their own licenses, see
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md). Made by [Synchain](https://synchain.ca).
