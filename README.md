# Tenkaichi3Decomp

A native PC port of **Dragon Ball Z: Budokai Tenkaichi 3** (PlayStation 2, USA release, SLUS-21678), built on
the matching decompilation in **BT3-Decompiled**. It is the game's own code compiled for PC, with a new
renderer, sound and input layer underneath; it is not an emulator.

This repository contains **no game data**. You need your own disc image of that release: the setup reads the
game's data from it, and it stays on your computer.

> **Official source.** This project lives only at
> [github.com/z3xox/Tenkaichi3Decomp](https://github.com/z3xox/Tenkaichi3Decomp), and it never ships the game:
> you need your own disc. We do not make, run or endorse any website that lets you play Budokai Tenkaichi 3 in a
> browser, with or without ads; such sites are not affiliated with this project. A site that runs the game
> without your own disc is handing out the game's copyrighted code and data. Our releases are free and have no
> ads, accounts or payments.

## What works

- The whole game loop: boot, opening movie, menus, saving, fights, split screen.
- A GPU renderer (Vulkan) with the PS2's effects (outline, glow, depth tint, distance blur), each switchable.
  An OpenGL 3.3 renderer draws the same picture for machines without a working Vulkan (F1, Video, Renderer; it
  is also tried by itself when Vulkan cannot start). It is slower than Vulkan.
- Internal resolution from 1x to 8x; 4:3, 16:9, 21:9 and wider without stretching the fight.
- Smooth motion (experimental, off by default; F1, Video, or F9): the game computes a fight 30 times a second, as
  on the console; with this the renderer draws pictures in between, for 60, 120 or 240 a second. The fight itself,
  replays and online play are unchanged, and the two players of an online match need not agree on it. The newest
  picture reaches the screen 17 to 29 ms later than without. Known limits: pictures are left out at camera cuts
  and where the camera would pass through scenery; 240 is uneven; little tested with OpenGL. Works in both halves
  of a split screen.
- Music, voices and sound effects; keyboard and controllers, fully rebindable; two players.
- Clashes: Square counts as a direction (off by default; F1, Controls): in a clash of two beams or two rushes the
  game counts newly pressed directions, which is why the stick is turned; with this each press of Square counts as
  one, as a tap of the d-pad does. It changes only your own pad, so it can be used online. A press can be made to
  count for 1, 1.5, 1.75 or 2 (1.75 unless set: the d-pad has four buttons to tap in turn, Square is one).
- Texture packs made for PCSX2 (`.dds` or `.png`): copied into the `textures` folder next to the game, they are used as they are.
- Stages and music from outside the disc: a stage model (`.unk`) dropped into the `stages` folder next to the
  game, or a song (`.mp3`, converted in the background when the game starts, or `.adx`) into `songs`, is added to the stage select, its name written in the game's own
  lettering (cut from the disc's name pictures while the game runs). They are not written to the save and are
  not offered in an online match. See [docs/port/adding_stages.md](docs/port/adding_stages.md) and
  [adding_songs.md](docs/port/adding_songs.md).
- In the game, **F1** opens the settings, or **START + SELECT** on a controller (which also works the window: d-pad and
  Cross, Circle to leave, L1 / R1 for the tabs). Controls has a row to choose another key or button. **Esc** asks
  before it closes the game, and the window has an "Exit game" button. Its Cheats tab has "Unlock everything" (a debug function the game's
  developers left in: all characters, stages, music, items and Zenni).

- **Online play, experimental** (from release 0.1.8): the main menu's hidden "Dragon Net Battle"
  entry is back and opens a small window. One player hosts and gets a six-character room code, the other joins
  with the code: no port forwarding (from 0.1.16; hosting on a port and joining by address is still there);
  both get the same roster with everything unlocked, each sees their own fighter's view full screen, and leaving
  the match puts each player back where they were with their own save. Linux and Windows play against each
  other. With rollback (the host chooses how many frames, and the input delay) your own moves come out at once
  and the game corrects itself when the other player's buttons arrive.
  The host also chooses the battle (single, team, or DP battle with a limit of 10, 15 or 20) and the time limit.
  Each player sees their own gauges on the left with the players' names under them, and hears the voice language
  chosen in their own save.
  The two players can write to each other: Tab opens a line, Enter sends it, Esc or Tab puts it away (the keyboard
  does not play while a line is typed; a controller does; the key can be changed under Controls). Lines show in the
  lower left corner for a few seconds. F1, Misc turns the chat off; the line also has "Mute rival". It is not
  part of the game's state and a copy without it simply sees nothing.

Online play is new. It has been played over the internet between a Linux and a Windows machine. The input delay
is chosen from the connection when a match starts (or set by the host), both players need the same release (the
game says so when they differ; 0.1.10 does not play against 0.1.8 / 0.1.9, and 0.1.15 does not play against
0.1.14 and earlier). F1, Video has a meter for the frame rate and, in a match, the ping, rollbacks and waits. The 32-bit
Linux build has no rollback. [docs/netplay_notes.md](docs/netplay_notes.md) is the working log of how it is built
and what was checked.

Room codes go through a small matchmaking service (a Cloudflare Worker; its whole source is in
[port/matchmaking](port/matchmaking), and `BT3_MATCH_URL` points the game at another copy of it). The game tells it
the name typed in the window and the addresses it can be reached at; the service keeps them with the room for at
most ten minutes and hands them to the player who enters the code. The two games then send to each other
directly. Where the routers do not allow that, the match goes through a relay (Cloudflare's TURN service), which
passes the match's packets (the buttons pressed) on and keeps nothing. Without a room code none of this is used.

See [docs/port/README.md](docs/port/README.md) for the running log of what is done, what is verified and what is not.

## Requirements

- A 64-bit x86 processor and a graphics card with Vulkan support. Most of the work is on the processor (the
  game's arithmetic is reproduced exactly as on the PS2); a recent mid-range CPU is recommended.
- Linux (glibc 2.35 or newer) or Windows 10 / 11.
- macOS on Apple Silicon: **experimental**, built from source only (see below).
- About 4 GB of disc space, and your disc image as an `.iso` file.

## Installing a release

Unpack the release, start `Tenkaichi3Decomp-setup` (`Tenkaichi3Decomp-setup.exe` on Windows), choose your disc image, press Play. The
setup checks that the image is the unmodified USA release and unpacks the game's data next to the program.

On a machine without a working Vulkan the setup window and the game use OpenGL by themselves. To ask for OpenGL
from the start (a Vulkan driver that starts but is incomplete, as on Intel Ivy Bridge), set `BT3_GPU_API=gl`:
`BT3_GPU_API=gl ./Tenkaichi3Decomp-setup`, and the same for `./Tenkaichi3Decomp`. The setup also runs without a
window: `./Tenkaichi3Decomp-setup --install <disc image>`.

## Building from source

```
python3 install.py --iso <your disc image>
```

does everything on Linux: checks the tools, reads the disc, builds the 64-bit program and runs the game's demo
fight as a test. `python3 install.py --check` lists what is missing. `port/setup/build.sh && ./Tenkaichi3Decomp-setup` is
the same with a window.

### macOS (experimental)

Apple Silicon Macs, native arm64, built from source on the Mac itself; there is no macOS release archive. The
attract-demo fight gives the same state as the Linux program on every frame, the rewind test (rollback) passes,
and the game draws through Metal with sound; online play against Linux / Windows has not been tried on two machines
yet. With [Homebrew](https://brew.sh):

```
xcode-select --install                       # (once: Apple's command line tools)
brew install llvm sdl3 shaderc spirv-cross
python3 install.py --iso <your disc image>   # then ./play.sh
```

`python3 install.py --check` lists what is missing. How the build works and what is left:
[docs/port/macos_plan.md](docs/port/macos_plan.md).

The release archives for Linux and Windows are built from the repository alone, with no disc involved:
`port/release/build.sh` (needs only docker) or the GitHub workflow in `.github/workflows/release.yml`. The
repository carries the shape of the game's built-in data tables without their values (`port/data`); the
finished program fetches the values from your disc when it starts.

## Playing in the browser

The same game also runs in a web page: the game's code and this port's engine (memory, files, saves, sound, input,
the OpenGL renderer) compiled to WebAssembly with Emscripten, with a WebGL 2 renderer and a WebGPU one
(`?gpu=webgpu`). The page is served from your own computer by a small local server (127.0.0.1 only) and reads the
game's data from the folder `extract_disc.py` makes; nothing is uploaded and the build holds no game data. It is
not a website: it does not let anyone host the game, and it needs the player's own disc like the PC build.

```
python port/tools/extract_disc.py <your disc image> gamedata
python port/tools/wasm_build.py game && python port/tools/wasm_build.py port
python port/tools/wasm_build.py data && python port/tools/wasm_build.py link
python port/web/play.py gamedata
```

`play.py` starts the server and opens the page (a current Chrome or Edge: it needs JavaScript Promise Integration).
The page has its own settings window (F1 or START + SELECT), controller set-up with a picture of the controller,
touch controls for phones and tablets, and saves kept in the browser with export and import. Not in the browser:
online play and Vulkan. `python port/tools/browser_package.py <name>` makes a package to hand out (the page, the
server and the unpacker; no game data). Build, toolchain and tests: [docs/BROWSER_BUILD.md](docs/BROWSER_BUILD.md);
what works and how it was checked: [docs/BROWSER_FEATURE_MATRIX.md](docs/BROWSER_FEATURE_MATRIX.md).

## Layout

| Path | Contents |
|---|---|
| `src/`, `include/` | The game's code, from BT3-Decompiled; changes for PC are inside `#ifdef PORT` |
| `port/src/` | The PC side: memory, files, memory card, movies; `gs/` renderer, sound, input, settings window |
| `port/tools/` | The build: pointer rewriting for 64-bit, packaging, extraction |
| `port/data/` | The game's built-in data tables as shape only (labels, sizes, pointer slots), no values |
| `port/release/` | The container and scripts that build the release archives |
| `port/setup/` | The setup window |
| `port/web/`, `port/src/web/` | The browser build: the page (settings, controllers, touch, saves) and its engine side |
| `port/tests/web/` | The browser build's tests (headless Chrome) |
| `port/third_party/` | Dear ImGui (MIT), the maths library of newlib |
| `docs/` | Notes on the game (from the decompilation) and `docs/port/` on the port |

This project was made with the help of AI.

## Contributors

- [RexxColder](https://github.com/RexxColder): the OpenGL 3.3 renderer beside Vulkan (the split of the renderer
  into a shared recorder and two back ends), and stages and songs from outside the disc (the manifest, the
  install scripts, the menu changes and their documentation), in
  [pull request #1](https://github.com/z3xox/Tenkaichi3Decomp/pull/1). Both were brought onto the main line
  with changes; the documents under `docs/port/` say which.

## Licence

The port's own code (everything under `port/` except `port/third_party/`, the setup, the build tools, and the
PC changes inside `#ifdef PORT`) is under the [GNU Affero General Public License, version 3](LICENSE)
(AGPL-3.0-only) in every commit after `06e4671` (2026-10-14). Up to and including that commit it was under the
[MIT licence](LICENSE-MIT), and that code, as it was then, stays available under MIT; contributions made under
MIT keep their MIT notice inside the AGPL whole. Whoever passes the program on, changed or not, or runs a
changed version for others over a network (a lobby or relay server, a web page), has to offer its source under
the same licence. The libraries in `port/third_party/` keep their own licences (Dear ImGui: MIT; newlib's maths
library: its BSD-style terms, in that folder).
The game's code in `src/` and `include/` comes from the decompilation and is not ours to license: it belongs
to the game's rights holders.

## Legal

This project is not affiliated with or endorsed by the game's developers, publishers or rights holders. It
exists for preservation, study and interoperability, and distributes none of the game's assets. All rights to
the game belong to their owners. You need your own legally obtained copy of the game to use it.
