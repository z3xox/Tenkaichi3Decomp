#!/usr/bin/env python3
"""Builds and runs the software-float test of the WebAssembly build (needs emcc and node on PATH).

ops.c goes through the pipeline the game's sources take in the WebAssembly build:
    emcc -E  ->  eeconst.transform  ->  clang IR at -O0 (optnone off)  ->  wasm_sf.py  ->  optimised wasm object
and is linked with driver.c and the routines (softfloat_ps2.c with SF_FAST_CALLS, plat_fastvec.c, vu0_a.c), built as
the port's files are. Also checked: the optimised object of ops.c contains no f32 arithmetic, comparison or
conversion instruction. With --control, ops.c is ALSO built without the lowering (host IEEE arithmetic) to show
that the test notices the difference.
Usage: python port/tests/wasm_sf/run.py [--control]"""
import pathlib, re, subprocess, sys

D = pathlib.Path(__file__).resolve().parent
ROOT = D.parents[2]
sys.path.insert(0, str(ROOT / "port/tools"))
import eeconst, wasm_sf

OUT = ROOT / "port/build/wasm_sf_test"
EMCC = "emcc"
GAME_FLAGS = ["-std=gnu99", "-fno-strict-aliasing", "-ffp-contract=off", "-fno-builtin", "-w"]

def sh(cmd, **kw):
    r = subprocess.run([str(c) for c in cmd], cwd=ROOT, capture_output=True, text=True, **kw)
    if r.returncode:
        print("FAILED:", " ".join(str(c) for c in cmd), "\n", r.stdout[-2000:], r.stderr[-3000:])
        sys.exit(2)
    return r.stdout

def lowered_object(src, o):
    """The WebAssembly build's pipeline for one software-float source."""
    pre = sh([EMCC, "-E"] + GAME_FLAGS + [src])
    i = o.with_suffix(".i")
    i.write_text(eeconst.transform(pre))
    ll, sf = o.with_suffix(".ll"), o.with_suffix(".sf.ll")
    sh([EMCC, "-S", "-emit-llvm", "-O0", "-Xclang", "-disable-O0-optnone"] + GAME_FLAGS + ["-x", "cpp-output", i, "-o", ll])
    text, n, bad = wasm_sf.fix(ll.read_text(encoding="latin-1"))
    if bad:
        print("unlowered float operations:", bad[:5])
        sys.exit(2)
    sf.write_text(text, encoding="latin-1", newline="\n")
    print(f"  {src.name}: {n} float operations lowered")
    sh([EMCC, "-c", "-O2", sf, "-o", o])
    asm = o.with_suffix(".s")
    sh([EMCC, "-S", "-O2", sf, "-o", asm])
    left = re.findall(r"\bf32\.(add|sub|mul|div|eq|ne|lt|le|gt|ge|convert_i32_[su]|trunc_\w*|nearest|sqrt|min|max)\b", asm.read_text())
    print(f"  f32 arithmetic / compare / convert instructions left in the optimised code: {len(left)} {sorted(set(left))}")
    return o, len(left)

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    rows = [l.split() for l in (ROOT / "port/tests/ps2float/pcsx2_v2.7.303.txt").read_text().splitlines() if l.strip()]
    (OUT / "vectors.h").write_text(f"enum {{ N_VECTORS = {len(rows)} }};\nstatic const uint32_t kVectors[{len(rows)}][19] = {{\n" +
                                   ",\n".join("{" + ",".join("0x" + x for x in r) + "}" for r in rows) + "};\n")
    routines = []
    for f, extra in [("port/src/softfloat_ps2.c", ["-DSF_FAST_CALLS", "-Iport/src"]), ("port/src/plat_fastvec.c", ["-Iport/src"]),
                     ("src/port/vu0_a.c", ["-DREF_VU0_EXTERN_ARITH", "-Iport/src"])]:
        o = OUT / (pathlib.Path(f).stem + ".o")
        sh([EMCC, "-c", "-O2", "-Iinclude"] + GAME_FLAGS + extra + [f, "-o", o])
        routines.append(o)
    ops, left = lowered_object(D / "ops.c", OUT / "ops.o")
    js = OUT / "test.js"
    sh([EMCC, "-O2", f"-I{OUT}"] + GAME_FLAGS + [D / "driver.c", ops] + routines + ["-o", js])
    r = subprocess.run(["node", str(js)], capture_output=True, text=True)
    print(r.stdout + r.stderr)
    ok = r.returncode == 0 and left == 0
    if "--control" in sys.argv:
        print("Control: ops.c compiled WITHOUT the lowering (the host's IEEE arithmetic):")
        oc = OUT / "ops_ieee.o"
        sh([EMCC, "-c", "-O2"] + GAME_FLAGS + [D / "ops.c", "-o", oc])
        jc = OUT / "control.js"
        sh([EMCC, "-O2", f"-I{OUT}"] + GAME_FLAGS + [D / "driver.c", oc] + routines + ["-o", jc])
        rc = subprocess.run(["node", str(jc)], capture_output=True, text=True)
        print("\n".join(l for l in rc.stdout.splitlines() if "differ" in l or "RESULT" in l or "FAIL" in l))
        print("control detected (expected FAIL):", rc.returncode != 0)
        ok &= rc.returncode != 0
    print("OVERALL:", "PASS" if ok else "FAIL")
    sys.exit(0 if ok else 1)

main()
