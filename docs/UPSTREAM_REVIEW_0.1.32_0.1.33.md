# Upstream review: Tenkaichi3Decomp 0.1.32 and 0.1.33 (for the browser port)

This review was made on 2026-10-12 before either release was merged into `browser-port`. Each release is reviewed on its own.
- Upstream was fetched read-only: `git fetch origin --tags`.
- The native comparison baseline stayed at 0.1.31 (`fff151e`) until the review was complete.

Related documents: [BROWSER_MIGRATION_PLAN.md](BROWSER_MIGRATION_PLAN.md) and [BROWSER_ARCHITECTURE.md](BROWSER_ARCHITECTURE.md).

| | 0.1.32 | 0.1.33 |
|---|---|---|
| Tag → commit | `v0.1.32` → `c5d2917` (lightweight tag) | `v0.1.33` → `420e055` (lightweight tag) |
| Release commit | `PORT_RELEASE` 131 → 132 only | `PORT_RELEASE` 132 → 133 only |
| Range | `fff151e..v0.1.32`: 19 commits by z3xox, Renzo, victorgonl and Mikay (PRs #49, #50, #52) | `v0.1.32..v0.1.33`: 8 commits by z3xox |
| Size | 19 files, +321 / −17 | 13 files, +456 / −34 |
| Predicted conflicts with `browser-port` (`git merge-tree`) | `port/src/gs/gs_gl.c` | `port/src/gs/gs_draw.c`, `port/src/gs/gs_gl.c` |

## 0.1.32

| Change | Files | Reaches the browser build? | Effect on the browser port |
|---|---|---|---|
| **`HUD_SCR` on the PS2 side** (PR #52, Mikay, `ae803c6`): `#else #define HUD_SCR(side) (side)` in the four HUD files; stage-name UV order in `CharSel_Draw` on the PS2 side | `src/battle/hud_a_b.c`, `hud_b.c`, `hud_c_b.c`, `hud_e_b.c`, `src/menu/menu_c_e.c` | The `PORT` side is unchanged | None. **This is the fix the M8 draft proposed** (with parentheses); upstream reports that the matching build links and matches again. The draft (`docs/drafts/upstream_hud_scr_report.md`) is superseded and must not be published |
| Skip logos / skip intro (PR #50), skip copyright page | `src/menu/menu_i_c.c`, `src/menu/menu_a_c.c`; native UI in `ui.cpp` | Yes (`Port_Setting("skip_logos" / "skip_intro" / "skip_copyright", 0)`) | Off by default: the boot is unchanged. **To offer them in the browser, the page's settings panel needs entries** (follow-up) |
| Loading screens keep their proportions and place beyond 4:3 (PR #49) | `src/sys/loading.c` (`Port_GsMarker(0x17)`, `Load_ItemDrawX`), `gs_draw.c` (anchor 7) | Yes | Only in a picture wider than 4:3 |
| Menus in 4:3 (an option) | `plat_stub.c` (`Port_Menus43`, `Port_PageAspectMilli`), `gs_draw.c`, `gs_gl.c`, `gs_gpu.c`; native UI | Yes | Off by default. **The `gs_gl.c` conflict is here**: upstream changed the picture's aspect inline, where the browser branch had moved that calculation into the shared `GsDraw_PictureSize` (used by WebGL 2 and WebGPU). Resolution: `GsDraw_PictureSize` uses `Port_PageAspectMilli`, so both browser back ends behave alike. A page entry is needed to switch it (follow-up) |
| Disc Fusion: both earlier discs count as recognised | `src/menu/menu_o_b.c` (inside `PORT`) | Yes | Changes game state for every PC build: Disc Fusion content becomes available |
| Side bars "Border" (`port/assets/bars/*.png`, the owner's art, embedded by `undefined.py`) | `ui.cpp`, `undefined.py`, `port/assets/bars` | No: drawn by the ImGui window | Native only. Not offered in the browser |
| Docs, release number | `docs/port/README.md`, `port_release.h` | `port_release.h` yes | None |

## 0.1.33

| Change | Files | Reaches the browser build? | Effect on the browser port |
|---|---|---|---|
| **Update check**: the newest release is asked of GitHub once per start (a thread), with a notice and a button | `gs/net_match.c` / `.h`, `ui.cpp`, `port_release.h` (`PORT_UPDATE_REPO`) | **No**: `net_match.c` and `ui.cpp` are not built for the browser, and nothing built for it calls `Match_UpdateAsk` / `Match_UpdateLatest` (checked with `git grep` at `v0.1.33`) | The browser build does not contact GitHub |
| Performance: pipeline lookups remembered by key (a 4096-entry cache in `pipeline_get`) | `gs_draw.c` | Yes | **`gs_draw.c` conflict**: the browser branch builds the key with `GsPipeKey_Encode` (the shared pipeline-key refactor). Resolution: keep the refactor and put the cache around `sBackend->pipeGet(...)`. Safe for both browser back ends: `gs_gl.c` `pipe_get` and `web/gs_wgpu.c` `wg_pipe_get` only ever add pipelines, and the back end does not change during a run. When a table is full, both return pipeline 0 (WebGPU says so once); the cache keeps that answer, which is the same result as before |
| Performance: the HUD's alpha (DATE) copies cover only what the following draws read | `gs_draw.c` (`GsDraw_DateRegion`), `gs_draw.h`, `gs_gl.c`, `gs_gpu.c` | WebGL 2: yes, merges cleanly. WebGPU: no | The WebGPU back end keeps copying the whole buffer: correct, without the gain. A later change could do the same there |
| Settings: a Misc tab; the skip options move there | `ui.cpp` | No | Native only |
| "Random" also draws added stages and songs | `src/menu/menu_c_e.c`, `menu_e_b.c` (inside `PORT`) | Yes | Applies in the browser too (added content works there since M3) |
| The character reference's model viewer shown wide | `src/menu/menu_h_b.c`, `headless.c` (`Port_MenuScene3D`) | Yes | Only wider than 4:3 |

## Merge plan

The plan is one release at a time, each on a temporary branch, with conflicts resolved as described above. Each release is tested before it is added to `browser-port`.

**Tests:**
- the browser build;
- the unit tests;
- demo and boot on both renderers (trace and screenshots);
- the page tests, including saves;
- the native check, `port/tests/native/compare.sh`, **against the upstream tag being merged**: that tag becomes the native reference for that step.

**Follow-ups, not part of the merges:**
- page settings for skip logos / intro / copyright and menus in 4:3;
- the smaller DATE copies in the WebGPU back end.

## Results

### 0.1.32: merged (tested as `e173fca`)

**Conflict:**
- One, `gs_gl.c`, resolved as planned: `GsDraw_PictureSize` uses `Port_PageAspectMilli`.
- Its only callers are the GL and WebGPU present steps.

**Browser:**
- The build is OK: 269 game files, 63 port files, 103 stubs.
- Unit tests PASS.
- Demo and boot on WebGL 2 and WebGPU: trace `8ad198bf58dbed33`, 12/12 screenshots identical to the 0.1.31 baseline. The new options are off by default.
- Page tests PASS: dashboard (both renderers), controllers, verify, extras, saves.

**Native, against `v0.1.32` itself** (the release container, `compare.sh`), all identical:
- headless: 70 lines;
- OpenGL: 12 lines + 4 screenshots;
- Vulkan: 70 lines + 15 screenshots;
- win64: 70 lines.

The first attempt did not run because Docker Desktop had stopped while idle; it was re-run.

### 0.1.33: merged (tested as `2816f75`)

**Conflict:**
- One, `gs_draw.c` `pipeline_get`, resolved as planned: the shared `GsPipeKey_Encode` is kept, with upstream's cache around `pipeGet`.
- `gs_gl.c` merged cleanly once 0.1.32's resolution was in place.

**Browser:**
- The build is OK: 269 game files, 63 port files, 103 stubs.
- Unit tests PASS.
- Demo and boot on WebGL 2 and WebGPU: trace `8ad198bf58dbed33`, 12/12 screenshots identical to the 0.1.31 baseline.
- Page tests PASS: dashboard (both renderers), controllers, verify, extras, saves.

**Native, against `v0.1.33`**, all identical:
- headless: 70 lines;
- OpenGL: 12 lines + 4 screenshots;
- Vulkan: 70 lines + 15 screenshots;
- win64: 70 lines.

**The native reference is now `v0.1.33`**, in place of 0.1.31. For later changes, run:

```bash
REUSE=1 RUN_WINDOWS=1 bash port/tests/native/compare.sh v0.1.33 <rev> <gamedata> <work folder>
```
