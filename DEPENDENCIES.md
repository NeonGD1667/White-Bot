# Dependency pins — White Bot (Geode v6.0.0 / GD 2.2091)

Every revision below was resolved from the live remote on 2026-11-25 and is
enforced by the build (`cmake/VerifyDependencies.cmake`) and/or
`ci/verify-pins.sh`. Nothing here is a moving branch.

## Toolchain and SDK

| Component | Revision / version | How it is pinned |
| --- | --- | --- |
| Geode SDK | `v6.0.0` → `fcbf84c15a18924df29c01636d46450f935c220c` | `VERSION` must equal `6.0.0`; `git rev-parse HEAD` must match (CMake guard) |
| Geode bindings | `c46bcdfc511b992200bb3818f7c1de1f41dc8fde` (`main`, 2026-11-25, "menu bindings (#1601)") | `bindings/2.2091` must exist; HEAD must match (CMake guard + `bindings-ref` in CI) |
| Geode CLI | `4.0.2` → `e1dd5c280c205afee3b34460a1b49068da65088f` | checked by `ci/verify-pins.sh`; binary version printed at configure time |
| `geode-sdk/build-geode-mod` | `dce36b35e1c3d9d8af92347760b12eba3caf4004` | workflow `uses:` full SHA |
| `actions/checkout` | `v4.2.2` → `11bd71901bbe5b1630ceea73d27597364c9af683` | workflow `uses:` full SHA |
| `actions/upload-artifact` | `v4.6.2` → `ea165f8d65b6e75b540449e92b4886f43607fa02` | workflow `uses:` full SHA |

`mod.json` targets `geode >= 6.0.0` for `win`, `android`, `macos`, `ios`,
each at GD `2.2091`.

## Transitive libraries pulled by Geode v6.0.0

Geode `CMakeLists.txt` adds these through CPM. The revisions observed in the
build cache match the SDK's declarations:

| Library | Tag / commit declared by Geode | Resolved commit |
| --- | --- | --- |
| `geode-sdk/result` | `#1668d22` | `1668d22ec3385453a9dd3cb029dee6ce5ed83747` (`v1.5.0-1-g1668d22`) |
| `geode-sdk/json` | `@4.1.0` | `72844b6196202caf4e7aab0dc2868cfb10d9b981` (`v4.1.0`) |
| `geode-sdk/nontype_functional` | `#6d1d08c` | `6d1d08cb447c038e3e56812dd362e61abc92933a` |
| `fmtlib/fmt` | `#12.2.0` | `1be298e1bd68957e4cd352e1f676f00e07dcfb57` (`12.2.0`) |
| `dankmeme01/asp2` | `@1.6.4` | `28d057e964dddad5fbadefbdb9ecb70b5d5b03d5` (`v1.6.4`) |
| `dankmeme01/arc` | `@1.6.6` | `c2da8265f9e169a6f4635bbabd700124d6f0f3d8` (`v1.6.6`) |
| `geode-sdk/TulipHook` | `#50e729f` | `50e729fe10cc6b98b33a35d8b26b50bc2fb98dba` (`v3.1.15-6-g50e729f`) |

These are pinned by the SDK, not by White Bot; the guard verifies the SDK
commit, which transitively fixes them.

## FFmpeg package

White Bot embeds a prebuilt FFmpeg package instead of depending on
`eclipse.ffmpeg` at runtime.

- Project: `NeonGD1667/White-Bot-FFMPEG`
- Version: FFmpeg **8.1.2-r1**, GPL build with x264
- Archive: `https://github.com/NeonGD1667/White-Bot-FFMPEG/releases/download/v1.0.0/white-bot-ffmpeg-8.1.2-r1-gpl.tar.gz`
- SHA-256: `77593abf2c821bdf7e04d927dd2415345033d1536c2b2a12c7addd176eed19f1`
- Enforced in `CMakeLists.txt` via CPM `URL_HASH SHA256=...`.

The same files are vendored inside this repository at
`src/renderer/ffmpeg/{include,lib}` and are the default source
(`-DWB_FFMPEG_DIR` defaults to `src/renderer/ffmpeg`). A full `diff -rq`
against the release tarball produced no differences, so a clean checkout
builds hermetically without downloading anything. `ci/ffmpeg-libs.sha256`
records SHA-256 for the 35 static libraries (Android arm64/armv7, iOS arm64,
macOS universal, Windows x64).

Targets provided by the package: `arm64-v8a`, `armeabi-v7a`, `ios-arm64`,
`macos-universal`, `windows-x64` (MSVC ABI, dynamic CRT `/MD`).

## How the pins are enforced

1. **CMake configure** (`cmake/VerifyDependencies.cmake`, included right
   after `project()`):
   - `GEODE_SDK` must be set, contain `CMakeLists.txt`, and `VERSION` must
     equal `6.0.0` (alpha/beta rejected).
   - If the SDK is a git checkout, `HEAD` must equal `fcbf84c...`.
   - `GEODE_BINDINGS_REPO_PATH` must contain `bindings/2.2091`; if it is a
     git checkout, `HEAD` must equal `c46bcdf...`.
   - Escape hatches exist only for deliberate experiments:
     `-DWB_ALLOW_SDK_REVISION_MISMATCH=ON`,
     `-DWB_ALLOW_BINDINGS_REVISION_MISMATCH=ON`.
   - If `GEODE_BINDINGS_REPO_PATH` is unset, configure warns that Geode will
     clone the bindings `main` branch (not reproducible).
2. **CI** (`.github/workflows/build.yml`): a `verify-pins` job runs
   `ci/verify-pins.sh` before any build; the build job passes
   `sdk: 6.0.0` and `bindings-ref: c46bcdf...` to the pinned action, and a
   post-build step re-runs the script against the checkouts the action used.
3. **`ci/verify-pins.sh`** also checks the vendored FFmpeg library hashes and
   prints the release URL/hash.

## Known dependency caveats

- The Android `armeabi-v7a` x264/FFmpeg build uses 64-bit file offsets and
  references `ftello64`/`fseeko64`, which bionic only exports from API 24.
  White Bot builds with API 23, so `src/renderer/ffmpeg/android32_lfs_compat.cpp`
  provides thin fallbacks. Files larger than 2 GiB are not supported on that
  ABI by this shim.
- Geode v6 itself still defaults the bindings to the `main` branch. That is
  why `GEODE_BINDINGS_REPO_PATH` / `bindings-ref` is mandatory for a
  reproducible build.
