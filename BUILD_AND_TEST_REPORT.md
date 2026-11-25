# Build and test report

Host: Linux x86_64 cloud sandbox. CMake 4.4.5, Ninja 1.10.1, g++ 13.4.0,
Android NDK r29 (`29.0.14206865`), Geode CLI 4.0.2 (built from
`e1dd5c2`).

## Summary of verification levels

| Level | Result |
| --- | --- |
| Source migrated | yes — all identified v6 breakages fixed |
| Static checks | passed (`/data/logs/static-checks.log`) |
| Host tests | passed — 17 tests / 280 checks / 0 failures |
| CMake configuration | passed for Android64 and Android32 (clean dirs) |
| Compile verified | Android arm64-v8a and armeabi-v7a only |
| Package verified | both Android ABIs produced a `.geode` |
| Runtime verified | **no** — no device/emulator run |
| Windows / macOS / iOS | **not compiled** in this environment |

## 1. Static checks

`/data/logs/static-checks.log` — searches over `src/`:

- obsolete `geode::TextInput` calls (`getInputNode`, `setDelegate`,
  `setAllowedChars`, `setMaxLabelLength`): 0 (matches are comments only)
- `m_closeBtn->getNormalImage()`: 0
- `RichTextArea`, `gd::string`, `Task.hpp`, `Signal.hpp`: 0
- `std::filesystem` occurrences are confined to the replay format, FFmpeg
  backend, and `fs_compat.hpp`

Workflow YAML was parsed with Python `yaml.safe_load` (both files OK).

## 2. Dependency guard tests

`/data/logs/guard-tests.log` (runs `cmake -P cmake/VerifyDependencies.cmake`
in isolated temp trees):

| Case | Expected | Result |
| --- | --- | --- |
| SDK `VERSION` = `6.0.0-alpha.1` | reject | exit 1, "version mismatch: found '6.0.0-alpha.1'" |
| bindings tree without `2.2091` | reject | exit 1, CMake error from the guard |
| bindings at a different commit | reject | exit 1, "Bindings revision mismatch" |
| pinned SDK + bindings | accept | exit 0, both revisions printed as pinned |

`/data/logs/verify-pins.log` — live `ls-remote` verification:

```
geode              v6.0.0 -> fcbf84c15a18924df29c01636d46450f935c220c
bindings           main   -> c46bcdfc511b992200bb3818f7c1de1f41dc8fde
cli                main   -> e1dd5c280c205afee3b34460a1b49068da65088f
build-geode-mod    main   -> dce36b35e1c3d9d8af92347760b12eba3caf4004
35 vendored FFmpeg libraries match ci/ffmpeg-libs.sha256
```

## 3. Host tests (replay formats)

Command (also written to `/data/logs/test-gdr-build.log`):

```sh
g++ -std=c++23 -O2 -I src/replay -I src -I src/replay/slc \
    src/replay/test/gdr_tests.cpp src/replay/gdr.cpp -o /data/tests/gdr_tests
/data/tests/gdr_tests
```

Result (`/data/logs/test-gdr.log`):

```
17 test(s), 280 check(s), 0 failure(s)
exit=0
```

Covers GDR1 (MessagePack/JSON), GDR2, WBR round-trips, conversions,
metadata/input preservation, framefix/extensions, DLL-bot offsets, malformed
input, loss policy, detection, HLR stub, file I/O, and a mutation fuzz pass.

Not run: `gdr_adapter_check` and the differential oracles. Their build needs
`src/replay/test/oracles/stubs` (not present in the repository) and
`legacy_gdr.cpp` includes `<Geode/Geode.hpp>`; the standalone compile fails
as logged in `/data/logs/build-adapter-check.log`. These tests are
unavailable, not failing.

## 4. Android builds (clean directories)

Both used:

- `GEODE_SDK=/data/deps/geode` (`fcbf84c`), `GEODE_BINDINGS_REPO_PATH=/data/deps/bindings` (`c46bcdf`)
- `CPM_SOURCE_CACHE=/data/cpm-cache` (audited revisions)
- `ANDROID_NDK=/data/toolchains/android-ndk-r29`, `CMAKE_BUILD_TYPE=RelWithDebInfo`
- `-DWB_FFMPEG_DIR=/data/whitebot/src/renderer/ffmpeg` (vendored)
- `-DGEODE_DONT_INSTALL_MODS=ON` (no local Geode profile)
- fresh build dirs `build-android64-clean`, `build-android32-clean`

### Android 64 (arm64-v8a, API 23)

```sh
cmake -S /data/whitebot -B /data/whitebot/build-android64-clean -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_TOOLCHAIN_FILE=/data/toolchains/android-ndk-r29/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-23 \
  -DGEODE_TARGET_PLATFORM=Android64 \
  -DWB_FFMPEG_DIR=/data/whitebot/src/renderer/ffmpeg \
  -DGEODE_DONT_INSTALL_MODS=ON
cmake --build /data/whitebot/build-android64-clean
```

Configure log: `/data/logs/configure-android64-clean.log` (Clang 21.0.0,
SDK 6.0.0 pinned, bindings pinned).
Build log: `/data/logs/build-android64-clean.log` — 67 targets, ends with
`Linking CXX shared library homeless.white-bot.android64.so` and
`Successfully packaged homeless.white-bot.geode` (21,543,355 bytes).

### Android 32 (armeabi-v7a, API 23)

Same command with `-DANDROID_ABI=armeabi-v7a -DGEODE_TARGET_PLATFORM=Android32`
and build dir `build-android32-clean`.

Build log: `/data/logs/build-android32-clean.log` — 68 targets (one extra for
`android32_lfs_compat.cpp`), ends with a successful link and
`Successfully packaged` (21,191,186 bytes, `homeless.white-bot.android32.so`).

Notes:

- The 32-bit package first failed to link: the prebuilt x264/FFmpeg uses
  `ftello64`/`fseeko64`, absent from bionic before API 24. The guarded shim
  in `src/renderer/ffmpeg/android32_lfs_compat.cpp` resolves this. It is
  compiled only for `__ANDROID__ && __arm__ && __ANDROID_API__ < 24`.
- An earlier packaging attempt failed with "No Geode profiles found!" because
  `geode package new --install` expects a local profile; setting
  `GEODE_DONT_INSTALL_MODS=ON` (as CI does) skips install and packaging
  succeeds.

## 5. Geode CLI build

The CLI was built from source at `e1dd5c2` (`cargo build --release`) in
`/data/logs/cli-build.log`; `geode --version` prints `geode 4.0.2`.

## 6. What was not verified

- **Runtime**: no Geometry Dash process, device, or emulator was available.
  No claim is made that the mod loads or that features behave at runtime.
- **Windows**: needs MSVC/clang-cl + the Windows FFmpeg archive; not run.
- **macOS**: needs an Apple toolchain; not run.
- **iOS**: needs Xcode; not run. The iOS keyboard hook remains disabled
  (pre-existing).
- **CI execution**: the GitHub Actions workflow was validated syntactically
  and its scripts were run locally, but no GitHub-hosted run occurred.
- **Adapter/differential replay tests**: unavailable (missing stubs /
  reference checkouts), as described above.

Because compile verification is limited to Android, hook signatures and
platform-specific code for Windows/macOS/iOS remain "source migrated,
unverified".
