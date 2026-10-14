# Browser port: architecture

What the browser build is made of, and what it should be made of.
- Evidence and file lists: [BROWSER_ONLY_AUDIT.md](BROWSER_ONLY_AUDIT.md).
- How to build it: [BROWSER_BUILD.md](BROWSER_BUILD.md).
- The plan: [BROWSER_MIGRATION_PLAN.md](BROWSER_MIGRATION_PLAN.md).

## 1. Target layering

```
decompiled game logic        src/, include/  (+ src/port/vu0_*: the PC port's vector library)
        │  game-facing hooks: Port_* (27 symbols), wrapped calls, forced include port_compat.h
        ▼
compatibility / runtime      port/include, port/third_party/newlib_libm, softfloat_ps2.c, mathf_pc.c, plat_*.c,
        │                    headless.c, data tables (port/data shapes + values from the user's disc)
        ▼
browser engine               port/src/gs/ (GS emulation, draw lists, pipeline keys, input, audio, state),
        │                    port/src/web/ (file system, GL context, settings/pads/saves bridge, net_off)
        ▼
renderers                    gs_gl.c → WebGL 2 (default)        gs_wgpu.c → WebGPU (?gpu=webgpu)
        ▼
page                         port/web/: index.html, bt3_data.js, settings.js, dashboard.js, controllers.js, saves.js
```

**This is the structure as it exists today (fact).** The layers are separated by convention and by the build script (`port/tools/wasm_build.py`), not by folders.

The one place where the layering leaks is **shared platform files with browser branches**. `plat_mem.c`, `plat_stub.c`, `plat_crash.c`, `gs_core.c`, `gs_draw.c` and `gs_gl.c` contain `__EMSCRIPTEN__` code next to the native code; see the audit, section 5.2.

## 2. Game-facing interface (fact)

The game reaches the platform only through four channels:
1. **`Port_*` functions** called from `#ifdef PORT` blocks. There are 27; each is mapped to its browser definition in the audit, section 2. They are defined in `plat_stub.c`, `plat_mem.c`, `plat_gs.c`, `plat_fastvec.c`, `plat_stages.c`, `plat_songs.c`, `gs_marker.c`, `headless.c`, `gs/gs_draw.c`, `gs/gs_input.c`, `gs/namefont.c` and `web/net_off.c`.
2. **Link-time wraps** (`wasm_build.py`, `LINK`):

   | Wrapped call | Wrapper (file) | Purpose |
   |---|---|---|
   | `Progress_Main` | `headless.c` | menus, demo, replay |
   | `fopen` | `web/web_fs.c` | data over HTTP |
   | `Port_PadRead` | `web/web_ui.c` | pad bindings in the page |
   | `func_002A1FB8`, `func_002A2320`, `func_002A2498` | `web/web_ui.c` | memory-card sync, write and close, for saves.js |

3. **Name substitutions** (`GAME_DEFS`):
   - `src/main.c`: `MainMenu_StartBallLoops` = `gPortOverlayLoadArea`;
   - `src/sys/heap.c`: `D_3BE71C` = `(*gPortAbs3BE71C)`.
4. **The forced include** `port/include/port_compat.h`, and the PS2 libraries' names (sce*, sif*, ...), which `plat_*.c` provides.

Anything the game calls that nobody defines becomes an aborting stub at link time (103 today). That keeps failures explicit.

## 3. Execution model

- **One thread.** The game's own loop runs on the browser's main thread.
  - Its waits suspend through **JSPI** (`-sJSPI=1`): the vertical blank in `plat_stub.c`, and file reads in `web_fs.c`.
  - `plat_mem.c` has a browser branch instead of its native game thread.
- **Floats.** The game's floats are lowered to the PS2's float model in software (`wasm_sf.py`), so results match the console bit for bit.
- **Call order.** `ptr32.r2l` keeps the game's right-to-left argument evaluation.
- **Memory layout B.** `GLOBAL_BASE` is `0x12010000` and memory can grow to `0x70000000`; the PS2 regions lie at their own addresses below `GLOBAL_BASE`.
- **Function pointers.** `-sEMULATE_FUNCTION_POINTER_CASTS` is on, and `wasm_sig.py` repairs calls whose signatures differ.

## 4. Services

The full table, with each service's provider and state, is in the audit, section 4. Two things are missing:
- **No threads** (`plat_verify.c`).
- **No directory listing** of the data folder (`plat_extras.c`, `gs_texpack.c`).

## 5a. Shared files that carry browser-specific changes

These are the files outside `port/src/web/` and `port/tools/wasm_*` that the browser branch changed. The list comes from `git diff --stat fff151e..HEAD -- port/src port/include port/tools`. Natively compiled code that runs only in the browser is guarded by `__EMSCRIPTEN__`. These files are the candidates for isolation in plan task M9, which comes only after the native check (M0).

| File | Change | Kind |
|---|---|---|
| `port/src/plat_mem.c` (+51) | No game thread in the browser; the start runs on the page's thread | `__EMSCRIPTEN__` branch |
| `port/src/plat_stub.c` (+15) | The vblank wait gives control back to the page (`emscripten_sleep`) | `__EMSCRIPTEN__` branch |
| `port/src/plat_crash.c` | ~~`Port_CrashContext`~~ **isolated (M9 step 3):** identical to upstream; the function is in `web/web_crash.c` | — |
| `port/src/gs/gs_core.c` (+22) | `BT3_FRAME_LOG` frame timing line, **active natively too**, env-gated; `Web_FrameSample` for the dashboard | shared addition + `__EMSCRIPTEN__` branch |
| `port/src/gs/gs_draw.c`, `gs_draw.h` | The renderer layer shared by every back end: pipeline keys, picture size, event polling, the smooth-motion limit | shared refactor (both builds) |
| `port/src/gs/gs_gl.c` | WebGL 2 differences (13 `__EMSCRIPTEN__` sites); the shared layer above | `__EMSCRIPTEN__` branches + shared refactor |
| `port/src/gs/gs_pipekey.c` (new) | Pipeline key encode/decode, pure functions; compiled natively too | shared addition |
| ~~`port/src/gs/gs_wgpu.c`~~ | **moved (M9 step 4)** to `port/src/web/gs_wgpu.c` | browser-only |
| `port/src/gs/shaders/*.frag` (5) | WebGL 2 / WGSL needs (`SEPARATE_SAMPLERS`, `NO_DEPTH_QUALIFIER` switches) | shared sources with browser switches |
| `port/src/gs/ui.cpp` | **isolated (M9 step 2):** identical to upstream (its browser branches were dead code) | — |
| `port/src/gs/ui.h` | Declarations of the shared renderer layer, used by `gs_draw.c`, `gs_gl.c` and `web/web_ui.c` | shared interface |
| `port/tools/toolchain.py` | **isolated (M9 step 1):** identical to upstream (the entry was unused) | — |

The browser work since this audit (M1–M3) added **no** changes to shared files. It uses link-time wraps and browser-only files (`web_extras.c`, `web_verify.c`, `web_fs.c`).

## 5. Proposed source structure (proposal only; nothing has been moved)

| Proposed folder | Would hold | Today |
|---|---|---|
| `game/` | decompiled code | `src/`, `include/` (**do not move**: upstream merges and the BT3-Decompiled comparison depend on these paths) |
| `runtime/` | compatibility layer, platform services | `port/include`, `port/src/plat_*.c`, `headless.c`, `softfloat_ps2.c`, `mathf_pc.c`, `port/third_party/newlib_libm`, `port/data` |
| `engine/` | GS emulation, input, audio | `port/src/gs/*.c` minus the renderers |
| `renderers/webgl2`, `renderers/webgpu` | the two back ends | `port/src/gs/gs_gl.c`, `port/src/web/gs_wgpu.c`, `port/src/web/web_gl.c` |
| `shaders/` | GLSL sources, generated WGSL | `port/src/gs/shaders/` (WGSL generated into `port/build/gen_wasm`) |
| `web/` | page and browser glue | `port/web/`, `port/src/web/` |
| `tools/` | build tools | `port/tools/wasm_*.py`, plus the shared `portsrc.py`, `eeconst.py`, `ptr32.py`, `toolchain.py` |
| `tests/` | tests | `port/tests/{web,bench,lib,wgsl,web_data,wasm_sf,wasm_mem,pipekey}` |
| `docs/` | documentation | `docs/BROWSER_*.md`, `BROWSER_PORT_STATUS.md` |

**Recommendation: keep the present layout.**
- Every file under `port/` and `src/` is shared with upstream z3xox/Tenkaichi3Decomp (0.1.32 is already out). Moving them would turn every upstream merge into a rename conflict, which costs more than the folder names gain.
- The layering is already enforced by `wasm_build.py`.
- The one structural change worth making is to **move browser branches out of shared files into browser-only files** under `port/src/web/` (plan stage 3), which needs no renames of upstream files.
