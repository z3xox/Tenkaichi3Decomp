# Browser port: staged migration plan

This is a proposal, awaiting approval; none of it has been carried out. The findings behind it are in [BROWSER_ONLY_AUDIT.md](BROWSER_ONLY_AUDIT.md), cited below as "audit §". The test commands are in [BROWSER_BUILD.md](BROWSER_BUILD.md) §4.

## Status (2026-10-12)

| Task | State | Commit | Tests (all run) |
|---|---|---|---|
| M4 stale comments | done | `6e40327` | build output byte-identical |
| M5 reproducible build | done | `49a0419` | fresh CRLF clone: `bt3.wasm` / `bt3.js` byte-identical; demo and boot on both renderers 12/12 identical |
| M6 memory test verdict | done | `734a81e` | B PASS, exit 0; with A scored instead, FAIL, exit 1 |
| M2 unavailable extras reported | done | `dbc4ba1` | `web/extras.js`; R |
| M1 data check not silent | done | `d62fe22` | `web/verify.js`; R |
| M3 listing over HTTP | done: stages and songs `9f2f3ec`, texture packs `d4ee2ff` | — | `web/extras.js` (17 checks); R. **Not seen:** a battle on an added stage, an added song played (audit §6a) |
| M7 CI | done; **not yet run on GitHub** (nothing pushed) | `b1a4d3b` | the workflow's steps in a plain ubuntu:24.04 container: PASS |
| M8 `HUD_SCR` | verified; draft prepared, **not published** | `894827c` (`docs/drafts/upstream_hud_scr_report.md`) | preprocessing checks (see the draft) |
| M0 native check | done (see below) | tool `1fd609a` (`port/tests/native/`) | see below |
| M9 | **done for 4 files** (toolchain.py, ui.cpp, plat_crash.c, gs_wgpu.c moved); plat_stub.c, plat_mem.c, gs_core.c and the renderer layer left, with reasons (M9 progress) | `4f15740`, `d309d46`, `377975a`, step 4 | per step, below |
| M10 upstream 0.1.32 | deferred, not fetched | — | — |

### M0: native validation

**Toolchain:** upstream's own release container, `port/release/Dockerfile`, built here as `bt3-port-release:jammy`:

| Component | Version |
|---|---|
| Base system | Ubuntu 22.04 |
| Game-code compiler (both configurations) | clang 22.1.8 (apt.llvm.org) |
| SDL3 | 3.4.12, built from source |
| Windows cross toolchain | mingw-w64 GCC 10-posix (`x86_64-w64-mingw32-gcc 10-posix 20220113`) |
| Software rendering for the rendered runs | Mesa 23.2.1: llvmpipe for OpenGL 4.5, lavapipe for Vulkan; Xvfb |

The builds use no disc data (`BT3_SKELETON=1`). The runs use the user's data, mounted read-only.

**Revisions:** upstream `fff151e` (0.1.31) against `browser-port` `d4ee2ff`, by hand. The committed script was then run against `894827c` (the same code, plus documents and CI): every comparison identical, exit 0.

| Configuration | Run | Result |
|---|---|---|
| `BT3_CC=clang64`, Linux x86-64 (as the Linux release) | build | both OK: 409 objects, 103 stubs, link OK |
| | attract demo, no window (`BT3_GS=none`, `BT3_TRACE=60`, 150 s) | traces **identical**, 70 lines |
| | rendered, OpenGL on llvmpipe (`BT3_GS=gpu BT3_GPU_API=gl`, `BT3_SHOT=150`, 420 s) | 4 screenshots (frames 0–450) **byte-identical**; 13 trace lines identical |
| | rendered, Vulkan on lavapipe (`BT3_GPU_API=vulkan`) | 15 screenshots (frames 0–2100) **byte-identical**; 70 trace lines identical |
| `BT3_CC=win64`, Windows x86-64 (as the Windows release; clang 22 `--target=x86_64-w64-mingw32`) | build (cross) | both OK |
| | run on this Windows 11 machine, no window, 150 s | traces **identical**, 70 lines, and identical to the Linux trace |

**Browser against native:** the browser's demo trace, `8ad198bf58dbed33` (67 lines), equals the first 67 lines of the native trace.

**Not covered by M0:**
- the developer default `BT3_CC=gcc` (32-bit) and `BT3_CC=clang` (32-bit);
- rendering on a real GPU natively;
- the ImGui settings window: `ui.cpp` is compiled and linked, but the window was not opened;
- audio (`SDL_AUDIODRIVER=dummy`);
- online play.

**Repeat with:**

```bash
RUN_WINDOWS=1 bash port/tests/native/compare.sh <rev A> <rev B> <gamedata folder> [work folder]
```

It needs Docker. M9 requires it after each file it moves.

### M9: progress (one shared file per commit)

Each step is committed on a temporary local branch first, checked, and only then added to `browser-port`.

**Checks per step.**
- **Browser build:** `wasm_build.py port` + `link`, and `bt3.wasm` / `bt3.js` compared with the build before M9.
- **Unit and build tests:** `wasm_sf`, `pipekey`, `web_data`, `wasm_mem`, `wgsl/check.js`.
- **Demo and boot** on WebGL 2 and WebGPU: trace and 12 screenshots each, against `audit_base_*` / `audit_boot_*`.
- **Page tests:** `dashboard` on both renderers, `controllers`, `verify`, `extras`, and `saves` where relevant.
- **Native:** `REUSE=1 RUN_WINDOWS=1 port/tests/native/compare.sh fff151e <step>`.

**Never covered** (see M0): the 32-bit GCC and Clang builds, rendering on a real GPU, opening the ImGui settings window, audio, online play, and the PS2 matching build.

| Step | Shared file | What moved where | Commit (tested as) | Browser | Native vs `fff151e` |
|---|---|---|---|---|---|
| 1 | `port/tools/toolchain.py` | The unused `BT3_CC=wasm` entry removed; the file is identical to upstream | `a3b4c94` | `bt3.wasm` / `bt3.js` **byte-identical**; every check PASS | headless 70, OpenGL 13 + 4 screenshots, Vulkan 70 + 15 screenshots, win64 70: **all identical** |
| 2 | `port/src/gs/ui.cpp` | Four dead `__EMSCRIPTEN__` branches removed (the browser build compiles no C++); the file is identical to upstream | `fd88f29` | `bt3.wasm` / `bt3.js` **byte-identical**; every check PASS | the same four comparisons: **all identical** (first attempt not run: Docker had stopped; re-run after restarting it) |
| 3 | `port/src/plat_crash.c` | Its `__EMSCRIPTEN__` branch (`Port_CrashContext`) moved unchanged to the new `port/src/web/web_crash.c`; the browser build leaves `plat_crash.c` out (`wasm_build.py`: `WASM_SKIP`, now also applied to `port/src/*.c`); the file is identical to upstream | `8115de8` | `bt3.wasm` differs (one object replaced by another), `bt3.js` identical; demo and boot traces + screenshots identical on both renderers; `verify` checks the crash context; every check PASS | Against `fff151e`: headless 70, Vulkan 70 + 15 screenshots, win64 70, all **identical**. OpenGL was identical as far as it ran: 11 lines and 3 screenshots, twice; the run stopped before frame 450. **Control** (step 2 vs step 3, same conditions): OpenGL 12 lines + 4 screenshots, Vulkan, headless all identical. Native binaries vs step 2: `.text` / `.data` identical; 20 bytes of build-id and 36 bytes after `.bss` (probably debug line numbers) differ |

| 4 | `port/src/gs/gs_wgpu.c` → `port/src/web/gs_wgpu.c` | Moved unchanged (`git mv`). `wasm_build.py`'s `WEB_RENDERERS` keeps the renderer's flags and object name; the renderer compile adds `-Iport/src/gs`. The native build no longer sees the file (it compiled to an empty object there) | `ce85c35` | `bt3.wasm` / `bt3.js` **byte-identical** to step 3's (built from a clean worktree of `377975a`); every check PASS (`web_data` printed PASS and then hit the known libuv exit assertion, exit 127) | headless 70, OpenGL 13 + 4 screenshots, Vulkan 70 + 15 screenshots, win64 70: **all identical** |

**Left unchanged, with the reason** (each would need a semantic change, or touches the renderer architecture, which M9 must not change):

| Shared file | Browser-specific part | Why it stays |
|---|---|---|
| `port/src/plat_stub.c` | `emscripten_sleep` in `vblank_wait` (static): the timed wait, plus two `emscripten_sleep(0)` yields in the "late" and "new grid" branches | The timed wait could become a `--wrap=nanosleep` with the same duration. The two yields have no call to hook: moving them to a wrapper of the caller changes when the page gets control, or adds a yield when a wait measures 0 ns. That is a pacing change. A partial move would split the logic across two places |
| `port/src/plat_mem.c` | The platform branch of the static memory helpers (`map`, `map_heap`); a branch in the static `Port_LoadGameData` that feeds the compiled-in run list | Static functions inside a platform `#if` chain. Isolating means making native functions non-static, or a different browser start-up (writing a `.dat` file into the in-memory file system and wrapping `readlink`). Both are semantic changes |
| `port/src/gs/gs_core.c` | `Web_FrameSample` (the dashboard's ring); the `BT3_FRAME_LOG` line, active natively too | The values are local variables of the frame function: no call to hook |
| `port/src/gs/gs_draw.c`, `gs_draw.h`, `gs_gl.c`, `gs_pipekey.c`, `port/src/gs/ui.h`, `port/src/gs/shaders/*.frag` | The renderer layer shared by every back end; WebGL 2 branches in `gs_gl.c`; shader switches for WebGL 2 / WGSL | Renderer architecture (out of scope for M9). Most of it is shared code used natively too, not browser-only code |

**Limitation of the native check (found in step 3):** the OpenGL run on llvmpipe stops after a fixed 420 s. How far it gets varies, 11–13 trace lines (frames 300–450), so a missing last screenshot is a run-length effect. It shows as "DIFFERENT or missing" and exit 1. A missing frame is therefore checked with a control run of the previous step under the same conditions before a step is accepted. **Resolved (2026-10-13):** the rendered runs now stop when their last screenshot is written (OpenGL: frame 450, Vulkan: frame 2100); the time is only a limit (900 s), and a run that does not get there is reported as "frame N not reached", not as a difference. `compare.sh` also says plainly when Docker is not running. Checked: `v0.1.33` vs `708f76c`, both runs to their frames (OpenGL 364-368 s, Vulkan 224-230 s), all identical.

## Principles

1. The game code (`src/`, `include/`) stays as the PC port has it. The browser needs every `PORT` block (audit §2).
2. Nothing native is deleted from the repository. "Decoupling" means two things: the browser build stops *depending* on native assumptions, and browser code leaves shared files. Native files keep working for upstream.
3. No upstream file is renamed or moved (see [BROWSER_ARCHITECTURE.md](BROWSER_ARCHITECTURE.md) §5).
4. Each task is one small commit that can be reverted.

**Standard regression set ("R").** Every task must keep these passing:
- the build: 269/269, 61/61, 73/73, link OK;
- `wasm_sf`, `pipekey`, `wgsl` and `web_data`;
- the demo run on both renderers, with trace `8ad198bf58dbed33` and screenshots 12/12 identical to `audit_base_gl` / `audit_base_wgpu`;
- the boot run on both renderers, 12/12 identical to `boot_after`;
- `dashboard` (both renderers), `controllers` and `saves`.

## Stage 0: baseline (done in this audit)

- Investigation, baseline results and these documents. Docs-only commit.

## Stage 1: correctness gaps in what the browser already runs

### M1. Game-data verification must not be silently off

| Field | Detail |
|---|---|
| Objective | The check that the data is the original disc's runs in the browser, or the page says that it did not run |
| Files | `port/src/plat_verify.c` (shared); new `port/src/web/web_verify.c` (or an `__EMSCRIPTEN__` branch), `port/web/index.html` |
| Dependency removed | `pthread_create` in the browser path (audit §5.2 item 1) |
| Replacement | Run `verify_thread`'s work synchronously at start; it reads through the same `fopen` wrap, and JSPI suspends on reads. **Or** start it in idle steps from the vblank hook. Report the result as the native build does (`Port_DataSummary`) |
| Prerequisites | Measure the cost first: the native check reads every file's size and time, about 68,611 files over HTTP |
| Risk | Medium: start-up time. If the full check is too slow, report "verification not run in the browser" visibly, rather than shortening the check |
| Tests | R; new `port/tests/web/verify.js`: the original data is reported as verified; a file changed in a copy of the data folder is reported as changed |
| Acceptance | No path leaves `DATA_OFF` without a message |
| Isolated commit | Yes |

### M2. Directory listing: decide and report

| Field | Detail |
|---|---|
| Objective | Added stages, songs and texture packs either work or are reported as unavailable. Today the absence is silent (audit §5.2 item 2) |
| Files | `port/src/plat_extras.c`, `port/src/gs/gs_texpack.c` (shared, read only); `port/src/web/web_fs.c` |
| Dependency removed | none yet; investigation first |
| Replacement | A visible "not available in the browser" message, as `net_off.c` does: a browser-only check that the data mount cannot be listed, reported once at start. It must not change the native meaning of "none installed" |
| Prerequisites | none |
| Risk | Low |
| Tests | R; a check that the message appears |
| Acceptance | No silent absence |
| Isolated commit | Yes |

### M3. Directory listing over HTTP (feature work; optional)

| Field | Detail |
|---|---|
| Objective | Added stages, songs and texture packs available in the browser |
| Files | `port/src/web/web_fs.c` (an `opendir`/`readdir` provider for the data mount); `port/web/devserver.py` (a listing endpoint or a manifest); `port/web/bt3_data.js` |
| Dependency removed | the host file system's directory scan |
| Replacement | A manifest generated next to the data. This also fills in the missing `make_manifest.py` (audit §5.3), and the sizes then need no HEAD requests |
| Prerequisites | M2 |
| Risk | Medium: SDL's `SDL_EnumerateDirectory` in `gs_texpack.c` goes through SDL's own file system code, not libc. It may need its own provider or a browser branch |
| Tests | R; a stage added with `port/tools/add_stage.py` to a *copy* of the data appears in the select screen; one texture pack loads |
| Acceptance | The native behaviour is reproduced; nothing is silent when the manifest is missing |
| Isolated commit | Two commits: manifest + `web_fs`; then texture packs |

## Stage 2: build hygiene

### M4. Stale texts

| Field | Detail |
|---|---|
| Objective | The comments say what the code does |
| Files | `port/tools/wasm_build.py` (usage text mentions ImGui; `PORT_OBJ_PREFIXES` lists `cxx`), `port/web/bt3_data.js` (`make_manifest.py`), `port/src/plat_stub.c` header |
| Dependency removed | none |
| Replacement | Corrected comments. Do not rename `headless.c`: it is upstream's file |
| Prerequisites | none (the manifest comment waits for M3, or is changed to say "not provided") |
| Risk | None |
| Tests | build byte-identical |
| Acceptance | — |
| Isolated commit | Yes |

### M5. Reproducible build

| Field | Detail |
|---|---|
| Objective | A fresh clone gives a byte-identical `bt3.wasm` |
| Files | `.gitattributes` (`port/src/gs/shaders/* text eol=lf`), or normalise line endings in `wasm_build.py`'s `shaders_header()` |
| Dependency removed | dependence on the user's `core.autocrlf` |
| Replacement | Prefer the `wasm_build.py` normalisation: `.gitattributes` would also change the native build's embedded shaders (`undefined.py`) |
| Prerequisites | none |
| Risk | Low |
| Tests | the clean-clone build is byte-identical to the working build; R |
| Acceptance | `cmp` of the two `bt3.wasm` files is equal |
| Isolated commit | Yes |

### M6. Memory test verdict

| Field | Detail |
|---|---|
| Objective | `port/tests/wasm_mem/build.py` passes when the layout in use passes |
| Files | `port/tests/wasm_mem/build.py` |
| Dependency removed | none |
| Replacement | Score layout B; report layout A as information only (it was rejected, audit §5.3) |
| Prerequisites | none |
| Risk | None |
| Tests | the test itself |
| Acceptance | PASS for B, A listed as "rejected layout" |
| Isolated commit | Yes |

### M7. CI for the browser build (without game data)

| Field | Detail |
|---|---|
| Objective | Every push builds the wasm and runs the tests that need no data |
| Files | new `.github/workflows/browser.yml` |
| Dependency removed | none |
| Replacement | Pinned emsdk 6.0.12, Python 3.12 with `libclang` 18.1.1, and naga 30.0.1. Run the build and `wasm_sf`, `pipekey`, `wgsl`, `web_data`, `wasm_mem` |
| Prerequisites | M5, M6. Ask the owner first: this adds a workflow, and it runs on GitHub's servers. No game data is ever used there |
| Risk | Low; CI minutes |
| Tests | the workflow |
| Acceptance | Green on `browser-port` |
| Isolated commit | Yes |

## Stage 3: browser code out of shared files

### M0. Native build check (prerequisite for all of stage 3)

| Field | Detail |
|---|---|
| Objective | Prove that the browser branch's changes to shared files left the native builds unchanged (audit §5.2 item 5) |
| Files | none changed. Build with `port/tools/undefined.py` / `link.py` (gcc 32-bit), run `port/run.sh demo`, and compare against an upstream 0.1.31 build |
| Prerequisites | A native toolchain (MSYS2/gcc or Linux), which is not installed on this machine. **Owner decision** |
| Risk | — |
| Tests | Native demo trace equal between `fff151e` and `browser-port` |
| Acceptance | Identical, or each difference explained |
| Isolated commit | No commit (verification only) |

### M9. Move `__EMSCRIPTEN__` branches into `port/src/web/`

| Field | Detail |
|---|---|
| Objective | Shared files carry no browser code; upstream merges become simpler |
| Files | `port/src/plat_mem.c`, `plat_stub.c`, `plat_crash.c`, `gs/gs_core.c`, `gs/gs_draw.c`, `gs/gs_gl.c` → new files under `port/src/web/` |
| Dependency removed | browser code inside native files |
| Replacement | One file at a time. Where a whole function differs, the browser version goes in `web/` and the shared file's version is left out of the browser build by name (a `WASM_SKIP`-style per-function rule is not available, so whole files only). Where the difference is a few lines inside a function, **leave it**: duplicating a large function is worse than a branch |
| Prerequisites | M0 |
| Risk | Medium: duplicated code can drift from upstream. Do only where the browser version already differs as a whole |
| Tests | R after each file; native build (M0) after each file |
| Acceptance | R identical; native identical |
| Isolated commit | One per file |

## Stage 4: upstream

### M8. Report the `HUD_SCR` defect upstream

| Field | Detail |
|---|---|
| Objective | The game code builds without `PORT` again (audit §2) |
| Files | `src/battle/hud_a_b.c`, `hud_b.c`, `hud_c_b.c`, `hud_e_b.c` (upstream's) |
| Replacement | A PS2 definition of `HUD_SCR(side)` as `(side)` under `#else`. Behaviour is unchanged on PC and in the browser |
| Prerequisites | Owner decision: an issue or PR on z3xox/Tenkaichi3Decomp is public, outward-facing content |
| Risk | Low |
| Tests | R (the browser build defines `PORT`, so its output is unchanged) |
| Isolated commit | Yes, if done here at all |

### M10. Merge upstream 0.1.32 (`c5d2917`)

A separate task, on request. It follows the procedure used for 0.1.31: merge, adapt, R, review before commit, no force-push.

## Order and what is *not* proposed

The order is: M4, M5, M6, M2, M1, M3, M7, then M0 → M9. M8 and M10 happen on request.

**Not proposed:**
- Deleting native files.
- Renaming upstream folders.
- Replacing the 103 aborting link stubs: they fail clearly.
- Removing the vu0 reference library or any `PORT` block.
- Changing the renderer architecture or the toolchain pins.
