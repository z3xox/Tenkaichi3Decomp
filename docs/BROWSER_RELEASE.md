# Tenkaichi3Decomp in the browser: release web-0.1.33.3

*Dragon Ball Z: Budokai Tenkaichi 3* (PlayStation 2, USA release, SLUS-21678) in your web browser: the decompiled game's code compiled to WebAssembly, running in a web page on your own computer.
- Based on upstream Tenkaichi3Decomp **0.1.33**. Source: [z3xox/Tenkaichi3Decomp](https://github.com/z3xox/Tenkaichi3Decomp).
- Tested in Chrome on Windows 11, from the downloaded package: unpacking a disc, `play.py`, boot, a fight, the settings. Controllers and touch were tested with simulated devices only.
- Since web-0.1.33.2: the server no longer prints error messages when the browser closes a connection (a reload or a closed tab).

> **The game's data is not included.** You need your own copy of the game's disc. The package contains only program code; the game's data tables are built empty and filled from your disc when the game starts. Do not share the extracted data, and do not put it on a public server.

## What you need

| | |
|---|---|
| A current **Chrome or Edge** | The page needs JavaScript Promise Integration (JSPI) and WebGL 2, and says so at start if one is missing. Tested: Chrome 154 on Windows 11. Firefox and Safari: **not tested** |
| **Python 3** | [python.org](https://www.python.org/downloads/); on Windows tick "Add python.exe to PATH" when installing |
| **7-Zip** | [7-zip.org](https://www.7-zip.org/), to read the disc image (found where its installer puts it, or on the PATH) |
| Your own disc image | The unmodified USA release (SLUS-21678), as an `.iso`; about 4 GB of free space for the unpacked data |
| For the WebGPU renderer (optional) | A browser with WebGPU enabled: open the page with `?gpu=webgpu` |

## Setting up (the package)

Unzip the package, open a terminal in its folder, then:

1. **Unpack your disc** once, into the `gamedata` folder next to `play.py` (about 4 GB, 68,611 files; a few minutes):

   ```bash
   python extract_disc.py <your disc image> gamedata
   ```

2. **Play:**

   ```bash
   python play.py
   ```

   It checks the data folder, starts a local server (127.0.0.1 only) and opens the game in your browser. Keep the window open while playing; Ctrl+C stops it. With the data elsewhere: `python play.py <data folder>`.

3. If the browser that opens is not Chrome or Edge, copy the address it prints (`http://127.0.0.1:8080/index.html`) into Chrome or Edge.

In the game, **F1** (or START + SELECT on a controller) opens the settings. Keyboard by default: arrows for the d-pad, K L J I for Cross, Circle, Square, Triangle, W A S D for the left stick, Enter for START.

**Package contents:** `web/` (the page and the program), `play.py`, `devserver.py` (the local server), `extract_disc.py`, this file, and `licenses/`.

**Your own server instead:** `python devserver.py --port 8080 --root web --mount gamedata=<data folder>`. Another server must support HTTP Range requests (the game reads its files in byte ranges); without folder listings, added stages, songs and texture packs are reported as unavailable.

## What works

- **Game:** boot, intro movie, menus, battles, two players. The game's own simulation runs exactly as in the PC version.
- **Two renderers:** WebGL 2 (the default) and WebGPU (open the page with `?gpu=webgpu`); both draw the same picture.
- **Settings** (F1, or START + SELECT on a controller), a window over the game: resolution, aspect ratio, HUD layout, menus in 4:3, effects, audio levels, controls, game speed, skip logos / intro / copyright page, Unlock everything.
- **Controllers:** a picture of the controller to change any button's key or controller button, guided set-up that lights the button to press, keyboard presets, a live view and a test. A notification when a controller connects or disconnects.
- **Touch controls:** an on-screen d-pad, analog stick and buttons over the game, played as player 1, shown on touch screens. Size, opacity, left- or right-handed layouts, and a layout editor.
- **Saves:** kept in the browser, with a backup version, and export and import as a `.zip`.
- **Texture packs, added stages and songs:** in the data folder's `textures`, `stages` and `songs` folders.
- **Performance view** and a diagnostics export (Settings, Developer).

## Known limitations

- **Not in the browser version:** online play and the Vulkan renderer (both are in the PC version).
- **Smooth motion** is off with WebGL 2, and up to 60 pictures a second with WebGPU.
- **Saves** live in the browser's storage: clearing the site's data, or a private window, loses them. Export a copy.
- **Not tested yet:** real controllers and real phones or tablets (simulated ones are tested), Firefox and Safari, playing an added song. The sound is checked to come out, not how it sounds.

Full list of what was tested: [BROWSER_FEATURE_MATRIX.md](https://github.com/z3xox/Tenkaichi3Decomp/blob/main/docs/BROWSER_FEATURE_MATRIX.md).

## Building from source

See [BROWSER_BUILD.md](https://github.com/z3xox/Tenkaichi3Decomp/blob/main/docs/BROWSER_BUILD.md): the toolchain (Emscripten 6.0.12), the four build steps and the tests.

## Licence

The project's code is under the GNU AGPL v3 (`licenses/Tenkaichi3Decomp-AGPL.txt`); code from before the original project's switch on 2026-10-14 also stays available under MIT (`licenses/Tenkaichi3Decomp-MIT.txt`). Whoever passes the program on, or runs a changed version for others over a network, has to offer its source. The licences of the libraries in the program (SDL3, Dawn's WebGPU header, Emscripten's runtime, newlib's maths library, xxHash) are in the package's `licenses` folder. The game itself, and its data, belong to their owners and are not part of this release.
