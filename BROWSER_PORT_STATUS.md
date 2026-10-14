# Browser port: status

Working log for the WebAssembly / browser build of Tenkaichi3Decomp. "Verified" means checked by running or by
reading the source in this checkout; everything else is marked as a plan or an assumption.

## 0. Repository (verified 2026-10-11)

- `https://github.com/HeckinChloe/tenkaichi3-decomp` **does not exist** (`git ls-remote`: "Repository not found";
  GitHub API: user `HeckinChloe` not found).
- The project matching the description (`port/src/gs/`, `docs/port/README.md`, `port/tools/`, Vulkan renderer,
  Dear ImGui) is **`https://github.com/z3xox/Tenkaichi3Decomp`** (MIT for the port's code, default branch `main`,
  last push 2026-10-11). It is a PC port built on the matching decompilation `z3xox/BT3-Decompiled`.
  Unrelated sibling: `z3xox/BT3-Recomp` (static recompilation, GPL-3.0); not used.
- Checked out shallow (`--depth 1`) at the head of `main` on 2026-10-11.

## 1. Audit (verified against the source)

### Build system
- No CMake/Make. Python drivers: `port/tools/undefined.py` (compile everything) then `port/tools/link.py` (link,
  generate stubs, write `.dat`). Variants in `port/tools/toolchain.py` (`BT3_CC=gcc|clang|clang64|win64`).
- Game code (`src/`, 273 C files incl. the menu overlay) is preprocessed, rewritten by `eeconst.py` (PS2 compiler's
  truncated float constants), then compiled. Native 64-bit builds also go through `ptr32.py` (4-byte pointers via
  `__ptr32`) and `irfix.py` (LLVM IR repairs) and `llc`.
- The game's data tables are not in the repo: `port/data/*.s` hold their *shape* (labels, `.space`, `.zero`,
  `.long sym`, 72 files; only those 5 directives + `.globl/.section/.p2align`). Values are copied in at start from
  the user's disc files (`Port_LoadGameData`, `<program>.dat` made by `make_dat.py`).

### Float model (the main compile-time issue for wasm)
- All game code is built `-msoft-float`: every single-precision op becomes a call (`__addsf3`, `__mulsf3`,
  `__divsf3`, `__ltsf2`, `__fixsfsi`, `__floatsisf`, ...) implemented in `port/src/softfloat_ps2.c` with PCSX2's
  PS2 model (truncation, no NaN/Inf/denormals). Routines take/return `uint32_t` bit patterns.
- Doubles use host IEEE arithmetic even natively (`port/src/plat_libm.c`).
- **clang's wasm32 target has no soft-float mode.** Plan: emit LLVM IR, rewrite every `float` `fadd/fsub/fmul/fdiv`,
  `fcmp`, `fptosi/fptoui/sitofp/uitofp` into `bitcast -> call @__xxsf3(i32, i32) -> bitcast` (bit-exact: wasm's
  reinterpret ops preserve every pattern), leave `double` ops native, then optimise and compile the IR. This is the
  same lowering the backend does natively, done before the optimiser instead of after (so no IEEE constant folding
  of float ops can sneak in).

### Memory model
- The game stores pointers in 32-bit fields: **wasm32 is a natural fit** (no `ptr32.py` needed). wasm32 also aligns
  `long long`/`double` to 8, which is what `-malign-double` provides on i386 (`check_layout.py` exists to verify).
- Fixed addresses used (`port/src/plat_mem.c`): game heap `0x003BE000..0x02000000` (one bump allocator,
  `Port_Malloc`), hardware registers `0x10000000` (64 KB) and `0x12000000` (8 KB), scratchpad `0x70000000` (16 KB).
  Plan: place the wasm program's own data above all of these (`-sGLOBAL_BASE`), so the PS2 regions are plain linear
  memory at their real addresses. Cost: the wasm memory must be ~1.8 GB+ (pages are committed lazily by browsers).
- `Port_LowAlloc` exists for 64-bit only; on wasm32 plain `malloc` qualifies (all addresses < 4 GB).

### Main loop and threads
- The game's own `main` runs the whole program; it blocks once per vertical blank in `vblank_wait()`
  (`port/src/plat_stub.c`, `nanosleep`). Plan: on wasm that wait yields to the browser (`emscripten_sleep` under
  JSPI, or Asyncify), so the original loop is kept unchanged.
- pthreads: optional render thread (`BT3_GS_THREAD`, off by default), data verification thread (`plat_verify.c`),
  netplay threads, `pthread_once`/mutex in settings and input. Plan: single-threaded wasm build; the verification
  thread and netplay are compiled out; `pthread_once`/mutexes work in Emscripten without threads.
- 64-bit stack switch (`__wrap_main`) is x86-64/Windows only; not needed on wasm32.

### Renderer
- Shared recorder `port/src/gs/gs_draw.c` (GS emulation front end, VU1 programs as shaders/interpreter, texture
  cache) + two back ends: Vulkan via **SDL3 GPU** (`gs_gpu.c`, SPIR-V) and **OpenGL 3.3 core** (`gs_gl.c`, GLSL
  translated at run time from the same Vulkan GLSL sources). 9 shaders in `port/src/gs/shaders/`.
- GL entry points used (complete list from `gs_gl.c`): all exist in WebGL2/GLES 3.0 except `glClipControl`
  (optional, has a fallback; WebGL `EXT_clip_control`), `GL_DEPTH_CLAMP` (WebGL `EXT_depth_clamp`), indexed
  `glColorMaski/glEnablei/glDisablei` (WebGL `OES_draw_buffers_indexed`), S3TC textures (texture packs only).
- Settings UI: Dear ImGui (vendored) with SDL GPU / OpenGL3 backends.

### Files, audio, input
- All game data reads go through two functions, `Port_FileSize` / `Port_FileReadAt` (`plat_fcache.c`): one choke
  point for browser-side loading. Data root `gamedata/` (~4 GB unpacked, 68,611 files).
- Audio: SDL3 audio streams (ADX music/voices `snd_adx.c`, sound effects sampler `snd_se.c`).
- Input: SDL3 keyboard and gamepads (`gs_input.c`). Saves: `saves/card1/` folder (`plat_mc.c`). Settings:
  `bt3_settings.txt` (`plat_settings.c`). Movies: own MPEG-2 intra decoder (`plat_mpeg2.c`, no ffmpeg).

### Known limitations of the project itself (from docs/port/README.md, not re-tested here)
- `ObjSeam_TransformVtx` was the last game function without C; a later note says it was written (2026-10-06).
- The two-layer character draw order, render thread race, OpenGL is ~14% slower than Vulkan (driver call count).
- Online play exists (out of scope for the browser build).

## 2. Plan and decisions

1. **Renderer: WebGL2 first, through the existing GL 3.3 back end** (smallest real path: the same recorder, the
   same shaders after a GLSL ES translation). **WebGPU second**, as a third back end of `gs_draw.c` next to Vulkan
   and GL (the recorder already abstracts the back end), with the 9 shaders ported to WGSL. Rust/wgpu is not
   planned: the renderer is C, the back-end interface is a C function table, and a C WebGPU back end (Emscripten's
   `webgpu.h`) needs no FFI layer.
2. Toolchain: Emscripten (installed: 6.0.12, clang 24), a new variant `BT3_CC=wasm` in `toolchain.py`; the native
   variants untouched.
3. Data: never bundled into public artifacts. Private build serves the user's own unpacked `gamedata/` from a local
   server; the browser fetches files on demand (`Port_FileReadAt` -> fetch with Range) and caches them.

## 3. Environment (verified 2026-10-11)

| | |
|---|---|
| Machine | Windows 11 Pro 10.0.26300, AMD Ryzen 7 7700 (8 cores), 31 GB RAM, NVIDIA RTX 5070 Ti |
| Layout | `C:\project\Tenkaichi3Web\` = `Tenkaichi3Decomp\` (git, branch `browser-port`), `tools\emsdk\`, `tools\venv\`, `env.sh` |
| Emscripten | 6.0.12 (clang 24.0.0git, `llvm-objdump`, `llvm-nm`, `wasm-ld`), node 24.19.0 (bundled) |
| Python | 3.12.10, venv with `libclang` (pip; loads from the short path) |
| Git | 2.45.2.windows.1 (`core.autocrlf` is on in this user's config) |
| Browsers | Chrome 154.0.8037.98, Edge 155.0.4283.45, the app's built-in Chromium 152; no Firefox installed |

The long-path problem (git `'$GIT_DIR' too big`, `libclang.dll` WinError 206) came from the session folder's path;
it does not occur under `C:\project\Tenkaichi3Web`. emsdk was moved there (not reinstalled) and works.

```
source /c/project/Tenkaichi3Web/env.sh             # Git Bash: emcc, node, venv python on PATH
python port/tests/wasm_sf/run.py --control          # software-float lowering test (+ IEEE control)
python port/tools/wasm_build.py game                # all game sources -> port/build/obj_wasm
python port/tests/wasm_mem/build.py                 # memory test under node; then in browsers:
python port/web/devserver.py --port 8080            # serves port/build/web on 127.0.0.1 only
#   http://127.0.0.1:8080/memtest/memtest.html?layout=B&tag=<name>  -> port/build/web/__results/
```

## 4. Verified results

### Software float (Priority 2): PASS
- `port/tools/wasm_sf.py` lowers -O0 IR before optimisation: `fadd/fsub/fmul/fdiv` -> `__addsf3/...`; `fcmp` (all
  16 predicates) -> `__ltsf2` + `icmp` (`ord` true, `uno` false: the PS2 has no unordered values, as the native
  routines say); `fptosi/fptoui` (<= 32 bits) -> `__fixsfsi/__fixunssfsi`; `sitofp/uitofp` (<= 32 bits) ->
  `__floatsisf/__floatunsisf`. Kept native, as in the native build: double arithmetic, float <-> double, `fneg`,
  `fabs`, `copysign` (sign-bit operations). Anything else on floats (frem, 64-bit conversions, float vectors, other
  float intrinsics), and any float instruction still present after the rewrite, stops the build.
- `port/tests/wasm_sf/run.py --control`: plain C operators through the lowering vs the routines called directly.
  Inputs: 43 special patterns (zeros, denormals, max, Inf/NaN patterns, 2^31 limits, ...) in every pair, 1,000,000
  random pairs, 2,000,000 single values for conversions, constant operands. **0 differences** in every operation
  (add, sub, mul, div, madd, neg, fabs, 14 comparison forms, select, ftoi/ftou/ftos16/ftou8, itof/utof/s16tof/u8tof,
  bit-pattern pass-through incl. a signalling NaN pattern, constants). Against PCSX2's own results
  (`port/tests/ps2float/pcsx2_v2.7.303.txt`, 1500 cases x 7 FPU columns): **0 of 10,500 differ**. 20 hand-worked
  PS2-vs-IEEE cases pass (1/0 = 0x7F7FFFFF, max*2 = max, denormal+0 = 0, NaN pattern > 1 is true,
  (int)NaN pattern = INT_MAX, the FPU add's exponent-gap rules, ...). The optimised object has **no** f32
  arithmetic / compare / convert instruction. Control: the same C compiled with host IEEE floats fails (2,091 of
  10,500 PCSX2 values, 12 hand cases), so the test does see the difference.
- Found and fixed on the way: clang 24 prints float constants as `f0x3F800000`. The first version of the rule did
  not match them and left 57 operations of `btl_char_move.c` as host floats. The safety net (no float instruction
  may remain) and the constant-operand tests were added because of it.
- One of my own hand-worked expectations was wrong (FPU add with an exponent gap >= 25 drops the smaller operand:
  1 - 2^-30 = 1.0); the routines were right, the expectation was corrected.
- Not covered: sqrt (a libm call, `Port_sqrtf`, not an operator); `frem` and 64-bit float conversions (none appear
  in the game's code; the build would stop if one did).

### All game sources compile (Priority 4, part 1): 269 of 269
- `python port/tools/wasm_build.py game`: 269 of 269 game sources (273 minus the 4 replaced vector files), 14,841
  float operations lowered, each object checked with `llvm-objdump` for f32 arithmetic. About 40 s.
- Fixes needed: `emcc -E` line markers on Windows hold single backslashes, which the second parse (libclang, r2l)
  read as escapes (`\o`, `\u`): normalised to `/`. libclang needs `-Wno-error=return-mismatch` (three functions
  without a return value). `portsrc.REPLACED` holds `/` paths, so on Windows the four replaced files were not
  excluded: filtered in wasm_build.py (portsrc.py left unchanged).
- Flags that hide diagnostics, as the native build's `-w` does: `-Wno-error=` return-mismatch,
  incompatible-function-pointer-types, int-conversion, implicit-function-declaration, incompatible-pointer-types.
  These matter more on wasm than on x86: a call through a pointer of the wrong function type TRAPS on wasm
  (`call_indirect` checks the signature). To be found at link time (wasm-ld) and at run time.
- Not yet linked or run: compiling is not a working build.

### Memory model (Priority 3): layout B chosen, PASS in node and three Chromium browsers
- Fixed addresses the game uses (plat_mem.c, include/sys/dma.h): heap 0x003BE000..0x02000000, hardware registers
  0x10000000 (64 KB; e.g. `VIF1_STAT = 0x10003C00` in game code), GS registers 0x12000000 (8 KB), scratchpad
  0x70000000 (16 KB). The scratchpad has no user in the game's C (no address literal; `Dma_SendVif1Spr` has no
  caller); the port maps it and `gs/state.c` snapshots it.
- The DMA code marks scratchpad lists with address bit 31 (`chain | 0x80000000`), so no address the game sees may
  be at or above 2 GB.
- **Layout B** (chosen): `-sGLOBAL_BASE=0x12010000 -sINITIAL_MEMORY=320MB -sALLOW_MEMORY_GROWTH
  -sMAXIMUM_MEMORY=0x70000000`. PS2 regions at their real addresses; the program's data, 16 MB stack and C heap
  above the GS registers; memory ends exactly at the scratchpad address, so any access to it traps ("memory access
  out of bounds") instead of landing in the C heap. Up to 1472 MB of C heap.
- **Layout A** (rejected on measurement): program above 0x70004000, everything at PS2 addresses, but under the 2 GB
  cap only 224 MB of C heap is left.
- `port/tests/wasm_mem`: 17 checks (sizes, 8-byte alignment of long long / double, struct and bit-field layouts,
  data / stack / malloc placement, each fixed region zero at start and holding a pattern, no collateral writes,
  pointer <-> u32 round trip, the PS2 heap address as a pointer, the cap), a scratchpad probe, growth until malloc
  returns NULL:

  | | node 24.19 | Chrome 154 headless | Edge 155 headless | built-in Chromium 152 |
  |---|---|---|---|---|
  | layout B | PASS, 1472 MB | PASS, 1472 MB in 30 ms | PASS, 1472 MB in 14 ms | PASS, 1472 MB in 17 ms |
  | layout A | 17/17, 224 MB only | 17/17, 224 MB: FAIL | 17/17, 224 MB: FAIL | not run |

  Instantiation with 320 MB initial memory: 4 to 7 ms. Every browser also allocated a bare 4 GB and 2 GB
  `WebAssembly.Memory` on this 31 GB machine, so allocation failure could not be provoked here; the loader will
  catch instantiation errors and show them. Not tested: Firefox, Safari, machines with little RAM.
- **Struct layouts**: the repository's `check_layout.py` against wasm32 (emcc -fsyntax-only through a wrapper; the
  tool unchanged): 14,862 documented PS2 member offsets in 183 headers, **1 differs** (`EftTask.unk4`, the same
  single report the native notes give for 32-bit x86 with `-malign-double`), 8 errors from `eft_z.h` (the tool
  asserts on a type declared later in that header; not a layout result).

### Platform code, data tables, link (Priority 4, part 2): the whole program links — 2026-10-11
Commands (Git Bash, `source /c/project/Tenkaichi3Web/env.sh`, in `Tenkaichi3Decomp/`):
```
python port/tools/wasm_build.py game     # 269 game sources
python port/tools/wasm_build.py port     # 62 port files (port/src, gs/, web/, newlib maths, ImGui)
python port/tools/wasm_build.py data     # 72 data tables + the list of their disc runs
python port/tools/wasm_build.py link     # stubs, wasm_sig.py, link -> port/build/web/{bt3.js,bt3.wasm,index.html,bt3_data.js}
```
Results: game 269/269, port 62/62 (584 float operations lowered there), data 73/73, **link OK**: `bt3.wasm`
4.7 MB, `bt3.js` 428 KB; 0 signature mismatches left; 103 stubs (`port/build/wasm_stubs.txt`).

- **Not built for the browser** (kept for the native builds): `gs/net.c`, `net_match.c`, `net_relay.c` (online play:
  UDP sockets, threads, matchmaking), `gs/gs_gpu.c` + `imgui_impl_sdlgpu3.cpp` (Vulkan through SDL GPU: SDL3 has no
  browser back end for it). `port/src/web/net_off.c` is the online-play interface in the state the native builds are in
  with no match (inactive, no names; `Port_NetView` / `Port_HudSwap` keep their non-network logic); `Port_NetInput`
  aborts with a message if ever reached (natively only called during a match).
- **SDL3**: Emscripten's port, SDL 3.4.2 (native builds use 3.4.12). Renderer: the GL back end on WebGL 2
  (`gs_draw.c` browser branch); ImGui's OpenGL3 back end with `#version 300 es` (`ui.cpp` browser branches).
- **Every file goes through LLVM IR** (soft-float files additionally through `wasm_sf.py`).
- **Data tables** (`port/tools/wasm_data.py`): one wasm data section per table file (contiguous, in order, 16-byte
  aligned, every label with its size), `.space`/`.zero` -> `.skip`, `.long sym+k` -> `.int32`, `D_3BE71C` -> the
  constant 0x3BE71C (natively `--defsym`). Function addresses in tables carry a placeholder `.functype`; checked with
  two objects in both link orders that wasm-ld still puts the real function's table slot there. The list of disc runs
  is generated as C (`port/build/gen_wasm/data_list.c`, offsets only) and linked; `plat_mem.c` (browser branch) turns
  it into the bytes of `<program>.dat` and runs the native loader code on it. 2,388 runs, 210,773 bytes, 1,872
  addresses: the figures of the native notes.
- **Signature mismatches** (`port/tools/wasm_obj.py`, `wasm_sig.py`): the game declares functions locally, not
  always as defined (`void f()` for `int f(int)`, one argument too many, ...). Harmless on the PS2 and x86; on wasm,
  wasm-ld replaces each such direct call by a trap. 171 functions, **813 call sites in 116 objects** repaired in the
  caller's IR to the definition's type (result dropped or read as 0, missing arguments 0, extra ones dropped, int/float
  bit-for-bit as the native software-float build passes them). Every repair is listed in `port/build/wasm_sig.txt`.
  Calls through function pointers are not covered: a mismatch there traps at run time (the page shows where).
- Other fixes: `main.c` compiled with `MainMenu_StartBallLoops` (a function used as the overlay's load address;
  the port's `sceRead` never writes there) mapped to a byte array; `heap.c` with `D_3BE71C` mapped to a pointer to an
  array at 0x3BE71C; `headless.c` with `__data_start`/`_end` mapped to wasm-ld's `__global_base`/`__data_end`;
  `plat_crash.c` browser branch (`Port_CrashContext` for the page); `newlib_shim.h`: `Port_scalbnf` prototype for the
  browser build.
- **Probable native bug, recorded, not changed natively**: `newlib_shim.h` includes `<math.h>` before renaming
  `scalbnf`, so `ef_pow.c` and `kf_rem_pio2.c` call `Port_scalbnf` without a prototype (implicit `int f()`: the float
  argument is passed as a double and the result read as an int). Affects `powf`'s subnormal path and `sinf`/`cosf` of
  large arguments in the native builds. The browser build declares the prototype.
- **Stubs**: 103 symbols nobody defines (98 SDK / CRI library functions in the game's address range, CRI's
  `dvCi*`/`htCi*`/`mfCi*` file-system drivers, `sceCdSearchFile`), most referenced only by library data tables. As
  natively (`link.py`), each stub prints `bt3: not implemented yet: <name>` and aborts: reaching one stops the game
  visibly, it cannot make start-up look successful.

### Game data in the browser (Priority 6/7, first part) — implemented, tested without game data
- All game-data reads in the port go through `fopen` (plat_fcache.c, snd_adx.c, snd_se.c, plat_mpeg2.c, plat_mem.c).
  The program is linked with `--wrap=fopen` (`port/src/web/web_fs.c`): a file in Emscripten's file system (saves,
  settings, prefetched files) opens as always; a read-only file under the data root that is not there opens as a
  stream (`fopencookie`) whose reads call the page's loader and WAIT for it (JSPI) without blocking the page.
- Why not the alternatives: synchronous XHR blocks the main thread; Emscripten's lazy files work only in workers;
  WasmFS's fetch backend needs pthreads (shared memory) for everything. JSPI cannot suspend before `main` (the
  constructor `Port_MapMemory` reads `disc/SLUS_216.78` and `disc/BIN/DBZP.BIN`): the page fetches those two first and
  writes them to the in-memory file system. Every other data read happens on the game's thread (the sound players
  load a whole file when they start; the audio callbacks only decode from memory).
- `port/web/bt3_data.js`: Range requests in 256 KB blocks, LRU cache (256 MB), sizes from `manifest.json` or `HEAD`.
  `node port/tests/web_data/test_data.js` (own HTTP server with scripted faults): **16 of 16 pass** — sizes by HEAD,
  URL encoding, 404 -> missing, 500 -> error, reads inside / across blocks / at and past the end, cache hit, LRU
  eviction and refetch, cache limit, concurrent reads sharing one request, server without Range (200, sliced),
  short body (error, not wrong data), 500 during a read.
- `port/web/devserver.py` serves the build and mounts the private data folder (`--mount gamedata=<folder>`; in Git
  Bash write it without a leading `/`, which MSYS would rewrite into a Windows path). It binds to 127.0.0.1 only.
- `plat_stub.c` browser branch: the vertical-blank wait is `emscripten_sleep` (a zero sleep when late), so the page
  gets control every blank: the frame is shown, input and sound are handled.

### Runtime test without game data (2026-10-11, built-in Chromium 152)
With a fake data folder (a 4 KB ELF header and 2 MB of zeros, nothing from the game) served at /gamedata:
checks pass (WebAssembly, JSPI, WebGL 2.0, data reachable), the two files are fetched, the module instantiates with
384 MB, the constructors run: memory map accepted, the two files read through the C library, all 2,388 runs copied,
and the engine's own check stops with its native message ("the game's programs in your game data are not the
unmodified USA release (SLUS-21678)"). With no data folder at all, the page says so before starting.
**Not yet run: `main`.** That needs the real data.

### FIRST BOOT IN THE BROWSER (2026-10-11): the engine initialises and runs the boot sequence — VERIFIED
Data: the user's own ISO (`C:\project\Tenkaichi3Web\bt3.iso`, volume SLUS_21678), unpacked outside the repository
with the repository's tool: `python Tenkaichi3Decomp/port/tools/extract_disc.py bt3.iso gamedata` (21 s; PZS3US0 1
file, PZS3US1 3,399, PZS3US2 65,201; 68,611 files in all, as the native notes count). `DBZP.BIN` SHA-1
4f910969e05d9b25c83af7642b60da9949a4348b and `SLUS_216.78` as the installer's flat image caee6c2269bba89fc51eea9e7beac3adbec9dc52:
both equal `config/*.yaml`.

```
python port/web/devserver.py --port 8080 --mount gamedata=C:/project/Tenkaichi3Web/gamedata
#   http://127.0.0.1:8080/index.html   (?autostart=1, ?report=<name>, ?env=BT3_...=...)
```
Run 1 (`boot1`): `main` ran, `Game_Main`'s initialisation went through into `Progress_Main -> FirstRun_Main ->
BootCard_Run`; two failures, both of the expected kinds:
- `bt3: gl: missing GL entry point glEnablei`: WebGL 2 has the per-attachment functions only as
  `OES_draw_buffers_indexed`. Fixed: `port/src/web/web_gl.c` + browser branches in `gs_gl.c` (ES 3.0 context, GLSL ES
  3.00 header with precisions, `std140` on uniform blocks, `EXT_clip_control`, `EXT_depth_clamp`).
- `RuntimeError: function signature mismatch` in `McFlow_CallDone` (every "done" callback called as `void(s32)`): a
  call through a pointer of another type. Fixed for all such calls: `-sEMULATE_FUNCTION_POINTER_CASTS=1` (missing
  arguments 0, extra ignored, absent result 0: the PS2's behaviour). Its speed cost is not measured yet.

Run 2 (`boot2`, built-in Chromium 152, this machine): log
```
bt3: display 1: 1, 1920 x 1080
bt3: internal resolution 2x (1024 x 896)
bt3: GL renderer: WebKit WebGL (OpenGL ES 3.0 (WebGL 2.0 (OpenGL ES 3.0 Chromium)))
bt3: gl: WebGL extensions: indexed blend/mask yes, clip control yes, depth clamp yes
bt3: sound: emscripten
```
All shaders compiled. Seen on the canvas (screenshots): the Funimation logo, the Spike logo, the game's logo, then
the opening movie (MPEG-2 through `plat_mpeg2.c`, read on demand). Module instantiation 29 ms; memory 384 MB at start,
553 MB during the movie. Data after ~30 s: 10 files, 89 requests, 17.9 MB fetched, 5,570 cache hits / 66 misses,
0 errors.

**Milestone reached:** the real engine initialises in the browser from the user's data and runs its boot sequence
(no ISO picker; the data is served by the local server). Not verified yet: sound actually audible (the page was
started without a click, so the browser may keep audio suspended), input, the title screen and menus, a battle,
frame rate, saves.

### CPU investigation and the WebGL 2 reference (2026-10-11) — measured
**Method** (`port/tests/bench/bench.js`, reproducible): the game's attract-demo battle (`BT3_DEMO=1`; the random seed is
fixed in `plat_sys.c`, no input) in Chrome 154.0.8037.98, headless, fresh throwaway profile, through the DevTools
protocol. GPU: ANGLE on Direct3D 11, NVIDIA GeForce RTX 5070 Ti (headless Chrome uses the real GPU); CPU AMD Ryzen 7
7700; 31 GB; Windows 11. `BT3_FRAME_LOG=1` gives one line per frame from the port's own timing point (`gs_core.c`):
**work** (the frame without the vertical-blank wait) = **game** code + display **list** (GS front end, VU1, recording,
of which **submit** = ending and submitting the frame to WebGL). Window: frames 300-2100 (1800 fight frames, 60 s,
after 300 warm-up frames of loading). Every run also hashes the fight trace (`BT3_TRACE=60`: health, positions as float
bits, clock): runs are identical (hash `8ad198bf58dbed33` in every run of the PS2-float build). Budget: 33.4 ms per
fight frame (30/s). The browser held 30.0 frames/s (2.00 vertical blanks per frame) in every run.

```
python port/web/devserver.py --port 8080 --mount gamedata=C:/project/Tenkaichi3Web/gamedata
node port/tests/bench/bench.js --label <name> --headless --from 300 --to 2100 [--profile] [--url ...] [--query gldebug=1]
```

| run | build | work mean | p50 | p95 | p99 | max | >33.4 ms | game mean | game p95 | list mean | submit mean | trace |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| base_r1 | original (page log in frame; getExtension per draw) | 17.01 | 18.14 | 21.82 | 24.78 | 56.45 | 6/1800 | 5.32 | 8.22 | 11.69 | 10.13 | 8ad198bf |
| base_r2 |  | 16.83 | 18.05 | 21.52 | 25.04 | 47.35 | 2/1800 | 5.24 | 8.16 | 11.58 | 10.02 | 8ad198bf |
| log_r1 | + page log out of the frame (ccdd15b) | 14.27 | 14.13 | 19.55 | 23.00 | 29.54 | 0/1800 | 2.51 | 6.72 | 11.76 | 10.18 | 8ad198bf |
| log_r2 |  | 14.32 | 14.06 | 19.89 | 24.16 | 45.97 | 4/1800 | 2.58 | 6.99 | 11.74 | 10.19 | 8ad198bf |
| ext_r1 | + extension objects cached (4f4787e) | 11.49 | 11.35 | 14.55 | 17.38 | 50.40 | 2/1800 | 2.30 | 3.10 | 9.19 | 7.61 | 8ad198bf |
| ext_r2 |  | 11.61 | 11.39 | 14.63 | 20.61 | 80.08 | 4/1800 | 2.38 | 3.17 | 9.23 | 7.67 | 8ad198bf |
| dclut_r1 | + WebGL 0x502 fix (02a52f3) = **current** | 11.61 | 11.49 | 14.64 | 18.35 | 40.24 | 2/1800 | 2.34 | 3.11 | 9.27 | 7.69 | 8ad198bf |
| dclut_r2 |  | 11.55 | 11.47 | 14.53 | 18.43 | 45.70 | 1/1800 | 2.34 | 3.15 | 9.21 | 7.64 | 8ad198bf |
| hf_r1 | diagnosis only: host IEEE floats (NOT the PS2's arithmetic) | 11.25 | 11.12 | 14.49 | 19.08 | 44.30 | 2/1800 | 1.98 | 2.75 | 9.27 | 7.69 | 590203ae |
| hf_r2 |  | 11.21 | 11.12 | 14.13 | 16.87 | 45.81 | 2/1800 | 1.94 | 2.64 | 9.27 | 7.70 | 590203ae |

(milliseconds; "max" and ">33.4 ms" are single frames: 1-6 of 1800 per run, all in `list`/`submit`, not examined yet)

**Findings**
- A correction to an earlier statement: "game code 20-38 ms" came from the port's `slow:` lines, which are printed only
  for frames over budget (a biased sample). Measured over every frame, the original build's game code was 5.3 ms.
- **The page's own log was half of the "game code"**: printf lines arrive inside the frame and were written to the DOM
  and console at once (profile: `log` 11.6 % of all CPU). Queued and painted 4x a second: game 5.3 -> 2.5 ms.
- **`getExtension` per draw** in `web_gl.c` (profile: 7.7 % of all CPU): cached; submit 10.2 -> 7.6 ms.
- **PS2 float model: about 0.36 ms per frame** (game 2.32 -> 1.96 ms with host floats in the `hf` diagnostic variant;
  its fight diverges, as expected, but repeats exactly between its own runs). About 15 % of the game code.
- **Pointer-cast emulation: a full A/B is not possible.** Without `-sEMULATE_FUNCTION_POINTER_CASTS` the battle traps in
  `BtlTask_Init` (the battle's task system calls every task callback through one pointer type), so a battle cannot run
  without it. Measured bound: the emulation's thunks take 0.1 % of all CPU in the fight (about 0.03 ms per frame); the
  widening of arguments at each indirect call site is inside the callers and not separable in the profile.
  **Not verified: that its behaviour equals the native ABI.** That needs the native build's fight trace of the same
  demo (a native build needs MSYS2 or Docker here; neither is available yet). Kept, since without it nothing runs.
- Profile of the current build (fight window, 200 us sampling): 75 % idle; of the busy time the largest parts are
  WebGL's own work (`bufferSubData` 3.1 % of all, binds, draws) and the JS<->wasm transitions (2.2 %), then the game
  and the renderer's front end (about 10 %), the PS2 float routines (3.0 %).
- **What is left is the WebGL submission: 7.7 ms of the 11.6 ms** (natively, per the repository's notes: Vulkan 1.7 ms,
  OpenGL 3.7 ms, measured on other CPUs). Game code 2.3 ms (natively about 1.5 ms on another CPU; no native build here).

**WebGL 2 errors (0x502) — fixed, verified**
- `?gldebug=1` (page): every WebGL call checked; every failure was `drawArrays(TRIANGLES, 0, 3)` in `frame_end` with
  "unit 0 = depth attachment": the depth table pass samples its target's depth texture while drawing into it. WebGL
  refuses such a feedback loop and drops the draw (desktop GL tolerates it), so the depth tint / glare feed / distance
  blur were missing in the browser. The pass neither tests nor writes depth: it now draws into a framebuffer with the
  same colour attachments and no depth (browser build only).
- Chrome 154, whole fight window, `BT3_GS_VERBOSE` per-frame check: **2,009 frames with an error -> 0**. Chromium 152
  (app's browser, `?gldebug=1`): 207 error lines in ~18 s -> 0. Fight trace unchanged; work 11.6 ms before and after.
- Earlier "Chrome 154 shows no errors" was unmeasured: the per-frame check only runs with `BT3_GS_VERBOSE`.
- Only this scene was checked (the demo battle); menus and other stages not yet.

### WebGPU shaders: WGSL generated from the GLSL (2026-10-11) — verified
- **Toolchain** (build only; nothing of it is in the game): naga-cli **30.0.1** (gfx-rs/wgpu's shader translator, from
  crates.io, `--locked`), built with Rust **1.99.0** installed into the project's tools folder without touching PATH:
  ```
  RUSTUP_HOME=C:/project/Tenkaichi3Web/tools/rust/rustup CARGO_HOME=C:/project/Tenkaichi3Web/tools/rust/cargo
  rustup-init.exe -y --no-modify-path --profile minimal --default-toolchain 1.99.0      (win.rustup.rs, 12.7 MB)
  cargo +1.99.0 install naga-cli --version 30.0.1 --locked                               (74 s)
  ```
  Linker: Visual Studio 2022 Build Tools (already installed). `port/tools/wasm_shaders.py` refuses another naga
  version; `BT3_NAGA` names another executable.
- **Prebuilt alternatives checked first, not taken**: no official naga binary exists (wgpu v30.0.1 release has no
  assets; nothing in scoop / winget); npm wrappers `naga-wasm` 30.2.0 and `naga-js-*` 30.0.1 embed the official naga
  but are weeks-old single-maintainer repositories (0 stars); scoop's glslang is 8.13 (2019). Emscripten's
  `emdawnwebgpu` port gives the WebGPU C API (Dawn's webgpu.h), not a shader compiler.
- **What naga accepts from our nine shaders**: the 5 vertex shaders as they are; the 4 fragment shaders fail on
  `uniform sampler2D` ("Not implemented: variable qualifier": its GLSL frontend has no combined image samplers; its
  SPIR-V frontend maps a sampled-image type to the image alone, so glslang -> SPIR-V -> naga does not help either).
  The fragment shaders now declare, under `SEPARATE_SAMPLERS` (defined only when naga translates them), a texture at
  binding 2k and its sampler at 2k + 1 of the same set; the `#else` part is the original line, so Vulkan (glslc) and
  OpenGL compile exactly what they did.
- **Checks**: naga validates all 9 (`python port/tools/wasm_shaders.py`); Chrome 154's own WebGPU compiler accepts all
  9 with no message (`node port/tests/wgsl/check.js`, adapter "nvidia blackwell"); the WebGL build with the edited
  sources keeps the fight trace (`8ad198bf58dbed33`), no GL errors, work 11.50 ms.

### Shared renderer layer clean-up (2026-10-11) — behaviour-preserving, verified
Before the WebGPU back end, the duplication between the back ends was removed from the shared layer; the GL back end
(verifiable in the browser) uses it. The Vulkan back end (`gs_gpu.c`) is not changed: it cannot be built here and is
out of the browser port's scope; it still carries its own copies of what is listed below (a follow-up for when a
native build can be checked).

| commit | change | files |
|---|---|---|
| 359653e | picture regression test: `bench.js --shots N`, `compare_shots.js A B`; page exposes `window.bt3Module` | port/web/index.html, port/tests/bench/{bench,compare_shots}.js |
| efb4deb | one pipeline-key encoder/decoder (`GsPipeState`, named enums), one API-neutral blend description (`GsBlendDesc`) | port/src/gs/gs_pipekey.c (new), gs_draw.h, gs_draw.c, gs_gl.c, port/tests/pipekey/{test.c,run.py} (new) |
| 0d760f9 | named constants: `GS_SAMPLER_*`, `GS_DISPLAY_W/H`, `VU_VERT_FLOATS/BYTES`, `GS_OUTLINE_DARKNESS` | gs_draw.h, gs_draw.c, gs_gl.c |
| 902869d | shared `GsDraw_PollEvents`, `GsDraw_PictureSize`, `GsDraw_FrameReset` | gs_draw.c, gs_draw.h, ui.h, gs_gl.c |

Tests after every commit: WebGL build; demo battle frames 300-2100 with screenshots every 150 frames
(`node port/tests/bench/bench.js --label <x> --headless --shots 150 --env BT3_GS_VERBOSE=1`), compared with the
pre-clean-up reference (`node port/tests/bench/compare_shots.js pre_cleanup_A <x>`): **12 of 12 screenshots identical,
fight trace `8ad198bf58dbed33`, 0 GL errors**, at every step. The reference itself is reproducible: two runs before the
clean-up, 12 of 12 identical. Pipeline key: `python port/tests/pipekey/run.py`, all **9,953,280** combinations of
ALPHA / ABE / TEST / ZMSK / FBMSK / topology / vertex source: 0 keys differ from the old encoding (the old code is kept
in the test as the reference), 0 change on decode + encode, 0 blends differ from the old OpenGL choice
(`-Wall -Wextra -Werror`). F1 opens / closes the settings window through the shared event loop (seen).

Left as they are, on purpose: where the letterboxed picture is placed (each back end's origin differs, and the GL back
end's rounding of the centred position differs by one row from Vulkan's when the free height is odd); tuned
thresholds in the widescreen layout code (not constants of the same kind). Still duplicated between the GL and Vulkan
back ends: the `BT3_GPU_CUT` / `BT3_GPU_TAIL` debug switches and the PPM screenshot writer (the writer is to be shared
when the WebGPU back end needs it).

### WebGPU back end: implemented and compared with WebGL 2 (2026-10-11) — verified
`port/src/gs/gs_wgpu.c` implements every `GsBackend` entry (commit c6a8aca); select it with `?gpu=webgpu`. WebGL 2 stays the
default and the reference. Fixed objects are made in a validation error scope (init fails with WebGPU's message if one
is wrong); uncaptured device errors are counted and logged; device loss is reported once and drawing stops.

**Fidelity** (12 screenshots each, `bench.js --shots`, `compare_shots.js`; thresholds not relaxed — every difference examined):
| run | result |
|---|---|
| boot (logos, opening movie through the display-buffer upload, title), frames 600-2800 | **12/12 identical** to WebGL 2 |
| demo battle, hardware (RTX 5070 Ti; WebGL = ANGLE/D3D11, WebGPU = Dawn/D3D12) | 0 device errors, fight trace identical (8ad198bf58dbed33); 0.19-1.40 % of pixels differ, nearly all by 1 level, plus 3-28 isolated pixels per frame (pairs one GS pixel apart with colours swapped: nearest-texel ties) |
| demo battle, both APIs on SwiftShader (`--chrome-flags use-angle=swiftshader` for WebGL; `use-webgpu-adapter=swiftshader,enable-unsafe-webgpu,enable-unsafe-swiftshader` for WebGPU), frames 300-750 | 0.008-0.012 % differ: single channels by 1 in the bilinear sky (interpolation rounding) and one pixel by 12 on a diagonal edge between two surfaces (a pixel centre on the shared edge: GL rasterizes the picture mirrored, its gl_Position.y flip, so the fill rule's tie goes the other way) |
Conclusion: the large hardware outliers come from the two drivers; on a common rasterizer what remains is rounding and
one edge tie, no rendering defect. Menus beyond the title and the settings window: not yet compared (below).

**Performance** (demo battle, frames 300-2100, headless, no screenshots; `perf_gl2` / `perf_wgpu2`):
| ms per frame | WebGL 2 mean / p95 / p99 / max | WebGPU mean / p95 / p99 / max |
|---|---|---|
| work (game + list) | 11.70 / 14.84 / 17.23 / 23.74 | **7.07 / 9.65 / 12.21 / 19.58** |
| game | 2.33 / 3.17 / 5.61 / 8.94 | 2.87 / 3.98 / 5.08 / 15.78 |
| list (replay + present) | 9.37 / 12.18 / 13.51 / 20.16 | **4.20 / 6.42 / 8.13 / 11.98** |
| submit | 7.75 / 9.62 / 11.39 / 15.83 | **2.49 / 3.96 / 4.64 / 5.29** |
Frames over the 33.4 ms budget (work): 0 / 1800 for both. Wall pacing identical (headless: 2 vblanks per frame).
WebGPU objects by frame 2070 (`BT3_GS_VERBOSE=1`): 0 errors, 1984 textures made (all live: the shared cache), 12923
bind groups (cached per texture and sampler, plus 1-2 table-pass groups a frame), 30 pipelines, uniform buffer 3.5 MB.
Not measured yet: GPU time (needs `timestamp-query`), GPU memory in bytes. (With `BT3_GS_VERBOSE` the GL back end
calls glGetError every frame, a GPU sync: don't compare GL timings taken with it.)

**Not implemented**: native pass 2 (alpha key) is no longer recorded; if it ever were, it is reported, not drawn. (The
settings window no longer depends on the renderer: see "Settings panel in the page".)

### Game saves kept in the browser (IndexedDB), with recovery, export and import (2026-10-12) — verified with the game's own saves
**Audit**: settings were kept (localStorage, the engine's settings file) but **game saves were not**: the game saves to an
emulated memory card (`plat_mc.c`: `saves/card1/<save folder>/` — icon.sys 964 bytes, the icon, the data file named
after the folder: the game save 0x4000 bytes, a replay 0x1AC00), which in the browser lay in the engine's in-memory
file system and was lost with the page. The game's data files carry its own checksum (`McCard_Checksum`,
src/battle/stgm_a_b.c: the first two words, over the rest of the block).

**Engine side** (`web_ui.c`; plat_mc.c unchanged): the card's sync / write / close are linked wrapped
(`--wrap=func_002A2498,func_002A2320,func_002A1FB8`): a file counts as open from sceMcSync's open result to its close;
when a written file is closed and no card file is open, the card's files are whole and the page is told (`cardWritten`).
`Bt3Ui_SaveChecksumOk` runs the game's own checksum on a buffer for the page (`_malloc` / `_free` exported).

**Page** (`port/web/saves.js`, IndexedDB database `bt3-saves`, store `card`, records `current` and `backup`, record
schema 1 — separate from the settings (localStorage), the data cache, the diagnostics):
- **Keeping**: the card is copied at that moment, checked (every save folder has icon.sys of 964 bytes and its data
  file; the data file has a size the game writes and passes the game's checksum; files of a save still being written —
  the game writes icon.sys, the icon, then the data — are waited for, quietly), and written in one transaction with
  strict durability; reported kept only on its completion. A card that fails is not kept and the stored one is not
  touched. Each file gets a SHA-256 against damage in storage. `navigator.storage.persist()` is asked for after a save.
- **Versions**: the previous distinct game save becomes the backup (a copy of the same save does not push it out; a
  damaged current one never replaces a good backup).
- **At start** (before the game runs): the current version is put on the card; if it is damaged (hash or game checks)
  the backup is, with a notice. The game then sees its save (title: Continue; the main menu loads it).
- **Saves tab**: storage state (lasting or not, usage / quota), last save (kept or not, why), both versions with their
  checks, "Use the previous version" (swaps them; reload), export of a .zip in the card's own layout (BASLUS-21678...,
  as a PS2 card folder or the native build's saves/card1), import of a .zip (stored or deflated) or a folder, checked
  like the game's own saves, then kept as the current version (reload to play with it). The browser's limits are
  explained: site data cleared, private windows, eviction when space runs short; export to keep a copy.
- **Failures** are said: no IndexedDB (saves last for the session; said at start and at each save), quota full,
  any write error — the stored save stays as it was.

**Tests** (`port/tests/web/saves.js`, PASS, 18 checks; the game's real saves, driven by scripted pad presses at fixed
frames, BT3_PAD — the page's env parser now allows commas inside a value): New Game creating the save (kept, generation
1, the save under way waited for without a warning); reload (the stored bytes on the card, exactly); an update by the game
after Unlock everything (Options > Save/Load > Save > overwrite: generation 2, generation 1 the backup); export (a zip in
the card layout); import (the export: kept; a malformed zip, a save failing the game's checksum, a save cut short:
refused, nothing changed); recovery (versions swapped); an interrupted write (an aborted transaction: the stored save
unchanged); damaged stored data (the backup used at start, with a notice); no IndexedDB (said; the game still saves for
the session); quota exceeded (reported, stored save unchanged). Done by hand too: New Game save, reload, Continue to the
main menu with the save, Options > Save, the Saves tab. Not tested: a browser killed in the middle of a write (the
transaction's atomicity is the browser's; an aborted transaction was tested), real eviction by the browser, replay saves
(the format is checked by size and checksum, but no replay was saved in these runs).

### Controllers: devices, live view, guided set-up, presets, test (2026-10-12) — verified with simulated controllers
The Controls tab's controller part (`port/web/controllers.js`), on the engine's own input model (gs_input.c: a key per
action, a controller button per pad button, a controller slot per player = the n-th controller SDL lists):
- **Devices**: the browser's controllers (Gamepad API: id, browser index, layout, button and axis counts) and which of the
  game's controller slots each one is (SDL's list matched by USB vendor / product, `Bt3Ui_GamepadVendorProduct`; SDL
  names controllers its own way). A browser shows a controller only after a button press; it reports little: said so.
  In a browser SDL takes every controller: with the standard layout its buttons are placed right; otherwise SDL places
  them by its database of makes, or guesses — the page warns that its buttons may be placed wrongly.
- **Live view** of one controller (buttons lit, axes as meters), updated once per display refresh only while visible.
- **Warnings**: a player set to a controller that is not connected; a notice when one is disconnected (a set-up of
  controller buttons under way is cancelled).
- **Guided set-up** of a player's controller buttons (16) or keys (24): the engine takes each (`Bt3Ui_CaptureBegin`, the
  keys on the game area where SDL hears them); a value another action of the player has is offered Swap / Keep both /
  Choose again; Skip, Back, Esc (keys: keeps it), Cancel (puts every binding back as before). The table's own capture
  buttons wait while it runs.
- **Keyboard presets** by the engine's action labels and SDL key names (`Bt3Ui_ScancodeFromName`, `Bt3Ui_BindingSet`): the
  engine's defaults (`reset_keys`: the engine's own table, the controller buttons kept), left hand on W A S D, numeric
  keypad; a key the other player also has is reported. "Controller buttons to defaults" (`reset_pads`).
- **Test**: shows the game buttons this player's bindings give (by include/sys/pad.h's names) and the sticks, while
  nothing reaches the game.
- **Persistence**: the engine's settings file (localStorage, as the other settings).

**What the game receives**: the browser build links `Port_PadRead` wrapped (`-Wl,--wrap=Port_PadRead`, web_ui.c): every
read is recorded as the game got it; in the test, with the panel open, the engine's own read runs as if the panel were
closed and the game is handed a released pad. Closing the panel calls the engine's `PortInput_Swallow`: controller
buttons still held are not the game's until let go.

**Tests** (`port/tests/web/controllers.js`, PASS, 29 checks) — **simulated controllers only**:
`port/tests/web/fake_gamepads.js` replaces the browser's Gamepad API before the page loads and sends the browser's own
connection events; the engine reads the devices through SDL's real Emscripten joystick code. Covered: connection and
device information, live view (button, axis), a controller without the standard layout (listed, warned), three
controllers with a disconnect and a reconnect reusing browser index 0 (slots matched right), the missing-controller
warning, set-up (capture, Back, Skip, duplicate Swap, Cancel restoring all, a completed set-up), key capture with real key
events, presets and defaults, nothing reaching the game while the panel is open (held button; the test shows Cross,
the game gets nothing), a button held while closing kept out of the game, play resuming (controller and Enter), the
set-up kept after a reload. **Not tested: any physical controller** (none was available).

### Live performance dashboard and diagnostics export (2026-10-12) — verified
The settings panel's **Developer** section (`port/web/dashboard.js`) shows, twice a second while it is open:
| row | source and definition |
|---|---|
| Renderer | WebGL 2 / WebGPU and the adapter the page's checks found |
| Pictures shown | display refreshes with a new picture presented since the last one (requestAnimationFrame + the present: WebGL 2 binding the window's framebuffer, WebGPU taking the canvas texture), now (last second) and average over the window |
| Game ticks, game speed | the engine's ticks per second; vertical blanks per second against 59.94 |
| Work / game / drawing / submit | the engine's per-tick CPU times (gs_core.c, the values of BT3_FRAME_LOG): work = the tick without the wait for the display; game = the simulation; drawing = the display list and the renderer's frame; submit = the renderer's replay and submission (CPU) — mean / p95 / p99 over the last 10 s |
| GPU time | the GPU's own time per frame: WebGPU `timestamp-query` (first pass's start to the present pass's end), WebGL 2 `EXT_disjoint_timer_query_webgl2` (TIME_ELAPSED around the frame; disjoint frames dropped); "not measurable here" where the browser offers neither. Browser-rounded; the two APIs measure different spans (here WebGL 2 ~10 ms, WebGPU ~1 ms) |
| Long frames | ticks over the 33.4 ms budget in the window; page frames over 50 ms (long-animation-frame) since opened |
| Memory | JS heap (Chrome's performance.memory, a browser estimate), WebAssembly (the engine's linear memory, reserved), GPU: WebGPU = the engine's own sum of the sizes of the textures and buffers it asked for (an estimate; no browser API reports GPU memory), WebGL 2 = not measurable (not tracked). A button asks the browser for `measureUserAgentSpecificMemory` |
| Game data | the loader's real counts: files, requests, bytes, blocks in flight, cache, errors (read on demand: there is no total) |
| WebGPU errors | count and latest message (the last 8 kept) |
| This section's cost | its own updates as a share of the time since it was opened (0.13 % measured) |
Plus a graph of the last 300 ticks' CPU work and GPU time against the budget line.

**Data path**: `web_ui.c` keeps one sample per tick (and one per GPU time, which arrives frames later) in fixed rings of
1024 (`Bt3Ui_PerfRing`); the page reads what is new straight from memory. Nothing runs while the section is closed (no
timer, no present counter, no long-frame observer); ticks from while the tab was hidden (throttled) are left out of the
statistics. Renderer restarts reload the page (fresh numbers).

**Export diagnostics**: a download (never sent) of `bt3-diagnostics` JSON version 1: renderer, GPU, the page's query,
browser capabilities (user agent, processors, memory class, WebGPU, JSPI, screen), engine state (frame, GPU timing,
WebGPU error texts), every setting's current value and the settings file if written, the whole tick and GPU rings with
column names and definitions, the window's summary, memory, loader state, and the log's error lines only (at most 200).
Not in it: saves, game data, the rest of the log. About 45 KB.

**WebGPU timestamp workaround**: emdawnwebgpu v20260423.175430 passes `WGPU_QUERY_SET_INDEX_UNDEFINED` to the browser as
4294967295 (library_webgpu.js copies the field as a number); the browser rejects the pass and the frame. A pass that
times one end writes its other end into a scratch query (`WG_TIMESTAMP_SCRATCH_QUERY`, gs_wgpu.c, documented there).

**Cost** (`BT3_GPU_TIMING=0` switches the GPU timing off, to measure it): work per tick, demo battle, two alternating
runs each: WebGL 2 on 14.56 / 14.07, off 13.70 / 14.44 ms; WebGPU on 8.02 / 6.83, off 7.26 / 8.50 ms: no difference above
the run-to-run variation (about +-0.8 ms). Pictures unchanged (demo 12/12 identical, trace unchanged, both renderers).

**Tests**: `port/tests/web/dashboard.js` (both renderers: PASS): every row has a value, the graph is drawn, the present
counter only while open and removed after, the export's schema, samples, settings, error lines only, no save or game
data, a .json download, ticks of a hidden tab left out (another tab in front: 120 left out).

### Smooth motion in the browser: measured, limited per renderer (2026-10-12) — verified
**How it works**: after a tick the engine shows in-between pictures (`gs_draw.c` GsGpu_InterpPump / interp_show): each
one replays the whole frame's list with blended vertices (a full `frameEnd`), spaced over the tick with
`SDL_DelayPrecise`, which in the browser calls `emscripten_sleep(1)` (it yields). A page's picture reaches the screen
at a display refresh after the page yields, so pictures drawn without a yield in between are never seen.

**Measuring tool**: `port/tests/bench/present_rate.js`. It counts, per display refresh (`requestAnimationFrame`),
whether a picture was presented since the last one (WebGL 2: the window's framebuffer bound for the present; WebGPU:
the canvas texture taken): pictures that reached the screen, and the gaps between them. A 60 Hz display (none here,
and no Chrome switch fixes one) is modelled from the presents' times; the same model for the real 200 Hz display
matches the measured count exactly (WebGL 30.0 / 47.0 / 47.9 / 48.4; WebGPU 30.1 / 59.3 / 100.8), so the model holds.

**Results** (demo battle, RTX 5070 Ti, headless Chrome on the 200 Hz display; work = the engine's time per tick):
| renderer, setting | shown/s | at 60 Hz (model) | gap median / p95 / max ms | in-between/s | game speed | work mean / p95 ms |
|---|---|---|---|---|---|---|
| WebGL 2, off | 29.9 | 29.9 | 33 / 36 / 36 | 0 | 100 % | 12.3 / 14.7 |
| WebGL 2, 60 | 48.2 | 46.6 | 21 / 30 / 51 | 22.5 | 100 % | 29.3 / 41.8 |
| WebGL 2, 120 / 240 | 48.6 / 47.7 | 47.3 / 46.1 | 20-21 / 31 / 52-53 | 22 | 100 % | 30 / 41 |
| WebGL 2, 60, frame skipping off | 53.4 | 49.6 | | 26.1 | **91 %** | 35.5 / 42.6 |
| WebGPU, off | 30.0 | 30.0 | 33 / 35 / 37 | 0 | 100 % | 6.8 / 10.1 |
| WebGPU, 60 | **59.1** | 51.2 | **17 / 21 / 34** | 29.3 | 100 % | 14.4 / 17.3 |
| WebGPU, 120 / 240 | 89.6 / 86.3 | 54.5 / 54.0 | 10 / 24-34 / 47-51 | 68 / 88 | 99-100 % | 28.5 / 42 |
Also run with vsync and the frame-rate limit off (`disable-gpu-vsync, disable-frame-rate-limit`: 90-110 refreshes a
second): the same pattern, game speed 99-100 % everywhere.

**Root cause of the slowdown measured before** (90 % at "60", 74 % at "120", on 0.1.25): each in-between picture costs a
replay of the frame (about 11 ms with WebGL 2 here, 5 ms with WebGPU), and with WebGL 2 one of them already takes a
tick's work past its 33.4 ms; the game advances once per tick, so it slowed. (The earlier measurement counted ticks,
which cannot show in-between pictures: what reached the screen was not measured then.) 0.1.31 added a cost guard and
frame skipping: the speed now stays at 100 %, but by leaving real ticks out, which makes gaps longer than without
smooth motion (51-53 ms against 36): uneven, not smoother.

**Decision**: offered only where it shows more pictures, evenly, at an unchanged game speed: WebGPU up to "60"
(`web_ui.c` Web_InterpLimit; `GsDraw_InterpLimit` in gs_draw.c, natively all settings as before). WebGL 2: none. The
limit applies to the saved setting and BT3_INTERP at start (clamped), to F9 (does nothing on WebGL 2, with a message
in the log; toggles off / 60 on WebGPU) and to the panel (Video, "Smooth motion": only the allowed choices; disabled
with WebGL 2, with the reason in its help text). Native builds: unchanged (the limit is the old maximum there).

Also: a fault in the page's panel no longer ends the game (it did: a panel bug stopped the engine at start); the
engine's calls into the page catch it, log it and show a notice. Regression: demo 12/12 identical, trace unchanged on
both renderers.

### Upstream 0.1.31 integrated (2026-10-12) — verified
**Upstream commit**: `fff151eb989ecaa5a0aaeb637984e33e07ef3ad9` (z3xox/Tenkaichi3Decomp `main`, "Release 0.1.31"): 39
commits on top of the previous base `80e0dd4` (0.1.25 to 0.1.31). **Strategy**: a merge of `origin/main` into
`browser-port` (no rebase, no force-push; our 38 commits kept as they were). Backup of the branch before the merge:
`browser-port-pre-0.1.31`.

**What upstream changed** (36 files): online play (quick match, match speed, names), the settings window (UI tab "HUD
Style", V-sync, frame skipping, voice and stage-sound volumes, "music follows game speed", a game speed of one's own,
Esc asks before closing), audio (streams keep their pitch at another speed), the renderer (depth as whole numbers per
pixel: `gs.frag` writes `gl_FragDepth`, the vertex programs hand over whole-number depth; a variant without the depth
output, `gsnz.frag`, key bit 23; Params grew by `extra`; frame skipping; in-between-picture and water-ring fixes), the
HUD in a wide picture (`hud_b.c`), and its native build (`undefined.py` generates `gsnz.frag`). No removed files; one
added (`port_release.h`). No game-data or build-dependency change.

**Conflicts and how they were resolved** (4 files):
| file | resolution |
|---|---|
| `shaders/gs.frag` | both: upstream's depth output and our SEPARATE_SAMPLERS block |
| `plat_stub.c` | upstream's `BLANK_NS` (the game speed's vertical blank) with our browser yield (`emscripten_sleep(0)`) |
| `gs_draw.c` (`pipeline_get`) | our shared encoder (`GsPipeState`), with upstream's bit-23 rule moved into `gs_pipekey.c` as `GsPipeState_FragDepthRedundant` (+ `noFragDepth` in `GsPipeState`, encoded and decoded); `BT3_NZ` kept |
| `gs_gl.c` | our decoded `GsPipeState` and shared event loop; upstream's programs, Params size, depth-qualifier stripping and V-sync kept; its 8 programs as `sMainProg[variant][vertex source]` |

**Adaptations of the browser port** (Vulkan untouched: `gs_gpu.c` and `undefined.py` are byte-identical to upstream):
- `GsDraw_PollEvents` (the shared event loop) takes upstream's "Esc asks first" (`Ui_QuitAsk`); `GsDraw_DepthMax`
  (gs_draw.c) is the one place of the Params `extra.x` formula for GL and WebGPU (Vulkan keeps its own copy).
- Shaders: `wasm_shaders.py` makes `gsnz.frag` (variants table, the same rule as `undefined.py`; the GLSL table of
  `wasm_build.py` uses it). naga 30.0.1 does not accept `layout(depth_less)` (conservative depth, absent from WGSL):
  `gs.frag` skips that declaration under `NO_DEPTH_QUALIFIER`, which only the WebGPU build defines, and writes the
  built-in. 11 WGSL shaders, all pass Chrome's compiler.
- WebGPU: Params 80 bytes with `extra`, the `gsnz` module for keys with bit 23.
- Functions upstream added that the browser build lacked (the link had made aborting stubs of them; one,
  `Port_NetSideOptions`, runs at every battle load): `Port_NetSpeedMilli` / `Port_NetSideOptions` in `net_off.c`
  (net.c's values outside a session), `Ui_QuitAsk` (a notice: a tab cannot be quit from the game) and `GsGpu_Vsync`
  (reports that a browser has no V-sync setting) in `web_ui.c`. Stubs back to the 103 of before.
- Settings panel: voices and stage-sound volumes, music follows game speed, HUD style, frame skipping, game speed
  50-200 % with a reset. Not offered: V-sync (a browser shows pictures at the display's refresh; SDL's swap interval
  only changes the page's timer mode), the online screen's new parts (online play is not in the browser build).

**Build**: Emscripten 6.0.12, naga 30.0.1, SDL3 port 3.4.2, Dawn v20260423.175430 (all unchanged). Port 61/61, the 5
changed game files recompiled, link OK, 103 stubs, 0 signature mismatches.

**Regression** (Chrome 154, RTX 5070 Ti; references from before the merge):
| test | result |
|---|---|
| pipeline key (`tests/pipekey`, now with upstream 0.1.31's encoding and BT3_NZ on / off) | PASS, 19 906 560 combinations |
| WGSL (`tests/wgsl/check.js`) | PASS, 11 shaders |
| fight trace, demo battle, WebGL 2 and WebGPU | `8ad198bf58dbed33` on both: unchanged (upstream did not change the demo's fight) |
| demo battle screenshots, WebGL 2 vs before | 0/12 identical, 0.01-0.23 % of pixels: upstream's whole-number depth. **With `BT3_DEPTH_FRAC=1`** (upstream's switch back to depth with fractions): **12/12 identical** — every difference is that change |
| demo battle, WebGPU with `BT3_DEPTH_FRAC=1` vs before | 12/12 identical, 0 device errors |
| demo battle, WebGPU vs WebGL 2 (new depth), hardware | 0.19-1.40 %, the same driver pattern as before the merge (D3D11 vs D3D12) |
| the same on SwiftShader (both APIs), frames 300-750 | 0.008-0.012 %, every one by 1 level |
| boot (logos, movie, title), WebGL 2 and WebGPU vs before | 12/12 identical each |
| page: launches, both renderers selectable (panel switch to WebGPU and back), new settings change the engine | verified in the browser pane: game speed 100 / 114 / 150 % measured 100 / 114 / 149 %; with frame skipping off at 150 % the game slows to 115 % (the pane draws 35 pictures a second) — both settings act as upstream intends |
Cost of the new depth on WebGL 2 (demo, work per frame): 12.0 ms with `BT3_DEPTH_FRAC=1`, 14.1 ms without; WebGPU
7.4 ms (one 88 ms frame in the run).

**Not verified**: the native builds (no native toolchain here; their sources are upstream's except our earlier,
behaviour-preserving shared-layer changes), a physical controller, the HUD styles' look in a wide picture (the setting
reaches the engine; the demo is 4:3).

### Settings panel in the page; no Dear ImGui in the browser build (2026-10-11) — verified
The browser build compiles no C++: `gs/ui.cpp` and Dear ImGui are left out (`wasm_build.py`); `port/src/web/web_ui.c`
is the engine's side of `ui.h` there, and the settings are an HTML panel beside the game (`port/web/settings.js`). The
canvas shows the game alone. Native builds, the Vulkan back end, `ui.cpp`, `gs_input.c`, `gs_draw.c`, `gs_gl.c` and
`headless.c` are unchanged (`git diff` against the commit before: no change).

**Interface** (`web_ui.c`, exported `Bt3Ui_*`): `Get(name)` reads a value at once; `Set(name, value)` and
`Action(name, arg)` queue a change, applied by the game's thread between two frames (an SDL user event that
`GsDraw_PollEvents` hands to `Ui_Event`); the engine then calls the page's `changed`. Values stay where the native
build keeps them (PortVideo via `GsGpu_Get/SetSettings`, `gs_input.c`'s bindings, `Port_ClashSquare*`,
`gPortUnlockAll`) and are saved through `Port_SettingSave` into the engine's settings file, which the page keeps
between visits (localStorage). Bindings are captured by the engine (SDL's key codes and names): the page puts the
focus on the canvas and the next key or controller button is taken there. The engine's settings key (F1, START +
SELECT, or the bound one) calls the page's `toggle`.

**The ImGui window's controls and where they went**
| ImGui (ui.cpp) | Browser |
|---|---|
| Internal resolution, aspect ratio, 2D filtering; effects (6) and glow; music and effects volume; texture pack (shown only if a pack is found) | panel, same engine settings |
| Renderer (Vulkan / OpenGL, next start) | WebGL 2 / WebGPU: restarts the page with `?gpu=` |
| Full screen (F11) | page button (browser Fullscreen API; a browser grants it only from a click, so the saved setting cannot apply it at start) |
| Controls: player, controller, reset, 24 bindings + the settings key; clash option and its worth | panel, same engine values |
| Cheats: unlock everything | panel (offered once a save is loaded; reports the result) |
| Meter (frame rate, game speed) | Developer section (with renderer, GPU, resolution, frame, WebGPU errors, screenshot, restart with start-up switches) |
| Display (monitor choice) | not offered: the browser decides |
| Smooth motion (60 / 120 / 240) | **not offered**: measured in the browser, it slows the game (speed 90 % at "60", 74 % at "120") and the in-between pictures do not reach the screen (a page shows one picture per animation frame). The engine's F9 still switches it, with that effect |
| Online play window, players' names, connection meter | online play is not in the browser build: the main menu's Dragon Net Battle entry is hidden (`BT3_MENU_ITEM4=0`) |
| Names of stages / songs added from outside the disc | the game prints them with its own font (its fallback without the overlay); the browser data has none |
| Notices (`Port_UiNotice`) | a line under the game |

**Keyboard and focus**: SDL hears the keyboard on the canvas only (`SDL_EMSCRIPTEN_KEYBOARD_ELEMENT=#canvas`; SDL's
default, the whole window, swallowed Tab and the arrows and passed the page's typing to the game). Panel: tabs with
the arrow keys, Escape closes it, the focus returns to the game; while it is open neither the keyboard nor a
controller reaches the game (`gPortOverlayOpen`). The engine quits on Escape (a desktop rule, unchanged); the page
keeps Escape from the canvas (a tab has no quit, and Escape leaves full screen), except while a binding is taken.

**Tested** (built-in browser pane and Chrome through CDP): every control changed its engine value on WebGL 2
(scale, aspect, 2D filtering, texture pack, the six effects bit by bit, glow, both volumes, clash option and worth,
key binding with K / Esc keep / Enter to rebind, reset to defaults, controller list, unlock after loading a save,
screenshot 512 x 448, renderer switch, restart with switches) and the video, effects and screenshot ones on WebGPU
(two resolution changes: 0 device errors); settings restored after a reload; F1 in the game opens the panel; Escape
closes it and the game keeps running; phone width (375 px) usable; full screen in Chrome 2560 x 1440 at 60 pictures/s,
Escape leaves it without ending the game (3 runs; in two of them Chrome did not grant full screen to its background
test window, without an error the page could report). Regression: demo battle and boot 12/12 identical on both
renderers, trace 8ad198bf58dbed33; pipekey and WGSL tests pass. Not testable here: a physical controller (START +
SELECT, button capture), the native build.

### WebGPU back end: design (2026-10-11; implemented as described, differences from the first draft marked *as built*)
Inspected: `gs_draw.h` (GsBackend), `gs_draw.c` (recording, pipeline_get, targets), `gs_gl.c` (replay, native passes,
present), `gs_gpu.c` (the Vulkan back end, the closest model), the nine shaders and their WGSL, the browser tests.

**Interface to implement** (`GsBackend`, 17 entries): `init`, `frameEnd`, `scaleChanged`, `whiteTex`, `texCreate`,
`texUpload`, `texDestroy`, `texSlotsLeft`, `targetEnsure`, `targetDrop`, `targetColor`, `targetAux`, `targetDepth`,
`pipeGet`, `targetRead`, `targetCopy`, plus `name`. New file `port/src/gs/gs_wgpu.c` (`sWebGpuBackend`), compiled into
the browser build only (the whole file is `#ifdef __EMSCRIPTEN__`, so the native builds, which compile `gs/*.c`, are
unchanged). Selection in the browser (`gs_draw.c` browser branch of `GsGpu_Init`): `BT3_GPU_API=webgpu|gl`; the page's
`?gpu=webgpu` sets it. **WebGL 2 stays the default and the reference** until WebGPU is verified.

**Dependencies**: Dawn's `webgpu.h` for Emscripten (`--use-port=emdawnwebgpu`, pinned by Emscripten 6.0.12 to Dawn
`v20260423.175430`). Waiting (adapter, device, buffer mapping) uses `wgpuInstanceWaitAny`, which the port implements for
JSPI builds (`emwgpuWaitAny` under `#if ASYNCIFY`, set for JSPI; requires the instance feature `TimedWaitAny`): the
game's thread waits as it does for files. ImGui's `imgui_impl_wgpu.cpp/.h` taken from the vendored version's tag
(ocornut/imgui v1.92.9b).

**Mapping**
| GS / back-end concept | WebGPU |
|---|---|
| render target (`gsTargets[i]`) | colour `rgba8unorm` + aux `r8unorm` (the exact GS alpha byte) + depth `depth32float`, GS_W x GS_H x SCALE, usage RENDER_ATTACHMENT + TEXTURE_BINDING (+ COPY_SRC for colour / aux: copies, read-back) |
| first draw into a target in a frame (`cleared == 0`) | render pass load op CLEAR (colour 0, aux 0, depth 0), else LOAD; store op STORE |
| texture (`texCreate`/`texUpload`) | `rgba8unorm` or BC1/2/3 (`texture-compression-bc`, if the adapter has it; else `texCreate` returns 0 as the interface says), mip levels as asked; pixels written with `wgpuQueueWriteTexture` |
| `GsDraw.sampler` (GS_SAMPLER_* flags) | 16 samplers made once: mag/min filter, address modes, mipmap filter linear + lodMaxClamp for GS_SAMPLER_MIPMAPS, else lodMaxClamp 0 |
| pipeline key | `GsPipeKey_Decode` -> one `WGPURenderPipeline` per key, cached; `GsBlendDesc` -> blend factors / operation; depth compare from `GsDepthTest` (GREATER, GREATER_EQUAL as on the GS: depth 0 cleared, larger = nearer); per-attachment write masks (aux written where alpha is) |
| blend constant (`d->blendc`) | `wgpuRenderPassEncoderSetBlendConstant` per draw when it changes |
| scissor (`d->scissor`, at SCALE) | `SetScissorRect`, intersected with the target (WebGPU rejects a rectangle outside the attachment; GL / Vulkan clip it, which is the same picture) |
| per-draw uniforms (vertex `U`, fragment `Params`) | one uniform buffer per frame, each draw's data appended at offsets aligned to the device's `minUniformBufferOffsetAlignment` (a device limit, not a constant), dynamic offsets in `SetBindGroup`, uploaded once with `WriteBuffer` before submission |
| vertices / indices | `gsVerts` (stride `sizeof(Vtx)`: float32x3, unorm8x4, float32x3), `gsVuVerts` (stride `VU_VERT_BYTES`: 3 x float32x4), `gsVuIdx` (uint32): three buffers sized like the GL ones, written once a frame |
| depth clamp (SDL GPU / GL: no depth clipping, depth clamped) | primitive `unclippedDepth` (feature `depth-clip-control`); if the adapter lacks it, said at start and listed as a difference (not hidden) |
| native 4 / 5 (alpha-byte copies) | `CopyTextureToTexture` aux -> `sAuxCopy` / `sDateCopy` between render passes |
| native 3 (depth table pass) | its own render pass: colour + aux, no depth attachment, sampling the target's depth (`unfilterable-float` + non-filtering sampler): legal in WebGPU (the GL feedback loop does not arise) |
| native 1 (outline) | *as built:* inside the target's own pass (colour + aux + depth): a fixed pipeline, colour reverse-subtract one/one with RGB written, aux not written, depth compare always without writes (= GL's depth test off), sampling the alpha-byte copy |
| native 2 (alpha key) | not recorded any more (gs_draw.h); if it ever appears the replay reports it once and does not draw it |
| native >= 100 (a picture uploaded into a display buffer) | `sFbTex` written with `WriteTexture`, drawn scaled into the target with a full-screen triangle (WebGPU has no blit) |
| present | swapchain texture of the canvas (`getPreferredCanvasFormat`), cleared black, viewport = the letterboxed rectangle (`GsDraw_PictureSize`, top-left origin like Vulkan), the shown target's display area sampled linearly |
| `targetRead` / screenshots | `CopyTextureToBuffer` (rows padded to 256 bytes), `MapAsync` + `WaitAny`; the PPM writer shared with the GL back end |

**Binding layouts** (from the generated WGSL; entry points `main`):
- group 0: unused by every shader -> an empty layout and bind group (WebGPU needs every slot of a pipeline layout set).
- group 1 (vertex programs vu0 / vu4 / vu6): binding 0 `U`, uniform, 304 bytes (`Vu0Uniform`), dynamic offset.
- group 2: gs.frag: 0 texture float, 1 sampler filtering, 2 `dateTex` texture float, 3 its sampler; outline / alphakey:
  0 aux texture, 1 sampler; dclut: 0 depth texture **unfilterable-float**, 1 sampler **non-filtering**, 2 aux, 3, 4 clut,
  5. Bind groups cached per (texture, sampler) on the texture's own record (`targetColor` / `texCreate` handles point to
  it), so a draw only switches a cached group.
- group 3 (fragment): gs.frag `Params` 64 bytes; dclut / outline / blit `Params` 16 bytes; dynamic offset. *As built:*
  one params bind group of 64 bytes serves all of them (the 16-byte ones are written into a 64-byte slot); the layout's
  minimum binding size is the largest user's. alphakey has none.
- *As built:* the frame's uniform buffer holds every `U` block first (U i at i x the aligned slot, as the draws index
  them), then one Params slot per draw that changes them and one for the present; it grows when a frame needs more
  (bind groups made again then, before the replay), and is written with one `WriteBuffer` before the submit.
- *As built:* the table pass's group 2 (target depth, alpha-byte copy, table) is made for each table pass and released
  once the pass holds it (a few a frame); every other group is cached (per texture and sampler; the outline's and the
  empty one made once).
- One extra shader source, `shaders/blit.frag` (texture sampled over a uv rectangle), for the present and the scaled
  upload: GLSL like the others, translated by the same pipeline (Vulkan uses SDL's blit, GL its own present program;
  neither changes).

**Ownership and lifetime**: every WebGPU object belongs to `gs_wgpu.c`. Textures: a record (texture, view, its cached
bind groups) freed by `texDestroy` (bind groups released first). Targets: created by `targetEnsure`, released by
`targetDrop` with their cached bind groups; `scaleChanged` releases every target and the alpha-byte copies (whose bind
groups depend on them) and remakes the copies. Pipelines: kept for the whole run (keys are few, at most 1024 as in the
other back ends). Per-frame: command encoder, the swapchain texture view, released at the end of `frameEnd`. Device
loss: the device-lost callback records the reason; the next `frameEnd` reports it once and stops drawing (the page's
log shows it); no silent recovery. Uncaptured errors (validation): logged with their message and counted (the tests
read the count).

**What does not map directly** (and how it is handled): no blit (shader passes); no combined samplers (WGSL from
SEPARATE_SAMPLERS); depth texture sampling only as unfilterable float with a non-filtering sampler; scissor must lie
inside the attachment; uniform offsets aligned to a device limit; no GL-style implicit state (all state is in the
pipeline or set per pass: blend constant, scissor, viewport, bind groups, vertex / index buffers are set again in each
render pass); no feedback loops at all (a texture used as an attachment cannot be sampled in the same pass: the
replay already separates those passes as the Vulkan back end does).

**Order of the work** (each a commit with the tests below): (1) build integration and device / surface init with
error reporting and device-loss handling, a cleared canvas; (2) shaders, layouts, pipelines; (3) buffers, textures,
samplers, targets, the replay of ordinary draws, the present; (4) the native passes and the scaled upload; (5) read-back
and screenshots (shared PPM writer); (6) the settings window (ImGui WebGPU); (7) menus and the battle compared with
WebGL 2; (8) performance. Tests at every step: both targets build, `wasm_shaders.py` + `tests/wgsl/check.js`,
`tests/pipekey`, the demo battle's 12 screenshots and fight trace for WebGL 2 (unchanged) and WebGPU (compared
with WebGL 2), browser console and device validation errors.

## 5. Log

- 2026-10-11 (1): repository identified, audit above, Emscripten installed. Blocked by the session folder's long path.
- 2026-10-11 (2): moved to `C:\project\Tenkaichi3Web`; environment verified; software-float lowering and its test;
  all game sources compile; memory model tested in four runtimes; struct layouts checked. Commits 8cd6f9e, 2593069,
  5527e58.

- 2026-10-11 (3): platform code, data tables, signature repair, link, data on demand, page; tested without game
  data. Commits e9cc31c, 2e5805c, 79be83e, 14b9b22.

- 2026-10-11 (4): the user's ISO unpacked and verified; first boot in the browser: logos and opening movie.
  Commit b101a5e.

- 2026-10-11 (5): measurement tooling, CPU investigation (two fixes in the page/glue), WebGL 0x502 found and fixed.
  Commits 08c6117, ccdd15b, 4f4787e, 6a3a16d, 1b5f38a, 02a52f3.

- 2026-10-11 (6): WGSL pipeline (naga 30.0.1 pinned), shared test driver. Commits c55c29c, 031c144.
- 2026-10-11 (7): shared renderer layer clean-up (behaviour-preserving). Commits 359653e, efb4deb, 0d760f9, 902869d.
- 2026-10-11 (8): WebGPU back end: init (bf2196f), blit shader (122dd4e), shared screenshot writer (7a251db), bench
  --chrome-flags (25a1aab), the back end itself (c6a8aca); compared with WebGL 2 and measured (above).
- 2026-10-11 (9): settings panel in the page, no ImGui in the browser build. Commits 65f1981, e36c449, 1fda3f3.
- 2026-10-12 (10): upstream 0.1.31 (`fff151e`) merged into browser-port and the browser port adapted (above).
- 2026-10-12 (11): smooth motion measured and limited (7a21686), live performance dashboard (59852b3), controllers (d9ded6b), game saves in IndexedDB (this commit).

## 6. Next milestone

**Title screen and menus with input and sound**: press START at the title (keyboard, then a gamepad), reach the main
menu, confirm audio after a click, measure the frame rate (`BT3_GS_VERBOSE=1` prints work per frame and pacing).
Then character select and a battle. Still not started: saves and settings persistence (IndexedDB), canvas resizing /
fullscreen, manifest generation, persistent cache, the speed cost of the pointer-cast emulation, WebGPU evaluation.
