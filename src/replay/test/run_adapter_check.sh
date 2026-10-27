#!/bin/sh
# Builds gdr_wb_adapter.hpp against the REAL old White Bot model (replay/legacy, namespace wbleg) with Geode/cocos2d stubbed.
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"; OUT="$(mktemp -d)"
g++ -std=c++23 -O1 -I"$HERE/replay" -I"$HERE/oracles/stubs" \
    "$HERE/tests/gdr_adapter_check.cpp" "$HERE/replay/gdr.cpp" "$HERE/replay/legacy/legacy_gdr.cpp" -o "$OUT/adapter_check"
"$OUT/adapter_check"
