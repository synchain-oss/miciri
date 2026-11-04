<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# packaging

Installers for the two supported platforms. Both are unsigned and not notarized.

| Platform      | Output                          | Contents                                                              |
| ------------- | ------------------------------- | --------------------------------------------------------------------- |
| Windows x64   | `Miciri-<version>-win64-setup.exe`     | VST3 into the standard VST3 folder; licenses, notices and uninstaller in `Program Files\Miciri` |
| macOS arm64   | `Miciri-<version>-macos-arm64.pkg`     | VST3 and Audio Unit under `/Library/Audio/Plug-Ins`; licenses and notices under `/Library/Application Support/Synchain/Miciri` |

The version in the file names always comes from `project(Miciri VERSION …)` in `CMakeLists.txt`.

- `scripts/package.ps1` builds one platform's installer from an existing Release build tree and checks it
  (`-Verify`: silent install and uninstall on Windows, payload comparison on macOS). `-Collect` checks the
  two installers together and writes `SHA256SUMS` and the release notes. `-SelfTest` runs the logic tests.
- `manifest-win64.txt` and `manifest-macos-arm64.txt` list the root entries each install tree must have, no more, no less.
- `win/miciri.iss` is the Inno Setup script; `win/innosetup.sha256` pins the Inno Setup version and the SHA-256 of its
  official installer (CI downloads it and checks the hash). To upgrade, change the version and hash together.
- `*/INSTALL.txt.in` are the templates of the `INSTALL.txt` that ships in each installer, including the corresponding-source pointers.
- `.github/workflows/release.yml` turns a `vX.Y.Z` tag on `prod` into a **draft** release; a maintainer publishes it.

Release builds (tag-triggered) are stricter: `THIRD-PARTY-NOTICES.md` and every license text for linked components must exist.
