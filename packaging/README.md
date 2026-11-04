<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# packaging

Each platform ships two files: an installer and a zip. Both are unsigned and not notarized. All four release files share the `Miciri-v<version>-` prefix.

| Platform    | Installer                              | Zip                                    |
| ----------- | -------------------------------------- | -------------------------------------- |
| Windows x64 | `Miciri-v<version>-win64-setup.exe`    | `Miciri-v<version>-win64.zip`          |
| macOS arm64 | `Miciri-v<version>-macos-arm64.pkg`    | `Miciri-v<version>-macos-arm64.zip`    |

- Installer: new users should use this. Windows puts the VST3 in the standard VST3 folder and the licenses, notices and uninstaller in `Program Files\Miciri`. macOS puts the VST3 and Audio Unit under `/Library/Audio/Plug-Ins` and the licenses and notices under `/Library/Application Support/Synchain/Miciri`.
- Zip: for people who manage their plug-in folders themselves. It holds the plug-in bundle(s), `LICENSE`, `LICENSES/`, `THIRD-PARTY-NOTICES.md`, `third_party/` and a bilingual (English / Chinese) `INSTALL.txt` that says where to copy the files. The macOS `INSTALL.txt` also has the `xattr` command that clears the quarantine flag.

The version in the file names always comes from `project(Miciri VERSION …)` in `CMakeLists.txt`.

## Parts

- `scripts/package.ps1` builds one platform's installer and zip from an existing Release build tree and checks them
  (`-Verify`). Windows: silent install and uninstall, and the wizard language. macOS: payload comparison. Both: the zip is
  unpacked and every file is compared with the stage. `-Collect` checks the four files together and writes `SHA256SUMS` and the
  release notes. `-SelfTest` runs the logic tests.
- `manifest-win64.txt` and `manifest-macos-arm64.txt` list the root entries each install tree and each zip must have, no more, no less.
- `win/miciri.iss` is the Inno Setup script; `win/innosetup.sha256` pins the Inno Setup version and the SHA-256 of its
  official installer (CI downloads it and checks the hash). To upgrade, change the version and hash together.
- `*/INSTALL.txt.in` and `*/INSTALL-zip.txt.in` are the templates of the `INSTALL.txt` that ships in each installer and in each zip, including the corresponding-source pointers.
- `.github/workflows/release.yml` turns a `vX.Y.Z` tag on `prod` into a **draft** release; a maintainer publishes it.

## Zip reproducibility

The same stage gives the same zip, byte for byte. Entries are files only, sorted by ordinal name, with a fixed time
(`SOURCE_DATE_EPOCH`, else the HEAD commit time, else 1980-01-01, rounded down to an even second) and fixed attributes. The macOS zip
keeps the bundle layout and marks files under `Contents/MacOS` as executable (0755), all others 0644. `scripts/package.ps1` builds each zip
twice and compares the bytes, then reopens it and compares every entry with the stage.

## Simplified Chinese installer

The Windows wizard has two languages: English and Simplified Chinese. `LanguageDetectionMethod=uilanguage` picks the one that
matches the system UI language, `ShowLanguageDialog=no` skips the picker and falls back to English. `/LANG=chinesesimplified`
(or `/LANG=english`) forces one. The license page stays the English GPL text.

The Chinese strings are Inno Setup's unofficial translation, taken unchanged from the official repository
(`Files/Languages/Unofficial/ChineseSimplified.isl` at tag `is-6_7_4`). `win/languages/SOURCE.txt` records the source, the
blob and how to upgrade; `win/ChineseSimplified.isl.sha256` pins the file hash (checked by `scripts/package.ps1` before compiling and
by the `pins` check). The file carries no license header of its own; it is registered in `REUSE.toml` under the Inno Setup License.

Release builds (tag-triggered) are stricter: `THIRD-PARTY-NOTICES.md` and every license text for linked components must exist.
