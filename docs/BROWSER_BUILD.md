# Browser port: build and test

How to build and check the browser version. For what the build is made of, see [BROWSER_ARCHITECTURE.md](BROWSER_ARCHITECTURE.md). For the dependency licences, see [BROWSER_ONLY_AUDIT.md](BROWSER_ONLY_AUDIT.md) §3.1. For the results, see [BROWSER_FEATURE_MATRIX.md](BROWSER_FEATURE_MATRIX.md).

## 1. Toolchain (versions found 2026-10-12; do not update without a demonstrated need)

| Tool | Version | Notes |
|---|---|---|
| Emscripten | 6.0.12 (clang 24.0.0git) | emsdk: `emsdk install 6.0.12`, `emsdk activate 6.0.12`; on the PATH with its `upstream/bin` (llvm-objdump, llvm-nm) |
| SDL3 port | 3.4.2 | fetched by Emscripten (`-sUSE_SDL=3`) |
| emdawnwebgpu | v20260423.175430 | `--use-port=emdawnwebgpu` |
| Python | 3.12.10 | with `pip install libclang==18.1.1` (for `ptr32.r2l`) |
| naga CLI | 30.0.1 | `cargo +1.99.0 install naga-cli --version 30.0.1 --locked`; found on the PATH, in Cargo's `bin`, or named by `BT3_NAGA` |
| Node.js | 24.19.0 | tests |
| Chrome | 154.0.8037.98 | tests (headless via CDP) |

Python must load libclang from a short path. A very long working-directory path causes WinError 206; see [BROWSER_PORT_STATUS.md](../BROWSER_PORT_STATUS.md) §3.

## 2. Build

Run from the repository root (on Windows in Git Bash), with Emscripten's environment loaded (`source <emsdk>/emsdk_env.sh`). On the development machine one script does that and the rest (`env.sh`, outside the repository).

```bash
python port/tools/wasm_build.py game
```

```bash
python port/tools/wasm_build.py port
```

```bash
python port/tools/wasm_build.py data
```

```bash
python port/tools/wasm_build.py link
```

| Step | Does | Output |
|---|---|---|
| `game` | The 269 game sources: `portsrc.prepare` → `eeconst` → `ptr32.r2l` → clang IR → `wasm_sf.py` → object | `port/build/obj_wasm` |
| `port` | 61 port files: newlib libm, the vu0 references, `port/src/*.c`, `gs/*.c` (minus `WASM_SKIP`), `web/*.c`; also `shaders.h` | `port/build/obj_wasm` |
| `data` | `wasm_data.py`: 73 data-table shapes and their disc-run list | `port/build/obj_wasm_data` |
| `link` | Stubs for undefined symbols, `wasm_sig.py` repairs, WGSL by `wasm_shaders.py` (naga), the `wasm-ld` link, the page files copied | `port/build/web/` (`bt3.js`, `bt3.wasm`, page) |

Environment options:
- `BT3_WASM_VARIANT=<name>`: a separate set of object and output folders, e.g. `clean`.
- `BT3_WASM_WEB=<dir>`: the output folder.
- `BT3_WASM_HARDFLOAT=1` and `BT3_WASM_NO_FPCAST=1`: diagnosis only.

**Measured (fact):**
- A full variant build takes 129 s; a fresh clone takes 133 s.
- The output is 269/269 game, 61/61 port and 73/73 data, link OK, 103 stubs, 0 signature mismatches left.
- The variant build's `bt3.wasm` and `bt3.js` are byte-identical to the working build's.
- A fresh clone differs only through CRLF line endings in the embedded shaders. See the audit, §5.3 and plan task M5.

## 3. Game data and running

The user's own disc is needed. Nothing from it is committed or uploaded.

```bash
python port/tools/extract_disc.py bt3.iso gamedata
```

```bash
python port/web/devserver.py --port 8080 --mount gamedata=<path to gamedata>
```

Then open `http://127.0.0.1:8080/index.html`.

| Option | Effect |
|---|---|
| `?gpu=webgpu` | the WebGPU renderer |
| `?autostart=1` | start without clicking |
| `?env=BT3_...` | engine variables, e.g. `BT3_DEMO=1`, `BT3_PAD=<frame>:<button>,...` |

The server binds to 127.0.0.1 only.

## 4. Tests

Unless noted, every test runs against the server above. "Data" means the test needs the user's game data.

| Command | Checks | Needs |
|---|---|---|
| `python port/tests/wasm_sf/run.py` | software-float lowering vs. the PS2 model | emcc |
| `python port/tests/pipekey/run.py` | pipeline-key packing | emcc |
| `node port/tests/wgsl/check.js` | WGSL generated for the 11 shaders validates | naga |
| `node port/tests/web_data/test_data.js` | `bt3_data.js` range reads and cache | node |
| `python port/tests/wasm_mem/build.py` | memory layouts (B is the one used) | emcc, node |
| `node port/tests/bench/bench.js --label <l> --headless --shots 150 [--query gpu=webgpu]` | demo battle: trace hash, frame times, screenshots | data, Chrome |
| `node port/tests/bench/bench.js ... --boot --from 600 --to 3000 --shots 200` | the boot sequence | data, Chrome |
| `node port/tests/bench/compare_shots.js <a> <b>` | screenshot equality between two runs | — |
| `node port/tests/bench/present_rate.js [--query gpu=webgpu]` | pictures presented per second | data, Chrome |
| `node port/tests/web/dashboard.js [--query gpu=webgpu]` | the performance dashboard and the diagnostics export | data, Chrome |
| `node port/tests/web/controllers.js` | controller set-up with simulated pads | data, Chrome |
| `node port/tests/web/saves.js` | IndexedDB saves with the game's own saves | data, Chrome |
| `node port/tests/web/verify.js` | the data check is not run in the browser, and that is said (log, crash report, dashboard) | data, Chrome |
| `node port/tests/web/extras.js --data <gamedata>` | added stages, songs, texture packs over HTTP; reported when the server does not list folders. It starts its own server on port 8090, with a temporary folder holding copies from your data, removed afterwards | data, Chrome |
| `node port/tests/web/start_options.js` | skip logos / intro / copyright page and menus in 4:3, by their effects (the intro movie's fetch, picture brightness, black side columns on WebGL 2 and WebGPU) | data, Chrome |
| `node port/tests/web/audio.js` | the page's audio output: battle sound, silence with every volume at 0, music on the title | data, Chrome |
| `node port/tests/web/touch.js` | on-screen touch controls with **simulated** touch input (Chrome's emulation, a phone-sized viewport): presses, several fingers, interruptions, orientation, editor, kept layout; what the game received | data, Chrome |
| `port/tests/ps2float` | native, against PCSX2 | **blocked here**: needs the native toolchain and PCSX2 data |

The `devserver.py` used by the tests must be the current one: it serves the folder listings (`?list`) that added stages, songs and texture packs need. Without them they are reported as unavailable.

## 5. Continuous integration

`.github/workflows/browser.yml` runs `port/tests/ci/browser_checks.sh` on `ubuntu-24.04`, from a clean checkout:
- **Toolchain:** emsdk 6.0.12, with its `upstream/bin` LLVM tools on the PATH; Node 24.19.0; Python 3.12 with libclang 18.1.1; naga 30.0.1 built with Rust 1.99.0.
- **Steps:** the build (game, port, data shapes, link, WGSL by naga), then `wasm_sf`, `pipekey`, `web_data` and `wasm_mem`.
- **Constraints:** no secrets and no artifacts; no game data.

The job's summary lists what is not checked there, and why:
- needs game data: bench, dashboard, controllers, saves, verify, extras;
- needs a WebGPU adapter: `wgsl/check.js`;
- needs physical controllers;
- needs native tools: `ps2float`.

The same script runs by hand on any machine with the toolchain on the PATH:

```bash
sh port/tests/ci/browser_checks.sh
```
