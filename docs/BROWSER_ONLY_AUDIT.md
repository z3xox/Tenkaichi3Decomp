# Browser-only audit: what the browser port uses from the native PC port

Audit of 2026-10-12. Investigation only: no file was moved, deleted or changed in behaviour for it.
Related documents:
- [BROWSER_ARCHITECTURE.md](BROWSER_ARCHITECTURE.md): the layers and the services.
- [BROWSER_BUILD.md](BROWSER_BUILD.md): the toolchain, the build and the tests.
- [BROWSER_FEATURE_MATRIX.md](BROWSER_FEATURE_MATRIX.md): features and the baseline test results.
- [BROWSER_MIGRATION_PLAN.md](BROWSER_MIGRATION_PLAN.md): the staged plan.

The earlier history is in [BROWSER_PORT_STATUS.md](../BROWSER_PORT_STATUS.md).

Labels used below:
- **Fact**: checked in this audit, with the command or file given.
- **Assumption**: not checked.
- **Unknown — investigate further**: the purpose or behaviour could not be proven.

## 1. Verified state

| Item | Value | How checked |
|---|---|---|
| Branch / HEAD | `browser-port` / `31588d7` (2026-10-12), working tree clean | `git status`, `git rev-parse HEAD` |
| Upstream base | `origin/main` = `fff151e` (Tenkaichi3Decomp 0.1.31) = merge base | `git merge-base HEAD origin/main` |
| Browser commits on top | 43 | `git log fff151e..HEAD` |
| Upstream since then | `c5d2917` (v0.1.32); seen by a dry-run fetch, **not fetched, not merged** | `git fetch --dry-run` |
| Backup branch | `browser-port-pre-0.1.31` | `git branch` |
| Browser diff vs. upstream base | 55 files, +9,408 / −233 | `git diff --stat fff151e..HEAD` |
| Game code (`src/`, `include/`) changed by the browser branch | **none** | `git diff --stat fff151e..HEAD -- src include` |

**Relationship to z3xox/BT3-Decompiled (fact).** The PC port shares history with the decompilation.
- The fork point is BT3-Decompiled `697d9e0` (2026-10-06): "Decomp holds PS2 code only: PC-port guards and the PC reference vector library removed".
- The decompilation has 113 commits since the fork. The port made 348 commits from the fork to 0.1.31.
- Comparing against the decompilation's HEAD is meaningless, because its files have been renamed since. The comparison is therefore made at the fork point (`git diff 697d9e0 refs/ours/browser-port -- src include`).

## 2. Game code vs. BT3-Decompiled (the PC port's changes)

**Totals (fact).** 75 of 474 game files differ from the fork point: 71 modified and 4 added, +6,383 / −119 lines.

**Reference vector library (fact).** 4,279 of those lines are the 4 added files:
- `src/port/vu0_a.c`, `src/port/vu0_b.c`
- `include/port/vu0_a.h`, `include/port/vu0_b.h`

These are the C references for the PS2 vector unit's library. They replace the 4 decompiled attempts `src/sys/vu0_a_c.c`, `vu0_a_c_b.c`, `vu0_a_c_c.c` and `vu0_b_c.c` (`port/tools/portsrc.py`, `REPLACED`). The browser compiles them as "vu0ref" (`port/tools/wasm_build.py`, `port_object`).

**Everything else** sits inside `#ifdef PORT` blocks, or in macros that have a PORT definition and a PS2 one (`BTL_STAGE_*`, `CHARSEL_*`, `HUDG_HP_*`). By kind:

| Kind | Examples (file: hook) | Why | Browser uses it? | Fix / adaptation / service | Action | Test |
|---|---|---|---|---|---|---|
| C/ABI correctness | `eft_i.c`, `eft_a.c`: by-value vs. by-address structs; `gfxm_a.c`: no float→u64 conversion; `include/sys/dma.h`, `dma.c`, `movie.c`: pointers (no uncached mirror) | The PS2 compiler's calling convention and memory map | **Yes**: compiled into the wasm | Fix | **Retain** | Demo trace `8ad198bf58dbed33` |
| Memory | `heap.c`: `Port_GameBigAlloc` / `Port_GameBigFree` | Large buffers outside the game heap | Yes (`plat_mem.c`) | Service | Retain | Boot and demo runs |
| Widescreen / 2D layout | `btl_cam.c`, `eft_a.c`, ...: `Port_WideFactor` (6 files); `Port_GsMarker` (8 files); `Port_HudLayout`, `Port_AspectMilli` | 16:9 picture; HUD placement | Yes (settings panel) | Adaptation | Retain | Boot and demo shots, 12/12 identical |
| Settings | `gfxm_a.c`: `Port_FxOff`; `Port_WallMesh`; `Port_ClashSquare` / `Port_ClashLog` | Effects switched off; clash input | Yes | Adaptation | Retain | Covered by the settings tests in [BROWSER_PORT_STATUS.md](../BROWSER_PORT_STATUS.md) |
| Name font | `Port_NameFontSheets` (2 files) | Text for added names | Yes (`gs/namefont.c`) | Adaptation | Retain | Boot shots |
| Added stages / songs | `menu_c_e.c`, `menu_e_b.c`, `battle_load.c`, `stg_*`: `plat_stages.h`, `plat_songs.h`, `Port_StageBufSize`, `Port_TestStage`, `Port_TestScreen`, `Port_TestBgm`, `Port_SongDebug` | Content added from outside the disc | **Compiled, but the feature is unavailable**: no directory listing in the browser (section 5) | Adaptation | Retain; browser listing in plan task M3 | None yet |
| Online play | `hud_b.c`, `hud_a.c`, `btl_cam.c`, `battle_load.c`, `menu_a.c`: `Port_NetActive`, `Port_NetMe`, `Port_NetSession`, `Port_NetView`, `Port_NetSideOptions`, `Port_NetLeave`, `Port_NetMenuOpen`, `Port_NetMenuListed`, `Port_HudSwap` | Two-player online | **No**: the browser links `port/src/web/net_off.c`, the interface in the state "no match running". The menu item is hidden (`index.html` sets `BT3_MENU_ITEM4=0`). | Service (native) | Retain the hooks (shared interface); the browser keeps `net_off.c` | Demo and boot runs (the hooks return "no match") |

**Defects found in the game layer (fact; not changed):**
- `HUD_SCR(side)` is defined as `((side) ^ Port_HudSwap())` only under `#ifdef PORT`, but it is used unguarded in `src/battle/hud_a_b.c`, `hud_b.c`, `hud_c_b.c` and `hud_e_b.c`. So the game code no longer builds without `PORT`. This affects the PS2/decompilation build only, not the browser. Plan task M8 (report it upstream).
- `src/sys/vu0_b_c.c`: the changes are comments only (and the file is replaced in both builds).

**Conclusion (fact).** The browser needs the game code exactly as the PC port has it, including all `PORT` blocks. None of them is native-only at the source level; the native-only part is the *implementation* behind some hooks (online play), which the browser already replaces.

## 3. Source and dependency inventory

Classified into 9 categories. "Browser" means the file is compiled, run or read by `port/tools/wasm_build.py` or the page.

| # | Category | Paths | Browser | Notes |
|---|---|---|---|---|
| 1 | Decompiled game code | `src/main.c`, `src/sys/*.c`, `src/battle/*.c`, `src/menu/*.c`, `include/**` | Yes: 269 files (`portsrc.sources()`) | Unchanged by the browser branch |
| 2 | Game-code replacements | `src/port/vu0_a.c`, `vu0_b.c`, `include/port/*` | Yes (vu0ref) | PC port's reference library |
| 3 | Compatibility layer | `port/include/port_compat.h` (forced include: `PORT 1`, types, empty `INCLUDE_ASM`, `malloc`→`Port_Malloc`), `port_libm.h`, `plat_stages.h`, `plat_songs.h`; `port/third_party/newlib_libm` (with `newlib_shim.h`); `port/src/softfloat_ps2.c`, `mathf_pc.c`, `plat_libm.c`, `plat_fastvec.c`; `port/data/*.s` (shapes only) | Yes | Shared with native |
| 4 | Platform services | `port/src/plat_*.c`, `headless.c`, `gs_marker.c` | Yes, all 22 compiled | Shared. Some have `__EMSCRIPTEN__` branches: `plat_mem.c`, `plat_stub.c`, `plat_crash.c` |
| 5 | Engine (GS emulation and renderers) | `port/src/gs/gs_core.c`, `gs_draw.c`, `gs_pipekey.c`, `gs_vu1.c`, `gs_sony.c`, `gs_texpack.c`, `gs_input.c`, `namefont.c`, `snd_adx.c`, `snd_se.c`, `state.c`; `gs_gl.c` (WebGL 2), `gs_wgpu.c` (WebGPU, browser-only); `shaders/` | Yes | `gs_gpu.c` (Vulkan) is excluded |
| 6 | Browser-only code | `port/src/web/*.c` (`net_off`, `web_fs`, `web_gl`, `web_plat`, `web_ui`); `port/web/*` (page, `bt3_data.js`, `settings.js`, `dashboard.js`, `controllers.js`, `saves.js`, `devserver.py`); `port/tools/wasm_*.py` | Yes | Added by the browser branch |
| 7 | Native-only | `port/src/gs/gs_gpu.c`, `net.c`, `net_match.c`, `net_relay.c`, `ui.cpp`; `port/third_party/imgui`; `port/setup/`, `port/release/`, `port/matchmaking/`; `.github/workflows/release.yml`; `port/run.sh`; `port/tools/undefined.py`, `link.py`, `package.py`, `make_dat.py`, `strip_data.py` | **No** | Excluded by `WASM_SKIP` or simply not referenced |
| 8 | Data preparation / dev tools | `port/tools/extract_disc.py` (the user's disc → `gamedata/`); `add_stage.py`, `add_song.py`, `mc_extract.py`, `gen_data.py`, `make_verify.py`, comparison and probe tools | Only `extract_disc.py`, run once by the user | Shared |
| 9 | Decompilation tooling | `config/` (159 splat/linker files), `scripts/`, `docs/` (decomp guides) | **No** | Kept for the reference repository |

**Shared build tools the browser imports (fact: `port/tools/wasm_build.py` line 21 and `compile_soft`):**
- `portsrc.py`: the source list and `prepare()`, which turns the decompilation's `#if 0` C attempts on.
- `eeconst.py`: the PS2 compiler's float constants.
- `ptr32.py`: only `r2l`, the right-to-left argument order the game depends on. It needs Python `libclang` 18.1.1.
- (`toolchain.py` is **not** used: the audit first listed it here; `wasm_build.py` does not import it. M9 reverted its `wasm` entry.)

The browser does not use `ptr32`'s pointer narrowing.

### 3.1 Concise dependency table

| Dependency | Version (pinned / found) | Class | Used by | Licence |
|---|---|---|---|---|
| Emscripten (emcc, clang 24.0.0git) | 6.0.12 | build-time only | all compiles, link | MIT / UIUC |
| SDL3 (Emscripten port) | 3.4.2 | runtime | window, input, audio (section 4) | zlib |
| emdawnwebgpu (Dawn's `webgpu.h`) | v20260423.175430 | runtime | `gs_wgpu.c` | BSD-3-Clause |
| naga (WGSL from GLSL) | 30.0.1 (Rust 1.99.0 installed; default stable 1.93.1) | build-time only | `wasm_shaders.py` | MIT / Apache-2.0 |
| Python | 3.12.10 (tools venv) | build-time only | `port/tools/*` | PSF |
| libclang (Python) | 18.1.1 | build-time only | `ptr32.r2l` | Apache-2.0 with LLVM exception |
| newlib libm (`port/third_party/newlib_libm`) | vendored | runtime | game maths with the PS2's float model | `COPYING.NEWLIB` (BSD-style; per-file) |
| xxHash (`port/third_party/xxhash`) | vendored | runtime | `gs_texpack.c`, `state.c` | BSD-2-Clause (`LICENSE`) |
| Node.js | v24.19.0 | optional dev/test | tests, CDP driver | MIT |
| Chrome / Chromium | 154.0.8037.98 | optional dev/test | browser tests | — |
| 7-Zip (`7z`) | **unknown — not checked** | game-data preparation only | `extract_disc.py` | LGPL (unrar part: restricted) |
| Dear ImGui (`port/third_party/imgui`) | vendored | **native-only removable** from the browser's view (never compiled for it) | `gs/ui.cpp` | MIT |
| Vulkan, sockets, pthreads in `net_*` | — | **native-only removable** | `gs_gpu.c`, `net*.c` | — |

The project's own licence is MIT (`LICENSE`). **The game's data is not a dependency of the build:** the user supplies it at run time from their own disc (section 6).

## 4. What the browser needs: services

The table below shows the service each part of the program needs and what provides it.

| Service | Game-side entry | Provider in the browser | Native provider (for comparison) | State |
|---|---|---|---|---|
| Rendering | GS packets → `gs_core.c` → `gs_draw.c` | `gs_gl.c` (WebGL 2, default) and `gs_wgpu.c` (`?gpu=webgpu`). The context comes from `web_gl.c`, the window from SDL (`SDL_CreateWindowWithProperties` in `gs_draw.c`) | `gs_gl.c` / `gs_gpu.c` (Vulkan) | Works; both renderers give the same trace |
| Input | `Port_PadRead` (wrapped) | `gs_input.c` (SDL gamepads and keyboard), then `web_ui.c` (`__wrap_Port_PadRead`, bindings) and `controllers.js` | `gs_input.c` + `ui.cpp` | Works (simulated pads) |
| Audio | `snd_adx.c`, `snd_se.c` | SDL audio streams (Emscripten port, Web Audio) | SDL audio | Works. **Assumption**: no automated audio test |
| Timing / vblank | `plat_stub.c` (vblank pacing, game speed) | JSPI suspends the game's wait; `SDL_DelayPrecise`; `state.c` `SDL_GetTicksNS` | the same code on a thread | Works |
| File system | `fopen` (wrapped), `plat_file.c`, `plat_fcache.c` | `web_fs.c` `__wrap_fopen` → `bt3_data.js` (HTTP Range, LRU cache) and MEMFS | the host file system | Works. Folders under the data root are listed from the server's listings since M3 (`web_fs.c`: `stat`, `opendir`, `readdir`, `closedir` wrapped; `devserver.py`: `?list`) |
| Saves | memory card `plat_mc.c` | MEMFS, plus `saves.js` (IndexedDB) through the wrapped card calls `func_002A1FB8`, `func_002A2320`, `func_002A2498` in `web_ui.c` | host files | Works |
| Config | `plat_settings.c` | `settings.js` (localStorage), then `web_ui.c` | settings file + ImGui | Works |
| Logging / crash | `plat_crash.c` | the `__EMSCRIPTEN__` branch: the page prints the JS stack, plus `Port_CrashContext` | signal / SEH handlers | Works |
| Threads | `plat_mem.c` (game thread), `plat_verify.c`, `gs_core.c` (`BT3_GS_THREAD`) | none: single thread. `plat_mem.c` has a browser branch | pthreads | `plat_verify.c`'s thread is not started; this is reported since M1 (section 5) |
| Networking | `Port_Net*` | `net_off.c` (not available; menu hidden) | `net*.c` | Excluded on purpose |
| Movies | `plat_movie.c`, `plat_mpeg2.c` | the same code (software MPEG-2) | the same | Works in the boot run (the intro movie) |
| Game data verification | `plat_verify.c` | **none in practice** (thread fails) | a thread | **Not run, and said** (M1: log, crash report, dashboard "Data check") |

## 5. Native PC dependencies and browser defects

### 5.1 Already excluded (fact)

The following are already excluded from the browser build, with nothing left to remove from it:
- `WASM_SKIP = {net.c, net_match.c, net_relay.c, gs_gpu.c}`;
- `ui.cpp` and ImGui, which are never globbed: `port_jobs` takes only `*.c`;
- no C++ at all.

| Native-only item | Files | Browser users | Shared interface | Browser replacement | Test |
|---|---|---|---|---|---|
| Online play | `gs/net.c`, `net_match.c`, `net_relay.c` | none | `Port_Net*` | `web/net_off.c` | Demo trace; `net_off` aborts with a message if the one match-only function is ever reached |
| Vulkan renderer | `gs/gs_gpu.c` | none | `gs_draw.h` back-end table | `gs_gl.c`, `gs_wgpu.c` | Demo shots on both renderers |
| ImGui settings window | `gs/ui.cpp`, `third_party/imgui` | none | `gs/ui.h` | `web/web_ui.c` + `port/web/settings.js` | dashboard, controllers and saves tests |
| Installer / release / CI | `port/setup`, `port/release`, `.github/workflows/release.yml` | none | none | `port/web/devserver.py` (local only) | — |
| Native link tools | `undefined.py`, `link.py`, `package.py`, `make_dat.py`, `strip_data.py` | none | — | `wasm_build.py`, `wasm_data.py` | Clean-clone build |

### 5.2 Native assumptions still inside shared code the browser runs (fact)

1. **`port/src/plat_verify.c:264`** starts a detached `pthread_create` thread. The browser build has no threads, so `pthread_create` fails and the state becomes `DATA_OFF` with no message. **Game-data verification is silently off in the browser**, against the "no silent fallback" rule. **Done (M1, `d62fe22`).** The check is not started and that is said, with the reason. Running it anyway would have been wrong: its quick pass `stat`s every data file, which is on the HTTP server, so all 68,605 files would have counted as missing and the original data would have been called modified.
2. **Directory listing.** Two places scan the data folder, which lives on the HTTP server; `web_fs.c` wraps `fopen` only:
   - `plat_extras.c:48` (`opendir`/`readdir`), which `plat_stages.c` and `plat_songs.c` go through;
   - `gs/gs_texpack.c:105,130` (`SDL_GetPathInfo` / `SDL_EnumerateDirectory`).

   So added stages, added songs and texture packs are unavailable in the browser.
   - **This is silent.** `PortExtras_Scan` returns 0 when `opendir` fails (`plat_extras.c:52`). `TexPack_Init` returns when the root is not a directory (`gs_texpack.c:122`).
   - Natively, both mean "none installed", which is legitimate. In the browser, they hide that the feature cannot work there.
   - Plan tasks M2 and M3. **Done:**
     - **M2 (`dbc4ba1`):** reported.
     - **M3a (`9f2f3ec`):** the data folder is looked through over HTTP, so stages and songs are found.
     - **M3b (`d4ee2ff`):** texture packs are found and used, seen in screenshots on both renderers.
     - Without a server that lists folders, all three are reported as unavailable, with the reason.
     - A battle on an added stage: seen with the game's own start (6a). Not seen: an added song played.
   - `plat_mc.c:214` also uses `opendir`, but on the memory card in MEMFS, which supports it (the saves test passes).
   - `mods/` overrides are looked up file by file through `fopen` (`plat_file.c:126`, `plat_fcache.c:52`), so they **probably** work over HTTP. Not tested.
3. **`gs_core.c:1255`**: `pthread_create` with no check, only when `BT3_GS_THREAD` is set *and* the mode is `"gpu"` (Vulkan). This path is not reachable in the browser (**assumption**: `gs_mode()` is never `"gpu"` there; not traced). It is a pre-existing native defect.
4. **Link stubs.** The link generates 103 stubs that abort with `bt3: not implemented yet: <name>`:
   - 102 are referenced only from data tables (`dvCi*`, `htCi*`, `mfCi*`, `func_0027B…`–`func_002BC748`);
   - one is `sceCdSearchFile` (`src/sys/file.c:194`, reached only for an unknown disc type).

   None was reached in any run. Upstream's native `link.py` does the same. These fail clearly; keep them.
5. **`plat_mem.c`, `plat_stub.c`, `plat_crash.c`, `gs_core.c`, `gs_draw.c`, `gs_gl.c`, `ui.cpp`/`ui.h`** were changed by the browser branch (`git diff --stat fff151e..HEAD -- port/src`). This is shared native code. **At the audit, the native build could not be rebuilt** (no gcc, clang or mingw on this machine). **Since then: M0 done**, using upstream's release container with Docker. The clang64 Linux and win64 Windows builds of `d4ee2ff` behave like upstream `fff151e`: identical headless traces, and byte-identical OpenGL and Vulkan screenshots. Details: BROWSER_MIGRATION_PLAN.md, M0.

### 5.3 Inconsistencies (fact)

- `port/web/bt3_data.js` named `port/web/make_manifest.py`, **which does not exist**. Sizes therefore always come from one HEAD request per file. That fallback is stated in the code, not silent. The comment now says that no tool makes the manifest yet (M4).
- `wasm_build.py`'s usage text said the port mode compiles "ImGui". Corrected (M4).
  - `PORT_OBJ_PREFIXES` lists `cxx`. This is **not** stale: it removes C++ objects left over from older builds before the link. A comment now says so (M4).
- `plat_stub.c`'s header says "Headless stand-ins", and `headless.c` is named after its history. Both now hold real runtime code (pacing, pads, menus).
  - These are **upstream files**, so they are left as they are: changing a comment there would only add merge conflicts.
- **Build reproducibility.** `shaders.h` embeds the shader text with its line endings. With `core.autocrlf=true`, a fresh clone has CRLF in the 4 shaders whose working copies here are LF (`alphakey`, `blit`, `dclut`, `outline` `.frag`). The result is a different `bt3.wasm` with identical output. **Done (M5, `49a0419`)**: shaders are embedded with LF, and a fresh CRLF clone now builds a byte-identical `bt3.wasm` and `bt3.js`.
- The `port/tests/wasm_mem/build.py` verdict reads FAIL because it scores the rejected layout A as well (224 MB). Layout B passes 17/17 (1,472 MB). This is a test-design flaw, not a regression. **Done (M6, `734a81e`)**: the verdict and exit status are layout B's; before the fix, the test always exited 0.

## 6. Data and assets

- **Game data.** The user unpacks their own disc with `port/tools/extract_disc.py` (needs `7z`). The page reads it on demand from a server the user runs on 127.0.0.1 (`devserver.py --mount gamedata=...`), using HTTP Range and an LRU block cache (`bt3_data.js`).
- **Data tables.** The game's data tables are not in the build: `port/data` holds only their shapes. At start, the values are copied from the user's `SLUS_216.78` and `DBZP.BIN` through the run list (`wasm_data.py` → `data_list.c`).
- **Nothing copyrighted is committed.** `port/build/` and `gamedata/` are outside version control (fact: `git ls-files`).

## 6a. Found during the approved tasks (2026-10-12)

- **`BT3_TEST_STAGE=0x24` stopped the game in the `BT3_DEMO` attract demo** at the battle's load. **Resolved (2026-10-13): the cause is the `BT3_DEMO` shortcut, not the browser.**
  - **Call stack**, taken with Chrome's debugger: `Battle_Load` → `Load_RunBlocking` → `BtlLoad_StepInitial` → `File_Request` → `File_OpenById` → `ADXF_OpenAfs` → `Port_FileSize` → `fopen`. It repeats for the path `/70000.bin`, about 91,000 times in 12 s.
  - **Why the folder name is empty:** an added stage's file id lies in archive partition 2, whose folder (`pzs3us2`) is set only when the game's first-run sequence loads that partition (`FirstRun_Main`, `src/menu/menu_i_c.c`: `File_LoadPartitionNw(2)`). The `BT3_DEMO` shortcut (`headless.c`) starts the battle without that sequence.
  - **Effect:** the path has no folder, the added-stage alias (`pzs3us2/70000.bin`) never matches, and the loader retries forever. In the browser the page freezes too, because the missing file's size is cached and each retry finishes within the same JavaScript turn.
  - **Natively:** the same code, so the same shortcut; not run natively.
  - **With the game's own start**, as in normal play, the battle on the added stage loads and runs. Checked by `port/tests/web/extras.js`: the title's attract battle, put on the added stage by `BT3_TEST_STAGE`, reads the stage file (frame ~2450) and the game runs on; screenshots show the battle on that stage.
  - **Candidate upstream report:** the `BT3_DEMO` / `BT3_REPLAY` shortcut could load partition 2. Not done; it is upstream's test tool.
- **The crash report's context named a stale stage**: during a running game, "stage: memory ready, before the program's main". **Resolved (2026-10-13).**
  - **Cause:** `gPortStage` (`plat_mem.c`) is updated to "the game is running" only by the Windows start (its `__wrap_main`). The browser's main is not wrapped, and neither is the Linux one, so it stays at the last start-up step.
  - **Fix, browser-only:** `web/web_crash.c` says "the game is running (vertical blank N)" once the game has counted a vertical blank (`gPortVBlanks`, `plat_stub.c`). Before that, it gives `gPortStage` as it is.
  - **Test:** `web/verify.js` checks the whole line. The native Linux build still shows the stale stage, an upstream matter.
- **`emscripten_set_main_loop_timing: Cannot set timing mode…`** is printed once at the start of every run, before M2 as well. It is harmless so far. **Unknown** which call makes it.
- **SDL's file functions.** The Emscripten SDL build defines `HAVE_FOPEN64`, but `libSDL3.a` references plain `fopen` (`llvm-nm`), so the existing `fopen` wrap covers texture packs. No `fopen64` wrap is needed.

## 7. Unknowns

Each is marked **unknown — investigate further**:
1. ~~Whether the native builds still build and behave the same~~ The release configurations, clang64 and win64, are checked (M0). The 32-bit configurations (`BT3_CC=gcc`, `clang`), native rendering on a real GPU, the ImGui window and audio remain **unknown — investigate further**.
2. ~~Whether the directory-listing features report their absence~~ Resolved: they are silent (5.2 item 2).
3. The `7z` version and licence terms as installed on the user's machine.
4. The content of upstream 0.1.32 (`c5d2917`) and its effect on the browser port. It was not fetched, by instruction.
5. Audio correctness. No automated check exists; it was heard by hand earlier (see the status document).
6. The reason for each `#ifdef PORT` block in the effects files (`eft_*.c`) was sampled, not read in full. The samples are all ABI fixes and widescreen. The rest is **assumed** to be of the same kinds.
