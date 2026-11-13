<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

**English** | [简体中文](USER_GUIDE.zh-CN.md)

# Miciri user guide

Miciri (pronounced *MIC-i-ri*) checks your vocal recording setup. It runs a few short tests on the signal from your microphone,
names the one thing worth changing first, and shows the numbers behind it. Audio passes through unchanged: Miciri only
listens. Nothing is recorded or sent anywhere, see [PRIVACY.md](PRIVACY.md).

Contents: [Install](#install) · [Before you start](#before-you-start) · [The window](#the-window) · [The steps](#the-steps) ·
[The report and retesting](#the-report-and-retesting) · [If no signal arrives](#if-no-signal-arrives) · [Glossary](#glossary) · [FAQ](#faq)

## Install

There are two kinds of download for each system. New to plug-ins: use the **installer**. If you manage your plug-in folders
yourself: use the **zip**. Both are unsigned and not notarized. A release also provides a `SHA256SUMS` file; compare it with
the checksum of what you downloaded if you want to be sure the file is intact.

### Windows (x64, VST3)

1. Run `Miciri-v<version>-win64-setup.exe`. The wizard follows your Windows language (English or Simplified Chinese).
   Windows SmartScreen may say the publisher is unknown, because the installer is not signed; choose "More info", then "Run anyway".
2. The installer puts `Miciri.vst3` in the standard VST3 folder (normally `C:\Program Files\Common Files\VST3`) and the license
   texts, notices and the uninstaller in `Program Files\Miciri`.
3. Rescan plug-ins in your DAW.

Zip instead: copy the folder `Miciri.vst3` from the zip into `C:\Program Files\Common Files\VST3` (this needs administrator
rights) and rescan.

The plug-in window uses the Microsoft Edge WebView2 Runtime, which is part of current Windows 10 and 11. If it is missing, the
plug-in shows an explanation panel instead of its window; install the Evergreen runtime from Microsoft.

To uninstall, use Windows Settings > Apps > Miciri, or delete `Miciri.vst3` if you used the zip.

### macOS (Apple silicon, VST3 and Audio Unit)

1. Open `Miciri-v<version>-macos-arm64.pkg` and follow the installer. The VST3 goes to `/Library/Audio/Plug-Ins/VST3` and the
   Audio Unit to `/Library/Audio/Plug-Ins/Components`.
2. **macOS will block the package the first time**, because Miciri is not signed or notarized. This is expected. Do one of these:
   - Open **System Settings > Privacy & Security**, scroll to the Security section, find the message about Miciri being
     blocked, and click **Open Anyway** (the exact wording differs between macOS versions). Then open the package again.
   - Or clear the quarantine flag in Terminal first, with the path of the file you downloaded:
     `xattr -dr com.apple.quarantine <path to the downloaded .pkg>`
3. Rescan plug-ins in your DAW. If the DAW still reports the plug-in as blocked or damaged, clear the flag on the installed
   plug-ins too:

   ```sh
   xattr -dr com.apple.quarantine /Library/Audio/Plug-Ins/VST3/Miciri.vst3
   xattr -dr com.apple.quarantine /Library/Audio/Plug-Ins/Components/Miciri.component
   ```

Zip instead: copy `Miciri.vst3` to `~/Library/Audio/Plug-Ins/VST3` and `Miciri.component` to `~/Library/Audio/Plug-Ins/Components`
(create the folders if they do not exist), then run the two `xattr` commands above with those paths, and rescan. The `INSTALL.txt`
inside the zip and the installer repeats these steps in English and Chinese.

To remove it, delete the installed items. A macOS package has no uninstaller.

## Before you start

- **Wear headphones.** With monitoring on and speakers playing, the microphone hears the speakers and the measurements suffer.
  If you must use speakers, turn them down.
- **Put Miciri on the track you record vocals on**, and make sure sound reaches it: the input is chosen, and monitoring is on if
  your DAW needs that. How to do this differs in every DAW and is not covered here; see your DAW's manual.
- Mono and stereo tracks work. On a mono track Miciri shows one level; on a stereo track it shows left and right and checks that they
  make sense.
- Pick a spot where you will actually record, and leave your gain and distance as you normally use them. Miciri measures what
  it hears, so it cannot tell what you meant to set.
- Leave the DAW playing or monitoring in real time while you test. Miciri does not run tests during an offline render.

The window opens with a large level bar. Speak, and when the bar moves, the signal is arriving.

## The window

- **Left: the input panel.** The level meter (with the true peak and the target zone), the buffer size, a clip lamp that you click
  to clear, and the microphone type (dynamic, condenser, USB, headset or not sure; you can skip it). After tests, the meter also
  shows marks for the noise floor, hum and the loudest mouth click; hover over a mark to see what it is.
- **The step bar** across the top: Connect, Gain, Quiet, Room, Position, Direction (advanced) and Report. Each step is marked not done,
  current, done, stale or failed. Click a step, a card above or below, or "Next" to move; scrolling also turns the page.
- **The corner buttons**: language (English or Simplified Chinese), window scale, the "?" button that opens the in-app guide, and
  "About". The window scale asks you to confirm and goes back by itself after a few seconds if you do not.

While a test runs, the window shows a single large instruction and a block of color for start, success and "try again", so you
can read it from your singing position.

## The steps

Each step is short. The window shows "about N seconds" before you start, and a counter while it runs. Colors follow the result:
green is fine or signal present, amber is worth noticing, red is the one to change first, gray is not measured or stale, and
purple is the current or next step.

### 1. Connect

Choose your microphone type if you know it (or skip), and speak. When the level bar moves, press the start button. This step
also reports the sample rate and buffer size, and on a stereo track whether both channels carry sound. If something looks wrong,
the card says what it may be; see [If no signal arrives](#if-no-signal-arrives).

### 2. Gain (about 8 seconds)

Sing as loudly as you will record, and keep going until the step finishes. Miciri reads the peak level (true peak) and whether
the signal clipped. The target zone is shown on the meter. If you clipped, lower the input volume by a good margin and test
again; if you are far below the zone, raise it. Miciri does not tell you which knob to turn, because it cannot read your
hardware: for a USB microphone it is the system input volume.

### 3. Quiet (about 8 seconds)

Stay silent and still. Miciri measures the noise floor (as a signal-to-noise ratio against your singing level) and looks for
mains hum. If speech or a bump is heard, the step starts over by itself. It also looks for signs that your device is processing
the sound (a noise gate or heavy noise reduction), which would make the other results look better than the room really is.

### 4. Room (about 20 seconds)

Stand about 1 meter from the microphone and clap hard. Every clap gets feedback, and the card counts how many more good claps
it needs. You need four good claps, and Miciri accepts up to eight; a round that is spoiled is marked void. From the decay of
each clap it estimates how long the room rings (T20) in four bands. Clapping hard is what this step wants, so loud claps are fine.
If the card says the device may have a noise gate or noise reduction on, turn it off and clap again.

### 5. Position (about 1 minute)

Go back to the place where you sing, at the distance you normally use.

1. Clap lightly three times.
2. Read the first word list twice, each word on its own, at your singing strength. You do not need a rhythm. For example
   "pop pick pay / top tea key".
3. Read the second word list twice in the same way. For example "see say so / she shoe show".
4. Read a short passage the way you normally speak.

You do not need to memorize the lists and Miciri does not judge whether you said them correctly. It measures how much of what the
microphone hears is the direct voice versus the room (D/R), plosives (the puffs of air on p, t, k), sibilance (the hiss on s and sh),
and mouth clicks. These feed a too-close / too-far scale. If a pass is much quieter than your gain test, Miciri asks you to read it again.

### 6. Direction (optional, about 25 seconds)

Speak toward the microphone for a few seconds in each of four directions. Miciri only answers one question: is the current
direction the loudest and brightest one? If another direction is clearly louder it repeats that pair to be sure, and only then
says the microphone may be the wrong way round. It does not guess your microphone model. If you chose "headset" as the microphone
type, this step is hidden.
Not testing direction never stops the report from saying you are ready to record.

## The report and retesting

The first screen of the report is **the one thing worth changing first**, with one to three concrete actions. Below it are the
result cards. Each shows one word and one main number; click a card to open its detail (readings, ratios and the evidence behind them).

- **Retest only what you changed.** After "Report" you will see "Made a change?" with three buttons: *I changed the gain*, *I moved*,
  *I changed the room*. They mark the affected cards stale, and one click retests only those.
- **Last time → now.** Each card keeps the previous value so you can see whether the change helped. When one problem is solved,
  the next one moves up, and at the end the report says you can start recording.
- **Copy results** puts a one-line summary on the clipboard to paste into a message or a forum post.
- A stale card means its measurement no longer applies, for example after the sample rate changed or you moved the microphone.

## If no signal arrives

Miciri cannot read your DAW's monitoring or arm state, so these hints always say "possibly".

| What the connect card sees | What it may mean |
|---|---|
| No audio callbacks for about 2 seconds | Monitoring may be off, or the plug-in may be asleep or bypassed |
| Audio arrives but is exactly zero from the start | Monitoring may be off, the track may not be armed, or the wrong input is selected |
| Signal was there, then exactly zero | The sound may have been muted, a noise gate may have closed, or the host may have stopped |
| Very low noise that does not rise when you speak | The path works, but the microphone may be disconnected, phantom power may be off, or the gain may be at zero |
| Only one channel on a stereo track | The recording may only land on one side: use a mono track, or check the input |
| The two channels are opposite in phase | The two channels carry the same voice but one is inverted; a mono mix-down could cancel. Check the cable and the interface's phase switch |

Some of these cannot be told apart from a very quiet room with nobody speaking, which is why the main button stays usable.

## Glossary

- **dBFS**: decibels relative to digital full scale. 0 dBFS is the loudest level a digital signal can hold; quieter levels are negative.
- **Peak / true peak (dBTP)**: the highest level of the signal. True peak also catches the peaks that fall between samples.
- **Clipping**: the signal hit the ceiling and was flattened. It sounds harsh and cannot be repaired afterwards.
- **Gain**: how much the input is amplified. Too low drowns the voice in noise; too high clips.
- **Noise floor / SNR**: the steady background noise, and how far your voice sits above it. A larger signal-to-noise ratio is better.
- **Hum**: a steady low tone (and its harmonics) at the mains frequency, 50 or 60 Hz, usually from wiring or power supplies.
- **T20 (reverberation time)**: an estimate of how long a sound takes to die away in the room, taken from the first 20 dB of the
  fade after a clap. Longer means a livelier, more echoey room.
- **D/R (direct-to-room ratio)**: how loud the direct voice is compared with the room's reflections at your position. Low means a lot of room in the recording.
- **Plosive**: the puff of air on p, t and k that overloads the microphone with a low thump.
- **Sibilance**: the sharp hiss on s and sh. Too much sounds piercing.
- **Mouth noise**: small clicks and wet sounds from the mouth. They are usually worse when you are very close.
- **Dynamic / condenser microphone**: a dynamic microphone is rugged and needs more gain; a condenser is more sensitive and needs
  phantom power from the interface. USB microphones have the interface built in.
- **Phase**: the timing of a wave. Two channels in opposite phase cancel each other when they are added into mono.
- **Stale**: a result that no longer matches your setup and should be measured again.

## FAQ

**Does Miciri change my sound?**
No. Audio passes through bit for bit. Miciri only analyzes a copy of what arrives and never mutes, filters or processes the signal.

**Does it record my voice or send it anywhere?**
No. Audio is analyzed in memory while it arrives and is not kept. The plug-in makes no network requests. See [PRIVACY.md](PRIVACY.md).

**Why does macOS say the plug-in cannot be opened or is damaged?**
Miciri for macOS is not signed or notarized, so Gatekeeper blocks downloaded files. Follow the steps in [Install](#install) to allow
the package and to clear the quarantine flag on the plug-ins.

**The level bar does not move. What now?**
Check that you are wearing headphones, that the track's input is selected and monitoring is on if your DAW needs it, and that the plug-in is not bypassed.
Then read [If no signal arrives](#if-no-signal-arrives). Miciri can only say "possibly", because it cannot see those DAW settings.

**Does it work in my DAW?**
Any host that loads VST3 (Windows, macOS) or Audio Units (macOS) can try to load it, but no host has a full test record yet. The
compatibility table is in [KNOWN_ISSUES.md](KNOWN_ISSUES.md) and every untested host says "Not tested".

**The step restarted by itself. Is that a bug?**
No. A step starts over when something spoils it: speech or a bump during the quiet test, a clap that clipped, or a pass much quieter
than your gain test. The card says why.

**Why does it say it cannot test while I render?**
Miciri tests only during real-time playback or monitoring. In an offline render the plug-in would measure the project's audio, not
your microphone. Known limits are in [KNOWN_ISSUES.md](KNOWN_ISSUES.md).

**How accurate are the results?**
The readings are real measurements, but the cut-offs between "good", "fair" and "poor" are starting values that have not yet been
calibrated on real recordings. Use them as guidance and trust your ears for the final word.
