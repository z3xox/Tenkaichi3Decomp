#!/usr/bin/env python3
"""The WebAssembly build: the game's sources and the port's code compiled with Emscripten.

The game's sources take the native build's route as far as it applies (portsrc.prepare, eeconst.transform), then:
  - ptr32.r2l: call arguments evaluated right to left, as the game's compiler does (clang evaluates left to right;
    the native 64-bit build needs the same pass);
  - no ptr32 pointer narrowing: wasm32 pointers are 4 bytes, as on the PS2;
  - clang -O0 IR -> wasm_sf.py (the PS2's float arithmetic as calls, see there) -> optimised wasm object.
wasm32 already aligns long long / double to 8 bytes (what -malign-double gives the 32-bit x86 build).

Usage (emcc on PATH):
  python port/tools/wasm_build.py one <source.c>...   compile those game sources only (objects in port/build/obj_wasm)
  python port/tools/wasm_build.py game                compile every game source (portsrc.sources())
  python port/tools/wasm_build.py port [file...]      compile the port's code (port/src, gs/, web/, newlib maths)
  python port/tools/wasm_build.py data                the data tables (wasm_data.py) and their disc-run list
  python port/tools/wasm_build.py link                stubs for what nobody defines, wasm_sig.py, link -> port/build/web
"""
import concurrent.futures, os, pathlib, re, subprocess, sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import eeconst, portsrc, wasm_sf, wasm_shaders

ROOT = pathlib.Path(__file__).resolve().parents[2]
# Diagnostic variants (measurements only, never the build that is used): BT3_WASM_VARIANT=<name> builds into
# obj_wasm_<name> / web_<name>; BT3_WASM_HARDFLOAT=1 leaves out the PS2 float lowering (host IEEE floats: the game's
# arithmetic is NOT the PS2's); BT3_WASM_NO_FPCAST=1 links without the pointer-cast emulation; BT3_WASM_WEB=<dir>
# only changes the output folder.
VARIANT = os.environ.get("BT3_WASM_VARIANT", "")
SUFFIX = "_" + VARIANT if VARIANT else ""
HARDFLOAT = os.environ.get("BT3_WASM_HARDFLOAT") == "1"
OBJ = ROOT / ("port/build/obj_wasm" + SUFFIX)
EMCC = "emcc"
# The game's flags of undefined.py without the x86 ones (-m32, -msoft-float, -mno-sse, -malign-double, -fno-pic, ...).
GAME = ["-std=gnu89", "-fno-strict-aliasing", "-ffp-contract=off", "-fcommon", "-w", "-fno-builtin",
        "-Wno-error=return-mismatch", "-Wno-error=incompatible-function-pointer-types", "-Wno-error=int-conversion",
        "-Wno-error=implicit-function-declaration", "-Wno-error=incompatible-pointer-types"]
# Per-file definitions for this target only (each explained where its replacement is defined).
GAME_DEFS = {"src/main.c": ["-DMainMenu_StartBallLoops=gPortOverlayLoadArea"],  # port/src/web/web_plat.c
             "src/sys/heap.c": ["-DD_3BE71C=(*gPortAbs3BE71C)"]}                   # port/src/web/web_plat.c
# The ELF linker's names for the start of the data and the end of the bss (headless.c's memory dump); wasm-ld's.
PORT_DEFS = {"headless.c": ["-D__data_start=__global_base", "-D_end=__data_end"]}
FORCED = ["-include", "port_libm.h", "-Iinclude", "-Iport/include", "-include", "port_compat.h"]
OPT = ["-O2"]

def run(cmd):
    return subprocess.run([str(c) for c in cmd], cwd=ROOT, capture_output=True, text=True, encoding="latin-1")

def compile_soft(src, o, extra=(), r2l=True, flags=GAME, forced=FORCED, ee=True):
    """One software-float source -> wasm object. Returns (ok, message, lowered operation count).
    flags: the compiler flags (GAME for the game's sources); forced: -include / -I options of the preprocessing step
    only (the preprocessed text contains them); ee: the PS2 compiler's float constants (eeconst.transform), which
    undefined.py applies to every software-float file except the vector-library references."""
    extra = list(extra)
    r = run([EMCC, "-E"] + flags + forced + extra + [src])
    if r.returncode:
        return False, "preprocess: " + r.stderr.strip()[-600:], 0
    # Windows: line markers carry paths with single backslashes, which a second parse reads as escape sequences
    text = LINE_MARK.sub(lambda m: m.group(1) + m.group(2).replace("\\", "/") + '"', r.stdout)
    if ee:
        text = eeconst.transform(text)
    i = o.with_suffix(".i")
    if r2l:
        import ptr32
        i.write_text(text, encoding="latin-1", newline="\n")
        try:
            text = ptr32.r2l(i.read_bytes(), ["-x", "cpp-output", "--target=wasm32-unknown-emscripten",
                                               flags[0], "-w", "-Wno-error=return-mismatch",
                                               "-Wno-error=return-type"]).decode("latin-1")
        except RuntimeError as e:
            return False, f"r2l: {e}", 0
    i.write_text(text, encoding="latin-1", newline="\n")
    ll = o.with_suffix(".ll")
    r = run([EMCC, "-S", "-emit-llvm", "-O0", "-Xclang", "-disable-O0-optnone"] + flags + extra +
            ["-x", "cpp-output", i, "-o", ll])
    if r.returncode:
        return False, "compile: " + "\n".join(l for l in r.stderr.splitlines() if "error" in l)[:800], 0
    if HARDFLOAT:  # (diagnosis only: host floats)
        text, n, bad = ll.read_text(encoding="latin-1"), 0, []
    else:
        text, n, bad = wasm_sf.fix(ll.read_text(encoding="latin-1"))
    if bad:
        return False, "float operations without a PS2 routine: " + "; ".join(l for _, l in bad[:3]), n
    # A variable defined without a value in the game's files (`s32 gPortSeSilent;`, -fcommon: a "common" symbol)
    # becomes a weak one: WebAssembly objects have no common symbols (LLVM's wasm tools crash reading one), and a
    # weak definition keeps their meaning (the same name in several files is one variable; a definition with a value
    # elsewhere wins).
    text = COMMON.sub(r"\1weak \2", text)
    sf = o.with_suffix(".sf.ll")
    sf.write_text(text, encoding="latin-1", newline="\n")
    r = run([EMCC, "-c"] + OPT + [sf, "-o", o])
    if r.returncode:
        return False, "codegen: " + r.stderr.strip()[-600:], n
    # the object itself: no f32 arithmetic, comparison or conversion may be left (neg / abs / copysign are sign-bit
    # operations, load / store / select / reinterpret keep the pattern)
    if HARDFLOAT:
        return True, "", n
    r = run(["llvm-objdump", "-d", o])
    left = sorted(set(F32_OP.findall(r.stdout)))
    if r.returncode or left:
        return False, f"f32 instructions in the object: {left or r.stderr[-300:]}", n
    return True, "", n

COMMON = __import__("re").compile(r"^(@[\w.$]+ = )common ((?:hidden |protected )?global )", __import__("re").M)
LINE_MARK = __import__("re").compile(r'^(# \d+ ")([^"\n]*)"', __import__("re").M)
F32_OP = __import__("re").compile(r"\bf32\.(add|sub|mul|div|eq|ne|lt|le|gt|ge|convert_\w+|trunc\w*|demote_f64|min|max|sqrt|nearest|floor|ceil)\b")

def game_object(f):
    f = pathlib.Path(f).resolve()
    o = OBJ / (str(f.relative_to(ROOT / "src")).replace("\\", "/").replace("/", "_")[:-2] + ".o")
    src, done, missing = portsrc.prepare(f)
    ok, msg, n = compile_soft(src, o, [f"-I{f.parent}"] + GAME_DEFS.get(f.relative_to(ROOT).as_posix(), []))
    return f, o if ok else None, msg, n, missing

# ---- the port's own code (the rules of undefined.py, for this target) -------------------------------------------
# Online play (gs/net*.c) is not part of the first browser build: sockets, threads and the matchmaking service do not
# exist there in this form. The Vulkan back end (gs/gs_gpu.c) has no browser back end in SDL3: the GL back end is used.
# The setup window (port/setup) is not needed: the browser build has no installer. The settings window (gs/ui.cpp, Dear
# ImGui) is not built either: the page has its own settings panel, and port/src/web/web_ui.c is the engine's side of
# ui.h for it. With that the browser build has no C++ at all. The crash handlers (plat_crash.c: signals, Windows
# exceptions) are not built either: in the browser the page catches a crash, and web/web_crash.c gives it the context.
WASM_SKIP = {"net.c", "net_match.c", "net_relay.c", "gs_gpu.c", "plat_crash.c"}
HARD = {"plat_libm.c", "plat_fastvec.c"}  # host floats on purpose (undefined.py)
SDL = ["-sUSE_SDL=3"]
# Renderer back ends that exist only in the browser build: in port/src/web/, compiled with the renderer's flags and
# headers (port/src/gs) under the renderer's object names
WEB_RENDERERS = {"gs_wgpu.c"}
# Dawn's webgpu.h for Emscripten (the WebGPU back end, web/gs_wgpu.c): compile and link flag
WEBGPU = ["--use-port=emdawnwebgpu"]
PC = ["-std=gnu99", "-fno-strict-aliasing", "-ffp-contract=off", "-fno-builtin", "-w", "-Iinclude", "-Iport/include", "-Iport/src"]
SOFT_FORCED = ["-DSF_FAST_CALLS", "-include", "math.h", "-include", "stdlib.h", "-include", "port_libm.h"]
GEN = ROOT / "port/build/gen_wasm"

def shaders_header():
    """port/build/gen_wasm/shaders.h: the GLSL sources for gs_gl.c, as undefined.py embeds them (NUL-terminated).
    No SPIR-V: the Vulkan back end is not built for the browser."""
    GEN.mkdir(parents=True, exist_ok=True)
    arrays = []
    # LF line endings, as the repository stores them: a checkout with core.autocrlf has CRLF, and the embedded bytes
    # (so bt3.wasm) would then depend on the checkout's settings
    texts = [(src.name, src.read_bytes().replace(b"\r\n", b"\n"))
             for src in sorted((ROOT / "port/src/gs/shaders").glob("*")) if src.suffix in (".vert", ".frag")]
    texts += [(name, wasm_shaders.variant_glsl(name).encode()) for name in sorted(wasm_shaders.VARIANTS)]  # gsnz.frag
    for file, text in texts:
        stem, stage = file.rsplit(".", 1)
        name = "k" + stem.capitalize() + stage.capitalize() + "Glsl"
        arrays.append(f"static const unsigned char {name}[] = {{" + ",".join(str(b) for b in text + b"\0") + "};\n")
    (GEN / "shaders.h").write_text("/* generated from port/src/gs/shaders by port/tools/wasm_build.py */\n" + "".join(arrays))

# The names port_jobs gives the port's objects start with these (the game's own objects are named after their folders
# under src/: battle_, menu_, sys_, main). "cxx" is no longer made (the C++ settings window is not built for the
# browser): it stays so that such an object left from an older build is removed before the link.
PORT_OBJ_PREFIXES = {"nl", "pc", "gs", "web", "cxx"}

def port_jobs():
    """(kind, source, object) for every file of the port's side."""
    jobs = []
    for f in sorted((ROOT / "port/third_party/newlib_libm").glob("*.c")):
        jobs.append(("libm", f, OBJ / ("nl_" + f.stem + ".o")))
    for f in [ROOT / "src/port/vu0_a.c", ROOT / "src/port/vu0_b.c"] + sorted((ROOT / "port/src").glob("*.c")):
        if f.name in WASM_SKIP:
            continue
        kind = "vu0ref" if f.parent.name == "port" and f.parent.parent.name == "src" else "hard" if f.name in HARD else "soft"
        jobs.append((kind, f, OBJ / ("pc_" + f.stem + ".o")))
    for f in sorted((ROOT / "port/src/gs").glob("*.c")):
        if f.name not in WASM_SKIP:
            jobs.append(("gs", f, OBJ / ("gs_" + f.stem + ".o")))
    for f in sorted((ROOT / "port/src/web").glob("*.c")):  # the browser build's own files
        if f.name in WEB_RENDERERS:  # a renderer back end: compiled as the renderer's files are
            jobs.append(("gs", f, OBJ / ("gs_" + f.stem + ".o")))
        else:
            jobs.append(("web", f, OBJ / ("web_" + f.stem + ".o")))
    return jobs

def port_object(job):
    kind, f, o = job
    rel = f.relative_to(ROOT)
    if kind == "libm":  # the PS2's maths library with the game's float model and constants
        ok, msg, n = compile_soft(f, o, [f"-I{f.parent}"], r2l=False,
                                  flags=["-std=gnu89", "-fno-strict-aliasing", "-ffp-contract=off", "-fno-builtin", "-w"],
                                  forced=["-Iport/include", "-include", "newlib_shim.h"])
    elif kind == "vu0ref":  # the vector library's C references, under the game's names; written for a host compiler
        ok, msg, n = compile_soft(f, o, r2l=False, flags=PC, forced=SOFT_FORCED +
                                  ["-include", "vu0_names.h", "-DREF_VU0_EXTERN_ARITH"], ee=False)
    elif kind == "soft":  # the port's software-float files: PS2 constants, PS2 arithmetic
        ok, msg, n = compile_soft(f, o, PORT_DEFS.get(f.name, []), r2l=False, flags=PC, forced=SOFT_FORCED)
    else:
        n = 0
        if kind == "hard":
            cmd = [EMCC, "-c"] + OPT + PC
        elif kind == "web":
            cmd = [EMCC, "-c"] + OPT + SDL + PC
        else:  # gs
            cmd = [EMCC, "-c"] + OPT + SDL + WEBGPU + ["-std=gnu99", "-fno-strict-aliasing", "-Wall", "-Wno-unused",
                                             "-Wno-misleading-indentation", f"-I{GEN}", f"-I{ROOT / 'port/src/gs'}"]
        # through LLVM IR like every other file, so that wasm_sig.py can repair calls in it (see there)
        ll = o.with_suffix(".ll")
        r = run([c for c in cmd if c != "-c"] + ["-S", "-emit-llvm", f, "-o", ll])
        if r.returncode == 0:
            r = run([cmd[0], "-c"] + OPT + [ll, "-o", o])
        ok = r.returncode == 0
        msg = "\n".join(l for l in r.stderr.splitlines() if "error" in l)[:1500] if not ok else ""
    return rel, kind, o if ok else None, msg, n

# ---- data tables and link -----------------------------------------------------------------------------------------
DATA_OBJ = ROOT / ("port/build/obj_wasm_data" + SUFFIX)
GEN_OBJ = ROOT / ("port/build/obj_wasm_gen" + SUFFIX)
WEB = ROOT / "port/build" / os.environ.get("BT3_WASM_WEB", "web" + SUFFIX)
# Memory: see BROWSER_PORT_STATUS.md (layout B). The PS2 regions lie below GLOBAL_BASE at their own addresses; memory
# ends at the scratchpad's address (0x70000000), so an access there traps.
LINK = ["-sGLOBAL_BASE=" + str(0x12010000), "-sINITIAL_MEMORY=" + str(0x18000000), "-sALLOW_MEMORY_GROWTH=1",
        "-sMAXIMUM_MEMORY=" + str(0x70000000), "-sSTACK_SIZE=16MB", "-sABORTING_MALLOC=0",
        "-sUSE_SDL=3", "-sMIN_WEBGL_VERSION=2", "-sMAX_WEBGL_VERSION=2", "--use-port=emdawnwebgpu",
        "-sJSPI=1",  # the game's own loop waits for the vertical blank and for files: those waits suspend (plat_stub.c)
        # calls through function pointers of another type than the function (the game's callbacks, e.g.
        # McFlow_CallDone calls every "done" callback as void(s32)): as on the PS2, missing arguments are 0, extra ones
        # ignored, a result that is not there reads 0. Without it such a call traps (call_indirect checks the type).
        "-sEMULATE_FUNCTION_POINTER_CASTS=1",
        "-sMODULARIZE=1", "-sEXPORT_NAME=createBt3", "-sENVIRONMENT=web",
        "-sFORCE_FILESYSTEM=1", "-sEXPORTED_RUNTIME_METHODS=FS,ENV,HEAPU8,UTF8ToString,callMain,ccall",
        "-sEXPORTED_FUNCTIONS=_main,_Port_CrashContext,_malloc,_free", "-sINVOKE_RUN=0", "-sEXIT_RUNTIME=0",
        "-Wl,--wrap=Progress_Main", "-Wl,--wrap=fopen",  # fopen: the data folder over HTTP (port/src/web/web_fs.c)
        "-Wl,--wrap=Port_PadRead",  # what the game receives from the pads, for the page (port/src/web/web_ui.c)
        # content added from outside the disc: said when it cannot be found here, and why
        "-Wl,--wrap=PortStages_Init", "-Wl,--wrap=PortSongs_Init", "-Wl,--wrap=TexPack_Init",  # (web/web_extras.c)
        # the data folder looked through over HTTP (web/web_fs.c): the folder lookups of stages, songs, texture packs
        "-Wl,--wrap=stat", "-Wl,--wrap=opendir", "-Wl,--wrap=readdir", "-Wl,--wrap=closedir",
        # the check of the game data (plat_verify.c) cannot run here: not started, and said (web/web_verify.c)
        "-Wl,--wrap=Port_VerifyStart", "-Wl,--wrap=Port_DataSummary",
        # the memory card's close / write / sync (plat_mc.c): when a save is whole, for the page to keep it (web_ui.c)
        "-Wl,--wrap=func_002A1FB8", "-Wl,--wrap=func_002A2320", "-Wl,--wrap=func_002A2498",
        "-Wl,--error-limit=0", "--profiling-funcs", "-sASSERTIONS=1"]
if os.environ.get("BT3_WASM_NO_FPCAST") == "1":  # (diagnosis only)
    LINK.remove("-sEMULATE_FUNCTION_POINTER_CASTS=1")

def data():
    """The data tables (wasm_data.py) assembled, with the compiled-in list of their disc runs."""
    r = subprocess.run([sys.executable, str(ROOT / "port/tools/wasm_data.py"), str(OBJ)], cwd=ROOT, capture_output=True, text=True)
    print((r.stdout + r.stderr).strip())
    if r.returncode:
        sys.exit(1)
    DATA_OBJ.mkdir(parents=True, exist_ok=True)
    for old in DATA_OBJ.glob("*.o"):
        old.unlink()
    srcs = sorted((GEN / "data").glob("*.s")) + [GEN / "data_list.c"]
    def one(s):
        return s, run([EMCC, "-c"] + OPT + [s, "-o", DATA_OBJ / (s.stem + ".o")])
    with concurrent.futures.ThreadPoolExecutor(16) as ex:
        bad = [(s, r.stderr) for s, r in ex.map(one, srcs) if r.returncode]
    for s, e in bad:
        print("FAILED", s.name, e.strip()[:300])
    print(f"data: {len(srcs) - len(bad)} of {len(srcs)} assembled")
    if bad:
        sys.exit(1)

def link():
    """Links port/build/web/bt3.js + bt3.wasm. Symbols nobody defines get a stub that says so and aborts (as link.py
    does natively: running the program shows what it still needs); calls of mismatched types are repaired first."""
    WEB.mkdir(parents=True, exist_ok=True)
    GEN_OBJ.mkdir(parents=True, exist_ok=True)
    for old in GEN_OBJ.glob("*"):
        old.unlink()
    objs = lambda: [str(o) for d in (OBJ, DATA_OBJ, GEN_OBJ) for o in sorted(d.glob("*.o"))]
    rsp = ROOT / "port/build/wasm_link.rsp"
    def em(extra):
        rsp.write_text("\n".join('"' + a.replace("\\", "/") + '"' for a in objs() + LINK + extra))
        return run(["em++", "@" + str(rsp), "-o", WEB / "bt3.js"])
    need = set()
    def write_stubs():
        lines = ["/* Generated by port/tools/wasm_build.py: one stub per symbol the browser build does not provide.",
                 "   A stub says which one was reached and aborts (link.py does the same for the native builds). */",
                 "#include <stdio.h>", "#include <stdlib.h>",
                 'static void missing(const char *n) { fprintf(stderr, "bt3: not implemented yet: %s\\n", n); abort(); }']
        lines += [f'void {s}(void) {{ missing("{s}"); }}' for s in sorted(need)]
        (GEN / "stubs.c").write_text("\n".join(lines) + "\n", newline="\n")
        (ROOT / "port/build/wasm_stubs.txt").write_text("\n".join(sorted(need)) + "\n", newline="\n")
        r2 = run([EMCC, "-c", "-O2", GEN / "stubs.c", "-o", GEN_OBJ / "stubs.o"])
        if r2.returncode:
            print(r2.stderr[-1500:])
            sys.exit(1)
    # wasm-ld reports an undefined symbol only where it is referenced from live code, and what is live changes as
    # stubs are added and calls repaired: a symbol has been seen to appear only in the last link. So the whole
    # sequence (probe, stubs, call repair, link) is repeated until the link names nothing new.
    for attempt in range(6):
        r = em([])
        more = set(re.findall(r"undefined symbol: (\w+)", r.stderr)) - need
        if more:
            need |= more
            write_stubs()
            continue
        s = run([sys.executable, str(ROOT / "port/tools/wasm_sig.py"), str(OBJ), str(DATA_OBJ), str(GEN_OBJ)])
        print(s.stdout.strip())
        if s.returncode:
            sys.exit(1)
        r = em([])
        more = set(re.findall(r"undefined symbol: (\w+)", r.stderr)) - need
        if not more:
            break
        need |= more
        write_stubs()
    for f in ("index.html", "bt3_data.js", "settings.js", "dashboard.js", "controllers.js", "toasts.js", "saves.js", "touch.js", "gamepad.svg"):  # the page (port/web)
        (WEB / f).write_bytes((ROOT / "port/web" / f).read_bytes())
    mism = re.findall(r"signature mismatch: (\S+)", r.stderr)
    errs = [l for l in r.stderr.splitlines() if "error" in l]
    print(f"{len(need)} stubs (port/build/wasm_stubs.txt); {len(mism)} signature mismatches left; link "
          f"{'OK -> ' + str((WEB / 'bt3.js').relative_to(ROOT)) if r.returncode == 0 else 'FAILED'}")
    for l in (errs + [f"signature mismatch: {m}" for m in mism])[:30]:
        print("  " + l[-200:])
    sys.exit(0 if r.returncode == 0 and not mism else 1)

def main():
    OBJ.mkdir(parents=True, exist_ok=True)
    if sys.argv[1:2] == ["data"]:
        data()
        return
    if sys.argv[1:2] == ["link"]:
        link()
        return
    if sys.argv[1:2] == ["port"]:
        shaders_header()
        r = run([sys.executable, ROOT / "port/tools/wasm_shaders.py"])  # WGSL for the WebGPU back end
        print(r.stdout.strip())
        if r.returncode:
            print(r.stderr.strip())
            sys.exit(1)
        # The link takes every object in OBJ: a port object no source makes any more (a file left out of the browser
        # build, such as the C++ settings window) is removed, or it would still be linked.
        made = {j[2].name for j in port_jobs()}
        for old in OBJ.glob("*.o"):
            if old.name.split("_")[0] in PORT_OBJ_PREFIXES and old.name not in made:
                print(f"port: removed {old.name} (no longer built)")
                old.unlink()
        jobs = [j for j in port_jobs() if len(sys.argv) == 2 or j[1].name in sys.argv[2:]]
        with concurrent.futures.ProcessPoolExecutor(min(16, os.cpu_count() or 4)) as ex:
            res = list(ex.map(port_object, jobs))
        bad = [r for r in res if r[2] is None]
        for rel, kind, o, msg, n in res:
            if o is None or "-v" in sys.argv:
                print(f"{'ok    ' if o else 'FAILED'} {rel} [{kind}]" + (f"\n       {msg}".replace("\n", "\n       ") if msg else ""))
        print(f"port: {len(res) - len(bad)} of {len(res)} compiled, {sum(r[4] for r in res)} float operations lowered; "
              f"not built for the browser: {', '.join(sorted(WASM_SKIP))}")
        sys.exit(1 if bad else 0)
    if (len(sys.argv) > 2 and sys.argv[1] == "one") or sys.argv[1:] == ["game"]:
        # (portsrc.REPLACED holds "/" paths and relative_to() gives "\" ones on Windows: filtered here as well)
        fs = sys.argv[2:] if sys.argv[1] == "one" else \
            [f for f in portsrc.sources() if f.relative_to(ROOT).as_posix() not in portsrc.REPLACED]
        with concurrent.futures.ProcessPoolExecutor(min(16, os.cpu_count() or 4)) as ex:
            res = list(ex.map(game_object, fs))
        bad = 0
        for f, o, msg, n, missing in res:
            rel = f.relative_to(ROOT)
            print(f"{'ok    ' if o else 'FAILED'} {rel}  ({n} float operations lowered){'  no C for: ' + ' '.join(missing) if missing else ''}")
            if not o:
                bad += 1
                print("       " + msg.replace("\n", "\n       "))
        print(f"{len(res) - bad} of {len(res)} compiled, {sum(r[3] for r in res)} float operations lowered")
        sys.exit(1 if bad else 0)
    print(__doc__)
    sys.exit(2)

if __name__ == "__main__":
    main()
