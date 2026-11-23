<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Changelog

All notable changes are recorded here, newest first. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and versions follow [Semantic Versioning](https://semver.org/). Each release also gets a bilingual (English and Chinese) note on
its GitHub release page.

## [Unreleased]

There is no release yet. The first one will be `0.1.0`. Work so far:

### Added

- Analysis core (no JUCE dependency, double precision): peak, true peak and clipping; noise floor; mains hum; channel check on
  stereo tracks; room reverberation (T20); direct-to-room ratio at the singing position; direction check; plosives,
  sibilance and mouth noise; and the choice of "the one thing worth changing first".
- Distance is a before/after comparison, not a scale: the first time there is only a weak "maybe too far" hint, and after a retest
  at another position the direct-to-room ratio, plosives and sibilance are compared with last time. The direct-to-room ratio,
  plosives, sibilance and mouth noise are not graded.
- Reference values (rules of thumb, not band limits) for plosives, sibilance and mouth noise, with a short hint on the card when
  one is exceeded, and a recommended distance by microphone type.
- Readings that no longer depend on where the analysis window starts: mouth noise, plosives, sibilance and the direction levels.
  A plosive needs a real vowel after its onset, so a low thump with only room noise behind it is not counted as a syllable.
- Every threshold carries a state (pending, synthetic, calibrated) and its evidence; `docs/ALGORITHMS.md` explains each reading,
  the limits of the real recordings behind it, and lists the thresholds, generated from `thresholds.json`.
- Plug-in: VST3 for Windows and macOS, Audio Unit for macOS, with bit-for-bit pass-through. Tests do not run while the host
  renders offline. A host-test build option writes counters to a JSON file; releases do not contain it.
- Plug-in window (English and Simplified Chinese): the step bar and stacked cards, the level meter with result marks, the
  large-type mode for testing from a distance, the report with retest and "last time → now", the detail view with evidence
  waveforms, and a seven-step in-app guide that opens once on first launch.
- Offline fonts for the window: subsets of Space Grotesk, IBM Plex (shipped as Miciri Sans and Miciri Mono) and Noto Sans SC under
  the SIL Open Font License, compiled into the plug-in. They are rebuilt by `scripts/fetch_fonts.py` and checked by the local checks.
- Installers and zips: Windows installer (with a Simplified Chinese wizard) and zip, macOS package and zip. All unsigned.
- Documentation: README (English and Chinese), user guide, privacy page, known issues, third-party notices with the full
  license texts that the binaries need.
- Local checks and CI: formatting, license and privacy checks, a golden-data test for the analysis, a page smoke test with
  an accessibility scan.
- Tools: `miciri-analyze` prints the continuous readings of a WAV file, with `--mode` for the room, direct-to-room ratio,
  plosives, sibilance, mouth noise and direction. The host-test build can run the test steps from a script and write the readings
  of each step to a file ([docs/HOST_PROBE.md](docs/HOST_PROBE.md)); releases do not contain it.

### Known limits

- Most thresholds are starting values or have only been checked on synthetic signals; a few are calibrated on real recordings,
  and those come from a small batch (3 rooms, 2 speakers, 2 microphones). The bands are starting judgements, not a certification.
- Only REAPER on Windows (VST3) has automated host test results; no host has been tested by hand, and the macOS builds have only
  passed the checks on the continuous-integration runner.
  See [docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md) and [docs/DAW_COMPATIBILITY.md](docs/DAW_COMPATIBILITY.md).
