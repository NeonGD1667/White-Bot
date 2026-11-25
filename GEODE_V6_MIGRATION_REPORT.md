# Geode v6.0.0 stable migration report — White Bot

## 1. Identity and target

| Item | Value |
| --- | --- |
| Upstream | `https://github.com/NeonGD1667/White-Bot` |
| Source branch | `NeonBrach` |
| Original HEAD | `9726d22d88d8a6b9cceaf4fb147b699c5c058006` ("Update CMakeLists.txt") |
| Working branch | `port/geode-v6-stable-2.2091` (created from that HEAD) |
| Mod version | `3.2.0` |
| Geode target | **v6.0.0**, commit `fcbf84c15a18924df29c01636d46450f935c220c` |
| Bindings | `c46bcdfc511b992200bb3818f7c1de1f41dc8fde` (`bindings/2.2091`) |
| Geometry Dash | **2.2091** on win / android / macos / ios |

`NeonBrach` was not modified or force-pushed. All work is on the port branch.

## 2. Commit history on the port branch

| Commit | Purpose |
| --- | --- |
| `4ae29db` | `chore(deps)`: geode 6.0.0, CMake guard, FFmpeg pin + vendored default, Android32 LFS shim, `.gitignore` |
| `436e328` | `ci`: pin actions to full SHAs, add `verify-pins` job and `ci/verify-pins.sh` |
| `b38d848` | `refactor(ui)`: TextInput v6 callbacks, popup `getDisplayNode()` |
| `759177e` | `fix(practice)`: PlayerData snapshot for 2.2091 |
| `c002b44` | `fix(download)`: `asp::Duration` timeouts |
| (final HEAD) | `docs`: this report and the four companion documents |

Files changed across the migration: 29 (plus the five reports). See
`GEODE_V6_API_AUDIT.md` for the per-file API mapping.

## 3. What was migrated

### 3.1 TextInput (12 UI files)

`geode::TextInput` in v6 no longer wraps `CCTextInputNode` and has no
`TextInputDelegate`. Every affected popup/settings layer was moved to
per-input callbacks:

- `getInputNode()->setDelegate(this)` → `setCallback(lambda)`
- `getInputNode()->setAllowedChars(...)` → `setFilter(...)`
- `getInputNode()->setMaxLabelLength(n)` → `setMaxCharCount(n)`
- `textChanged(CCTextInputNode*)` → `textChanged()` / `onPageInput` /
  `onFrameInput` / `onSearchInput`, bound to the specific field
- IME detach and caret hacks → `unfocus()`

Affected: autoclicker, autosave, auto-swift, swift-click, fake-taps,
pathfinder, trajectory, noclip, macro editor, macro timeline, load-macro
search, render settings. Vanilla `CCTextInputNode` fields in render settings
(args/audio args/video args/seconds) were deliberately kept with their
delegate override.

Behaviour preserved: filters (byte-for-byte, including the `ñ` extension
filter), character limits, defaults/placeholders, "Restore"/clear buttons,
disabled states, save keys, and focus/close behaviour. Section 1 of the API
audit documents why `setString` and `setMaxCharCount` do not fire callbacks
and why that matches the old behaviour.

### 3.2 Popup / Button

`Popup::m_closeBtn` is a `geode::Button` in v6; `getNormalImage()` is gone.
Replaced with `getDisplayNode()` in load-macro, macro-index, and record
popups, keeping the original scales (`0.6f`, `0.6f`, `0.575f`). Layout,
init sizes, titles, button menus, and close handling were not redesigned.

### 3.3 Geometry Dash 2.2091 bindings

`PlayerObject::m_stateNoStickX`, `m_stateNoStickY`, and `m_stateUnk2` no
longer exist; `m_stateUnk` is now a `float` aliasing `m_unusedFloatA80`.
The practice snapshot struct and its save/apply functions were updated. No
other 2.2091 renames were needed: the full source compiles against the
pinned bindings.

### 3.4 WebRequest

`timeout()` takes `asp::Duration`; both updater calls now use
`asp::Duration::fromSecs(...)`.

### 3.5 Build system and dependencies

- `mod.json`: `geode: 6.0.0` (stable), GD 2.2091.
- `cmake/VerifyDependencies.cmake`: hard checks for SDK version/commit and
  bindings 2.2091/commit, with explicit opt-out flags.
- FFmpeg: vendored under `src/renderer/ffmpeg` (default), SHA-256 pinned
  release fallback, full library hash manifest in `ci/ffmpeg-libs.sha256`.
- Android armeabi-v7a LFS shim for `ftello64`/`fseeko64` (API 23).
- CI: all actions pinned by full SHA, SDK/bindings pinned by version/SHA,
  pre- and post-build verification.

Dependency revisions are listed in `DEPENDENCIES.md`; the CI lock is
described in `CI_DEPENDENCY_LOCK.md`.

## 4. Audits performed (no change required unless noted)

- **MDTextArea**: two call sites, both use the still-present
  `create(std::string, CCSize)`. Rendering, scrolling, placeholders,
  sizing, and default config are untouched.
- **Filesystem / error types**: `fs_compat.hpp` already bridges `asp::fs`.
  `std::filesystem` was kept where a std path/stream is genuinely required
  (replay, FFmpeg backend, `toStd`). `asp::Result` and `numFromString` error
  handling unchanged. Compile-verified on Android against asp 1.6.4.
- **Events / keybinds**: Geode `KeybindSettingV3`; five listeners registered
  in `$on_mod(Loaded)`; no legacy `EventFilter`/`EventListener`.
- **Hooks**: 54 `$modify` classes; signatures compile against 2.2091 on
  Android. iOS keyboard-dispatcher hook remains disabled (pre-existing).

## 5. Verification status

| Category | Status |
| --- | --- |
| Source migrated | Complete for all identified breakages |
| Static checks | Passed |
| Host tests | Passed (17 tests / 280 checks / 0 failures) |
| CMake configuration | Passed on clean Android64/Android32 trees |
| Compile verified | Android arm64-v8a and armeabi-v7a |
| Packaging verified | `.geode` produced for both Android ABIs |
| Runtime verified | **No** |
| Windows compile | **Not verified** |
| macOS compile | **Not verified** |
| iOS compile | **Not verified** |
| CI executed on GitHub | **No** (local script + YAML validation only) |

Details, command lines, and log paths are in `BUILD_AND_TEST_REPORT.md`.

## 6. Known issues and blockers

1. **Runtime behaviour is unverified.** No device/emulator/Geometry Dash
   process was available. Text-input focus, keyboard behaviour, recording,
   rendering, and FFmpeg playback are untested at runtime.
2. **Only Android was compiled.** Windows, macOS, and iOS remain
   source-migrated but unverified. Platform-only paths (WinAPI, FMOD,
   Apple frameworks, iOS keyboard hook) were not exercised.
3. **No GitHub Actions run.** The workflow is syntactically valid and its
   shell scripts pass locally, but GitHub-hosted execution was not possible.
   Reproducibility is therefore verified locally, not in CI.
4. **arma-v7 >2 GiB files**: the `ftello64`/`fseeko64` shim uses 32-bit
   `off_t` and returns `EOVERFLOW` beyond `INT_MAX`. Acceptable for replay
   and render files; documented in the shim.
5. **Adapter/differential replay tests unavailable**: `gdr_adapter_check`
   needs `src/replay/test/oracles/stubs` (absent), and the legacy adapter
   pulls in Geode headers. Not a migration regression.
6. **Pre-existing keybind gaps** (unchanged): `handleHoldForward` is defined
   but never registered; `g.keybinds`/`g.allKeybinds` are unused because
   `updateKeybinds()` is a TODO.
7. **No push**: the sandbox has no credentials for the upstream repository.
   The branch and commits exist locally and are delivered as a source ZIP;
   the final commit SHA is reported in the hand-off summary.

## 7. Delivered artifacts

- `White-Bot-Geode-v6.0.0-2.2091.zip` — complete migrated source tree
  (build directories, logs, and generated binaries excluded).
- This report, `GEODE_V6_API_AUDIT.md`, `DEPENDENCIES.md`,
  `CI_DEPENDENCY_LOCK.md`, `BUILD_AND_TEST_REPORT.md`.
- Build/test logs: configure and build logs for Android64/Android32 clean
  builds, host test build/run logs, static checks, guard tests, pin
  verification, CLI build, adapter-check failure log.

The ZIP is a source delivery, not a claim of a verified release build.
