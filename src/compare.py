#!/usr/bin/env python3
"""Behaviour comparison OLD (v3.1.1) vs NEW (rewritten macro.hpp/macro.cpp). Usage: compare.py <build dir printed by run_macro_harness.sh>"""
import subprocess, sys, os, itertools
B = sys.argv[1]; OLD = f"{B}/drv_old"; NEW = f"{B}/drv_new"; T = f"{B}/t"; os.makedirs(T, exist_ok=True)
def run(*a, seed=None):
    env = dict(os.environ); 
    if seed is not None: env["HARNESS_SEED"] = str(seed)
    r = subprocess.run(a, capture_output=True, text=True, env=env); return r.returncode, r.stdout
def first_diff(a, b):
    for i, (x, y) in enumerate(itertools.zip_longest(a.splitlines(), b.splitlines())):
        if x != y: return f"line {i+1}: new={x!r} old={y!r}"
    return "identical"
fails = 0; checks = 0
def check(ok, msg):
    global fails, checks; checks += 1
    if not ok: fails += 1; print("  FAIL", msg)
names = {0: "White Bot v3.1.0-Neon", 1: "Dll Bot v2.3.5", 2: "Dll Bot v3.0.1", 3: "Other 7"}
for kind in ("mp", "json"):
    for v in range(4):
        for plat in (0, 1):
            tag = f"GDR1/{kind} {names[v]} plat={plat}"
            fo, fn = f"{T}/o.bin", f"{T}/n.bin"
            run(OLD, "gen", fo, str(v), str(plat), kind); run(NEW, "gen", fn, str(v), str(plat), kind)
            bo, bn = open(fo, "rb").read(), open(fn, "rb").read()
            if v == 1: pass   # Dll Bot < 2.3.6: new writer undoes the +1 import offset (checked below as "no drift")
            elif plat == 0: check(bo == bn, f"{tag}: bytes written by new exportData differ from old ({len(bn)} vs {len(bo)})")
            else:          check(len(bn) > len(bo), f"{tag}: expected only the additive platformer key to differ")
            # old-written file read by both
            import re
            seednorm = lambda s: re.sub(r"\|(\d+)\|2\|1\|", "|S|2|1|", s)   # replay-level seed: old GDR1 reader left it 0
            _, do = run(OLD, "dump", fo); _, dn = run(NEW, "dump", fo); do, dn = seednorm(do), seednorm(dn)
            check(do == dn, f"{tag}: reading OLD-written file: {first_diff(dn, do)}")
            # new-written file read by both (old reader must accept it)
            _, do2 = run(OLD, "dump", fn); _, dn2 = run(NEW, "dump", fn); do2, dn2 = seednorm(do2), seednorm(dn2)
            norm = lambda s: s.replace("plat=1", "plat=0")      # old model has no platformer field in GDR1
            check(norm(do2) == norm(dn2), f"{tag}: reading NEW-written file: {first_diff(dn2, do2)}")
            # import->export
            fo0 = f"{T}/o0.bin"; run(OLD, "gen", fo0, str(v), str(plat), kind, seed=0)     # seed 0: byte-for-byte comparison
            ro, rn = f"{T}/ro.bin", f"{T}/rn.bin"; run(OLD, "rt", fo0, ro, kind); run(NEW, "rt", fo0, rn, kind)
            drift = (v == 1)   # old exporter adds +1 on every re-save of an old Dll Bot macro (documented bug, fixed)
            if plat == 0 and not drift: check(open(ro, "rb").read() == open(rn, "rb").read(), f"{tag}: import+export bytes differ")
            if drift:
                _, a = run(NEW, "dump", rn); _, b = run(NEW, "dump", fo0)
                check(a == b, f"{tag}: new re-save drifted: {first_diff(a, b)}")
# GDR2
for v in range(4):
    for plat in (0, 1):
        tag = f"GDR2 {names[v]} plat={plat}"
        fo, fn = f"{T}/o.g2", f"{T}/n.g2"
        run(OLD, "gen", fo, str(v), str(plat), "gdr2"); run(NEW, "gen", fn, str(v), str(plat), "gdr2")
        rc1, d_new_reads_old = run(NEW, "dump", fo); rc2, d_old_reads_new = run(OLD, "dump", fn); rc3, d_old_reads_old = run(OLD, "dump", fo)
        check(rc1 == 0 and rc2 == 0, f"{tag}: cross read failed (new<-old rc={rc1}, old<-new rc={rc2})")
        # inputs and metadata both readers extract from the OLD file must agree, modulo the old reader's lack of sorting
        sel = lambda s: sorted(l for l in s.splitlines() if l.startswith("in|"))
        check(sel(d_new_reads_old) == sel(d_old_reads_old), f"{tag}: new reader inputs differ from old reader on old file")
        check(sel(d_old_reads_new) == sel(run(NEW, "dump", fn)[1]), f"{tag}: old reader inputs differ from new reader on new file")
        _, dn = run(NEW, "dump", fn)
        wantFixes = 0 if v == 3 else 90     # bot "Other" may not use the WBX1 block -> frame fixes dropped (and logged)
        check(sum(1 for l in dn.splitlines() if l.startswith("fix|")) == wantFixes, f"{tag}: frame fixes in GDR2 (new): expected {wantFixes}")
        check("fix|" not in d_old_reads_old, f"{tag}: sanity: old GDR2 has no frame fixes")
print(f"{checks} comparisons, {fails} failure(s)"); sys.exit(1 if fails else 0)
