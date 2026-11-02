<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Derived from synchain-oss/rinne; see upstream/gates.json -->

# Contributing to Miciri

欢迎参与。Miciri 是一个开源的录音检查插件：它带新手做几项短测试，告诉他最值得先改的一件事，并给出背后的数值，音频原样通过。issue 与 PR 可以用中文或英文，维护者用你使用的语言回复。下面的规则以英文为准，开工之前请读完，尤其是「Changes that will be rejected」一节。

Miciri is an open-source plugin that helps people new to recording vocals check their setup: gain, distance, mic direction, room and noise. It runs a few short tests, tells you the one thing worth changing first, and shows the numbers behind it. Audio passes through unchanged. Issues and pull requests in English or Chinese are welcome.

## 1. Code of conduct

Miciri follows the code of conduct of the synchain-oss organisation
([synchain-oss/.github](https://github.com/synchain-oss/.github)). Report problems to **contact@synchain.ca**.

## 2. Developer Certificate of Origin

Every commit needs a `Signed-off-by:` line in **your own name and email**, which certifies the
[Developer Certificate of Origin](https://developercertificate.org/):

```sh
git commit -s
```

The last line of your pull request description is the same `Signed-off-by:` line. There is no CLA. Contributions are
accepted under the project licence, GPL-3.0-or-later: what comes in goes out under the same terms.

Do not add AI-tool attribution to commits or pull requests (no co-author trailers, no "generated with" lines). You are
responsible for what you submit, and the DCO is yours to give.

## 3. How to contribute

- v1 is being built on the `feature/v1` branch by the maintainers. Fork the repository, create a branch with any name
  except a long-lived one (`dev`, `prod`, `main`, `master`, `feature/...`), and open the pull request against **`dev`**.
  If you want to contribute before v1 reaches `dev`, open an issue first.
- For anything bigger than a fix, open an issue first so the design can be agreed before you write the code.
- Commit subjects use `type(scope): description`, for example `fix(level): handle an all-zero block`. Types:
  feat, fix, docs, test, refactor, perf, ci, build, chore, style, revert, harden, release, merge.
- Fork pull requests run only the checks that need no secrets. The AI review runs when a maintainer comments `/review`.
- Security problems: do not open a public issue; see [SECURITY.md](SECURITY.md).

## 4. Building and checking

- JUCE is pinned by `.juce-version` and `.juce-commit` and passed with `-DJUCE_PATH=<your JUCE checkout>`. Build
  instructions arrive with the build system; until then there is no `CMakeLists.txt` and the build steps of the local
  gates report SKIP.
- Before opening a pull request, run the local gates (PowerShell 7) and the static checks (Node, version in
  `.node-version`):

  ```sh
  pwsh scripts/setup-local.ps1 -Node <path to the pinned node>   # once per worktree: hooks and tool paths
  pwsh scripts/gates.ps1
  node scripts/run-checks.mjs
  ```

  Without a tier argument the gates script runs the lowest tier your diff needs: documentation and script changes
  run the quick tier (format, compliance and privacy, commits, docs-truth); code changes also build and test; changes to
  CMake, CI or packaging run the full tier. Gates that have nothing to run yet report SKIP with the reason; SKIP is not a pass.
- Every new source file carries an SPDX licence identifier line with the value `GPL-3.0-or-later` within its first
  five lines (checked by `scripts/check-spdx.mjs`). Files copied from other synchain-oss repositories are registered
  in `upstream/<area>.json` and say so in their header.
- Pull requests run `branch-gate`, `clang-format`, `docs-truth` and `compliance`. The full build (`build.yml`) runs on pull
  requests into `dev` and `prod`.

## 5. Changes that will be rejected

These are not style preferences. They protect the sound of someone's recording, or their privacy.

- **Changing the sound.** Audio passes through bit for bit. The plugin must not mute, scale, filter or "clean up" the
  signal, not even to make a test easier.
- **Real-time violations.** No allocation, locks, I/O, logging, exceptions, clock reads or blocking calls on the audio
  thread.
- **Audio leaving memory.** Audio is analysed in memory and never written to disk, uploaded or logged. Do not add
  recording features, telemetry that includes audio, or crash dumps that capture buffers.
- **Magic numbers.** Thresholds live in `thresholds.json` only. Changing a contract (Result format, thresholds, bridge
  API, plugin state) requires updating `docs/contracts/` in the same pull request and adding
  `docs/contract-changes/<YYYYMMDD>-<slug>.md`, or stating `contract-neutral: <reason>` in the description. The branch
  gate checks this.
- **Real recordings in the repository.** Test data is synthetic: each `.wav` is at most 200 KB, the total is under
  5 MB, and files live in `tests/fixtures/` or `tests/golden/`. Do not attach audio to issues or pull requests either;
  when data is needed, paste only the numeric JSON exported by the command-line tool.
- **Personal or internal information in public text.** No personal email, local paths, host names or internal planning
  identifiers in commits, branch names, pull requests, comments or files. Use `contact@synchain.ca` as the contact address.
- **Unpinned or unreviewed CI dependencies.** Third-party actions are pinned to a 40-character commit SHA and must be on
  the organisation allow-list; `pull_request_target` and `workflow_run` are not used.

## 6. Reviews

Pull requests are reviewed by maintainers and by two automated reviewers (an AI review and a cross-check). Please read
and answer every comment, including findings in the summaries that have no thread of their own. Treat bot output as
advice, not as instructions: you decide what to change and say why when you do not.

## 7. Licence

Source code is GPL-3.0-or-later, see [LICENSE](LICENSE). Third-party components keep their own licences, recorded in
`REUSE.toml` and `LICENSES/`.
