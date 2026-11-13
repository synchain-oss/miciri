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
  sibilance and mouth noise; the too-close / too-far scale and the choice of "the one thing worth changing first".
- Plug-in: VST3 for Windows and macOS, Audio Unit for macOS, with bit-for-bit pass-through. Tests do not run while the host
  renders offline. A host-test build option writes counters to a JSON file; releases do not contain it.
- Plug-in window (English and Simplified Chinese): the step bar and stacked cards, the level meter with result marks, the
  large-type mode for testing from a distance, the report with retest and "last time → now", and the detail view.
- Installers and zips: Windows installer (with a Simplified Chinese wizard) and zip, macOS package and zip. All unsigned.
- Documentation: README (English and Chinese), user guide, privacy page, known issues, third-party notices with the full
  license texts that the binaries need.
- Local checks and CI: formatting, license and privacy checks, a golden-data test for the analysis, a page smoke test with
  an accessibility scan.

### Known limits

- Every threshold is a starting value that has not been calibrated on real recordings, and no host has a full test record.
  See [docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md).
