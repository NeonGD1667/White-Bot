#!/bin/sh
# Usage: run_macro_harness.sh <dir with the NEW macro.hpp/macro.cpp> <NEW replay/ dir> <ORIGINAL White Bot checkout (v3.1.1)>
# Builds the macro harness twice (old v3.1.1 sources vs the rewritten macro.hpp/macro.cpp) and diffs their behaviour.
set -e
NEWM="$1"; NEWR="$2"; OLD="$3"; HERE="$(cd "$(dirname "$0")" && pwd)"; STUBS="$HERE/../../oracles/stubs"; B="$(mktemp -d)"
# ---- NEW tree: real macro.hpp + real replay/ (slc replaced by an empty stub header) + the real GDR-IO block of macro.cpp
mkdir -p "$B/new/replay/slc"; cp "$NEWM/macro.hpp" "$B/new/"; cp -r "$NEWR/gdr.hpp" "$NEWR/gdr.cpp" "$NEWR/gdr_wb_adapter.hpp" "$NEWR/json.hpp" "$NEWR/legacy" "$B/new/replay/"
echo "// stub" > "$B/new/replay/slc/slc.hpp"
awk '/^\/\/ BEGIN GDR-IO/{on=1} on{print} /^\/\/ END GDR-IO/{on=0}' "$NEWM/macro.cpp" > "$B/new/io_block.inc"
# ---- OLD tree: v3.1.1 macro.hpp + src/gdr + the OLD GDR2 reader/writer/functions extracted verbatim from macro.cpp
mkdir -p "$B/old/gdr/slc"; cp "$OLD/src/macro.hpp" "$B/old/"; cp "$OLD/src/gdr/gdr.hpp" "$OLD/src/gdr/gdr.cpp" "$OLD/src/gdr/json.hpp" "$B/old/gdr/"; echo "// stub" > "$B/old/gdr/slc/slc.hpp"
python3 - "$OLD/src/macro.cpp" "$B/old/io_block.inc" <<'PY'
import sys
s=open(sys.argv[1]).read().replace('\r','')
a=s.index("namespace {\n\nclass GDR2Reader"); b=s.index("} // namespace",a)+len("} // namespace")
c=s.index("bool Macro::isGDR2Data("); d=s.index("// ============================================================\n// SLC2")
open(sys.argv[2],'w').write("#include <array>\n#include <bit>\n#include <cstring>\n#include <span>\n"+s[a:b]+"\n"+s[c:d])
PY
INC="-I$HERE -I$STUBS -std=c++23 -O1 -w"
g++ $INC -DOLD_TREE -I"$B/old" -I"$B/old/gdr" "$HERE/macro_driver.cpp" "$B/old/gdr/gdr.cpp" -o "$B/drv_old"
g++ $INC -I"$B/new" -I"$B/new/replay" "$HERE/macro_driver.cpp" "$B/new/replay/gdr.cpp" "$B/new/replay/legacy/legacy_gdr.cpp" -o "$B/drv_new"
echo "$B"
