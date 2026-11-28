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
- Opening screen of the plug-in window: until the page draws its first frame, the plug-in paints the page's own purple-to-pink
  gradient with its three soft halos, instead of a pale grey that looked like a white window. A page smoke test checks that its
  colours, angle and halo positions match the page's design tokens.
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

### Changed

- You start and finish each part of a test step. The Start button on the preparation page starts the first part; every later
  part waits for Start, with no countdown and no time limit on the wait. Finish becomes available once the part has the minimum
  it needs (for example a few seconds of voice, a few good claps, or six syllables of a word list), so a word list can end as soon
  as it is read, and each of the four directions is started and finished by hand. A part still ends by itself at its old time
  limit, and a part that has to be redone goes back to waiting instead of restarting after a countdown. A short stretch right
  after Start and right before Finish is left out of the analysis, so the mouse click is not taken for a mouth click or a clap.
  The host-test probe keeps the automatic timeline.
- The light claps of the position step are no longer timed when you start and finish parts yourself. The part keeps listening,
  so you can walk back to where you sing after pressing Start; every clap gets feedback on the spot (valid, clap softer, not
  measured), and the part moves on to the word list by itself once it has three valid claps. A clap that clipped no longer
  sends the whole part back at the end: it just does not count. Only if there are still fewer than two valid claps after
  20 seconds (`session.soft_clap_max_s`, new) does the part go back to waiting.
- The room claps and the light claps show the count of valid claps as the big number instead of a countdown. One clap before they
  end by themselves they say "one more and it finishes (moves on) by itself", with a line below that Finish is available now if
  you can't get one more, instead of "that is enough, press Finish".
- Natural reading in the position step, when you start and finish parts yourself, lasts 30 seconds (`session.natural_read_s`,
  new) and ends by itself. The passage is longer, and you read it again from the top until time is up. Finish becomes available
  after 20 seconds of voice (`session.natural_min_voice_s`, was 8). The host-test probe keeps 15 seconds.
- Direction step: the first direction also waits for Start, so you can sit back and set the microphone after the preparation page.
  Directions are named by which side of the microphone faces you (front, side, back, other side), each a further quarter turn the
  same way, instead of by angles. The number on the direction card is always positive: when the direction is right, it is how much
  louder it is than the loudest other direction; otherwise the card says which side is louder and by how much.
- Sibilance card: its number is now "6–12 kHz vs vowel", the value that is compared with the reference. The absolute levels of
  the 3–6 kHz and 6–12 kHz bands, which only compare with an earlier test at the same gain, are now only in the details, and
  "last time → now" on the card compares the same number as the card shows.
- Mouth-click card: it says how many seconds of speech the rate per minute was counted over (both word lists and the natural
  reading). The loudest click, measured above 2 kHz, is now only in the details, where it is given against your average speech
  level; the details used to call that level the speech peak. Readings, references and thresholds are unchanged.

### Fixed

- Connect page: it never started listening by itself, so even with sound coming in it stayed on "Waiting for sound" and "Start
  the gain test" stayed grayed out. It now listens as soon as the plug-in window opens, one round after another until the
  signal is fine, on whichever page you are, and stops when you close the plug-in window. If the plug-in cannot start yet (for
  example before the host has started its audio), it tries again a few seconds later. The connect step counts as passed only
  while the signal is fine (or only very low): with no audio, or with samples that are exactly zero, the window opens on the
  connect page again, the step bar and the folded summary say what is wrong instead of "Signal OK", and "Start" on the other
  steps is locked as on the connect page.

- Level meter: the blue liquid was the average level of only the last audio block while the peak line and the scale were the
  peak, so a voice's liquid sat far below the line (and below what the gain step then judged). The liquid is now a peak meter:
  it follows the true peak (4x oversampled, the same interpolation as the gain step) between two reads, rises at once and falls
  slowly, and the peak line is kept. The average level (RMS over the same interval) is a paler core inside it. The large-type
  bar in the test view reads the true peak too.
- Clip lamp: it used to stay on until clicked. It now goes out 5 seconds after the last clip, and a click still clears it at once.
- Direction card: when the repeat did not confirm that another direction was louder, the card showed 0 dB. It now shows the
  difference from the first measurement.
- Plug-in window: the UI never finished starting in any host (first seen in Cubase 15 on Windows, then reproduced in REAPER).
  The JUCE front-end bridge script was compiled in without the file it imports, so the page could not make a single call to the
  plug-in and switched to the fallback panel after 15 s. Both files are compiled in now, and a test starts the real page through
  the real bridge in WebView2.
- Plug-in window start-up: the page reports its start-up stages, so a failure switches to the fallback panel at once instead of
  after the 15 s watchdog, and the diagnostics show which stage was reached (`script … > frame … > bridge … > ready …`). The
  window is revealed as soon as the shell is drawn instead of after a 3 s timeout. The fallback panel follows the UI language
  (English or Simplified Chinese).
- Host-test build: the probe JSON also counts `uiReady`, `uiFallback` and `bridgeRoundTrips`.

### Known limits

- Most thresholds are starting values or have only been checked on synthetic signals; a few are calibrated on real recordings,
  and those come from a small batch (3 rooms, 2 speakers, 2 microphones). The bands are starting judgements, not a certification.
- Only REAPER on Windows (VST3) has automated host test results; they cover audio, the UI start-up and, with a synthetic input,
  screenshots of the level meter and the clip lamp. Cubase 15 on Windows has been tried by hand (which found the start-up defect
  above; the fixed build passed the re-test: the UI starts and the test flow runs through, while transport, arm and monitoring
  states and offline export were not tested); no other host has been tested by hand, and the macOS builds have only passed the
  checks on the continuous-integration runner.
  See [docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md) and [docs/DAW_COMPATIBILITY.md](docs/DAW_COMPATIBILITY.md).
