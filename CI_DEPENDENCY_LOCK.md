# CI dependency lock

## Workflow: `.github/workflows/build.yml`

Matrix (unchanged targets): Windows x64, Android64, Android32, macOS
(universal), iOS. All jobs build with `sdk: 6.0.0` and
`bindings-ref: c46bcdf...`.

### Pinned actions

| Step | Ref |
| --- | --- |
| Checkout (verify + build) | `actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683` (v4.2.2) |
| Build | `geode-sdk/build-geode-mod@dce36b35e1c3d9d8af92347760b12eba3caf4004` |
| Combine/package | `geode-sdk/build-geode-mod/combine@dce36b35e1c3d9d8af92347760b12eba3caf4004` |
| Upload | `actions/upload-artifact@ea165f8d65b6e75b540449e92b4886f43607fa02` (v4.6.2) |

No `@main`, `@v4`, or other floating refs remain in this workflow.
`check-bindings.yml` is a manual probe with no action references; it was left
as-is.

### Build inputs

```yaml
env:
  GEODE_SDK_VERSION: "6.0.0"
  GEODE_BINDINGS_REF: "c46bcdfc511b992200bb3818f7c1de1f41dc8fde"
  BUILD_GEODE_MOD_REF: "dce36b35e1c3d9d8af92347760b12eba3caf4004"
```

The SDK is selected by version (`6.0.0`), which the action maps to the
`v6.0.0` tag; the exact commit is additionally enforced by
`cmake/VerifyDependencies.cmake` at configure time. Bindings are selected by
the full commit SHA, not `main`.

### Early failure and post-build verification

1. `verify-pins` job (runs first, `needs: ['verify-pins']` on the build job):
   - `bash ci/verify-pins.sh`
   - resolves `refs/tags/v6.0.0` (including the peeled `^{}` commit),
     `bindings`/`cli`/`build-geode-mod` `main` heads via `git ls-remote`,
     and compares them to the recorded SHAs.
   - verifies the 35 vendored FFmpeg libraries with
     `sha256sum --check ci/ffmpeg-libs.sha256`.
   - fails the pipeline on any mismatch, so an alpha SDK tag moving or a
     bindings force-push is caught before compiling.
2. Build job: after the action, a `Verify the SDK / bindings actually used`
   step:
   - requires `$GEODE_SDK/.git` and `$GEODE_BINDINGS_REPO_PATH/.git`,
   - re-runs `ci/verify-pins.sh` so the checkouts the action fed to CMake are
     the pinned ones.
3. Configure itself refuses to proceed on a mismatched SDK version/commit or
   missing `bindings/2.2091` (see `cmake/VerifyDependencies.cmake`).

## What "clean checkout" means here

- FFmpeg is vendored at `src/renderer/ffmpeg` and is the default
  `WB_FFMPEG_DIR`; the download path is only a fallback. The URL is
  SHA-256 pinned, so a fallback download cannot silently change.
- Ninja build directories, `CMakeCache.txt`, and generated `.so`/`.geode`
  files are ignored by `.gitignore`; nothing build-generated is committed.
- The only caches CI relies on are the action's own SDK/bindings checkout.
  There is no project-level CMake or CPM cache key that could serve stale
  objects under a new SDK revision; each matrix job configures from scratch.
- Local clean-room verification used fresh `build-android64-clean` /
  `build-android32-clean` directories with `CPM_SOURCE_CACHE` pointing at
  the audited revisions (see `BUILD_AND_TEST_REPORT.md`).

## Known CI limitations

- Windows/macOS/iOS matrix entries were not executed here; only the Android
  entries were reproduced locally (Linux host + NDK cross-compile). The
  workflow configuration is syntactically valid (`python -c yaml.safe_load`)
  and mirrors upstream semantics.
- `ci/verify-pins.sh` needs network access for `ls-remote`; set
  `SKIP_REMOTE=1` for offline runs (local checkout and FFmpeg checks still
  run).
- The post-build step assumes the pinned action exports `GEODE_SDK` and
  `GEODE_BINDINGS_REPO_PATH`, which is how `build-geode-mod` provides its
  checkouts to CMake. If a future action version changes that, the step
  fails loudly rather than silently skipping verification.
