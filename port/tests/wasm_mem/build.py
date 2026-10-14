#!/usr/bin/env python3
"""Builds the memory-model test (memtest.c) for both layouts into port/build/web/memtest/ and runs it under node.
Then serve port/build/web with port/web/devserver.py and open /memtest/memtest.html?layout=A (or B)&tag=<browser>
in each browser; results land in port/build/web/__results/.
The verdict (and the exit status) is that of layout B, the one the browser build uses (port/tools/wasm_build.py, LINK).
Layout A was rejected (BROWSER_PORT_STATUS.md, memory model); it still runs, for comparison only.
Usage: python port/tests/wasm_mem/build.py"""
import pathlib, shutil, subprocess, sys

D = pathlib.Path(__file__).resolve().parent
OUT = D.parents[2] / "port/build/web/memtest"
COMMON = ["-O2", "-sMODULARIZE=1", "-sALLOW_MEMORY_GROWTH=1", "-sABORTING_MALLOC=0",
          "-sSTACK_SIZE=16MB", "-sEXPORTED_FUNCTIONS=_run_tests,_touch,_grow_test", "-sENVIRONMENT=web,node", "-sEXPORTED_RUNTIME_METHODS=HEAPU8"]
LAYOUTS = {"A": ["-DLAYOUT_A", "-sGLOBAL_BASE=" + str(0x70010000), "-sINITIAL_MEMORY=" + str(0x72000000), "-sMAXIMUM_MEMORY=2GB"],
           # B: memory ends at the scratchpad address, so a stray access to it traps
           "B": ["-sGLOBAL_BASE=" + str(0x12010000), "-sINITIAL_MEMORY=" + str(0x14000000),
                 "-sMAXIMUM_MEMORY=" + str(0x70000000)]}
USED = "B"

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    shutil.copy(D / "memtest.html", OUT / "memtest.html")
    results = {}
    for name, flags in LAYOUTS.items():
        js = OUT / f"memtest_{name}.js"
        r = subprocess.run(["emcc", str(D / "memtest.c"), "-o", str(js), f"-sEXPORT_NAME=createMemtest_{name}"] + COMMON + flags,
                           capture_output=True, text=True)
        if r.returncode:
            print(r.stderr)
            sys.exit(2)
        # node: the same sequence as the page, without the browser
        script = (f"require('{js.as_posix()}')({{print: console.log}}).then(m => {{ const f = m._run_tests();"
                  f"let s; try {{ m._touch(0x70000000); s = 'readable'; }} catch (e) {{ s = 'trap: ' + e.message; }}"
                  f"console.log('  scratchpad before growth: ' + s); const mb = m._grow_test(32);"
                  f"let s2; try {{ m._touch(0x70000000); s2 = 'readable'; }} catch (e) {{ s2 = 'trap: ' + e.message; }}"
                  f"console.log('  grow: ' + mb + ' MB; scratchpad after growth: ' + s2); process.exit(f || mb < 1000 ? 1 : 0); }})")
        r = subprocess.run(["node", "-e", script], capture_output=True, text=True)
        role = "the layout in use" if name == USED else "rejected layout, for comparison only"
        print(f"--- node, layout {name} ({role}) ---\n{r.stdout}{r.stderr}")
        results[name] = r.returncode == 0
    for name, passed in results.items():
        if name != USED:
            print(f"node, layout {name} (not used): {'passes' if passed else 'fails'}")
    print(f"node, layout {USED}:", "PASS" if results[USED] else "FAIL")
    sys.exit(0 if results[USED] else 1)

main()
