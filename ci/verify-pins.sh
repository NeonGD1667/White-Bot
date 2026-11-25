#!/usr/bin/env bash
# Verify that every immutable dependency pin still resolves as recorded.
#
# Run from the repository root:  bash ci/verify-pins.sh
# Requires network access for the git ls-remote checks; set SKIP_REMOTE=1 to
# only verify local files.
set -euo pipefail

GEODE_REPO="https://github.com/geode-sdk/geode"
GEODE_TAG="v6.0.0"
GEODE_COMMIT="fcbf84c15a18924df29c01636d46450f935c220c"

BINDINGS_REPO="https://github.com/geode-sdk/bindings"
BINDINGS_COMMIT="c46bcdfc511b992200bb3818f7c1de1f41dc8fde"

CLI_REPO="https://github.com/geode-sdk/cli"
CLI_COMMIT="e1dd5c280c205afee3b34460a1b49068da65088f"

BUILD_ACTION_COMMIT="dce36b35e1c3d9d8af92347760b12eba3caf4004"

FFMPEG_ARCHIVE_SHA256="77593abf2c821bdf7e04d927dd2415345033d1536c2b2a12c7addd176eed19f1"

fail=0
ok()   { printf '  [ok]   %s\n' "$*"; }
bad()  { printf '  [FAIL] %s\n' "$*"; fail=1; }

echo "== Git revision pins =="
if [[ "${SKIP_REMOTE:-0}" != "1" ]]; then
    check_ref() { # repo, ref, expected, label
        local refs
        refs=$(git ls-remote "$1" "$2" 2>/dev/null | awk '{print $1}' | head -n1 || true)
        if [[ "$refs" == "$3" ]]; then ok "$4  $2 -> $3"; else bad "$4  $2 -> ${refs:-<missing>} (expected $3)"; fi
    }
    # Annotated tags: take the peeled commit (line ending in "^{}").
    geode_refs=$(git ls-remote "$GEODE_REPO" "refs/tags/${GEODE_TAG}" "refs/tags/${GEODE_TAG}^{}" 2>/dev/null || true)
    geode_commit=$(printf '%s\n' "$geode_refs" | awk '/\^\{\}$/ {print $1}' | head -n1)
    if [[ -z "$geode_commit" ]]; then
        geode_commit=$(printf '%s\n' "$geode_refs" | awk '{print $1}' | head -n1)
    fi
    if [[ "$geode_commit" == "$GEODE_COMMIT" ]]; then
        ok "geode  ${GEODE_TAG} -> $GEODE_COMMIT"
    else
        bad "geode  ${GEODE_TAG} -> ${geode_commit:-<missing>} (expected $GEODE_COMMIT)"
    fi
    check_ref "$BINDINGS_REPO" "refs/heads/main" "$BINDINGS_COMMIT" "bindings"
    check_ref "$CLI_REPO" "refs/heads/main" "$CLI_COMMIT" "cli"
    check_ref "https://github.com/geode-sdk/build-geode-mod" "refs/heads/main" "$BUILD_ACTION_COMMIT" "build-geode-mod"
else
    echo "  (SKIP_REMOTE=1: remote checks skipped)"
fi

echo "== Local dependency checkouts (if present) =="
if [[ -n "${GEODE_SDK:-}" && -d "${GEODE_SDK}/.git" ]]; then
    head=$(git -C "$GEODE_SDK" rev-parse HEAD)
    [[ "$head" == "$GEODE_COMMIT" ]] && ok "GEODE_SDK -> $head" || bad "GEODE_SDK -> $head (expected $GEODE_COMMIT)"
fi
if [[ -n "${GEODE_BINDINGS_REPO_PATH:-}" && -d "${GEODE_BINDINGS_REPO_PATH}/.git" ]]; then
    head=$(git -C "$GEODE_BINDINGS_REPO_PATH" rev-parse HEAD)
    [[ "$head" == "$BINDINGS_COMMIT" ]] && ok "bindings -> $head" || bad "bindings -> $head (expected $BINDINGS_COMMIT)"
fi

echo "== FFmpeg vendored binaries =="
if command -v sha256sum >/dev/null 2>&1; then
    if sha256sum --check --quiet ci/ffmpeg-libs.sha256; then
        ok "35 vendored FFmpeg libraries match ci/ffmpeg-libs.sha256"
    else
        bad "vendored FFmpeg libraries differ from ci/ffmpeg-libs.sha256"
    fi
else
    bad "sha256sum not available"
fi

echo "== Recorded FFmpeg release archive =="
echo "  url:    https://github.com/NeonGD1667/White-Bot-FFMPEG/releases/download/v1.0.0/white-bot-ffmpeg-8.1.2-r1-gpl.tar.gz"
echo "  sha256: ${FFMPEG_ARCHIVE_SHA256} (enforced by CMake URL_HASH)"

echo
if [[ "$fail" -ne 0 ]]; then
    echo "PIN VERIFICATION FAILED"
    exit 1
fi
echo "All dependency pins verified."
