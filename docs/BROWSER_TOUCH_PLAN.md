# Touch controls for the browser port: audit and plan

This was written before the implementation, on 2026-10-13. Results go into
[BROWSER_FEATURE_MATRIX.md](BROWSER_FEATURE_MATRIX.md); the test commands into [BROWSER_BUILD.md](BROWSER_BUILD.md).

## 1. How input reaches the game today (audit)

| Layer | Where | What it does |
|---|---|---|
| The game | `src/` (libpad) | Reads 18-byte pad replies: two active-low button bytes (`include/sys/pad.h`), then the right and left sticks' X and Y |
| **The mapping layer** | `port/src/gs/gs_input.c`, `Port_PadRead(socket, data)`, shared with native | Builds a reply per player from **24 engine actions** (`kActions`: 16 PS2 buttons, then the left and right sticks' up, down, left and right):<br>• a controller: per-player bindings, controller button → action;<br>• the keyboard: per-player bindings, key → action;<br>• `BT3_PAD`, a scripted list for tests;<br>• then the "Square counts as a direction" option, `clash_square`.<br>A settings window that is open takes everything |
| Browser wrap | `port/src/web/web_ui.c`, `__wrap_Port_PadRead` | Records what the game received, for the page's live view and test (`Bt3Ui_PadButtons`); the input test |
| Page | `port/web/controllers.js`, `settings.js` | Bindings (through the engine: `Bt3Ui_CaptureBegin`, `Bt3Ui_BindingSet`), guided set-up, presets, live view, test |
| Physical controllers | SDL3 (Emscripten) on top of the browser's Gamepad API | `gs_input.c` reads them through SDL |

## 2. Where touch input enters, and why there

Touch controls press **engine actions**, as a held key does. They do not press pad bits or keys, and they are not a gamepad.

| Option | Verdict |
|---|---|
| Patch the reply in `__wrap_Port_PadRead` (browser-only) | Rejected. It would duplicate the action → pad-bit table, and touch would bypass `clash_square` (Square as a direction would not work from touch) |
| A virtual SDL gamepad (`SDL_AttachVirtualJoystick`) | Rejected. Touch would take a controller slot and go through the *physical* controller bindings: remapping a gamepad would remap the touch buttons. That mixes the two things that must stay separate |
| Synthetic keyboard events | Rejected (requirement) |
| **A hook in `Port_PadRead`: virtual actions** | **Chosen** |

The hook works like this:
- `gs_input.c` calls `PortInput_VirtualActions(socket, &lx, &ly)` where it reads the keyboard, under the same condition: nothing while the settings window is open.
- The call returns an action mask, plus, optionally, the left stick's analog position.
- The actions become pad bits through the same `kActions` table, and stick directions as keys make them, all before `clash_square`.
- Natively a weak default returns nothing, so native behaviour is unchanged. This is checked with `port/tests/native/compare.sh` against `v0.1.33`.
- The browser's `web_ui.c` provides the real function, fed by the page (`Bt3Ui_TouchSet`).

**This is the one change to a shared file**, and it is justified: it is the only way touch goes through the same mapping layer as the keyboard without duplicating it.

## 3. The page side (`port/web/touch.js`, new)

**The overlay** lives in `#stage`, the element that also goes full screen. In it are, as HTML elements:
- a d-pad;
- a left analog stick;
- the four face buttons;
- L1, R1, L2, R2;
- START, SELECT, L3 and R3.

Each control names the engine actions it presses (by action index, `Bt3Ui_ActionLabel`).

**Input:**
- Pointer events with pointer capture.
- A map from pointer id to control, so each finger is its own. The held set is the union of all live pointers' actions.
- `touch-action: none` on the controls. The overlay box itself does not take events, so the game and the page under it are untouched.
- On a change the page calls `Bt3Ui_TouchSet(actions, lx, ly)`. It does that only on change, never repeating presses.

**Releasing:** every pointer is released on:
- `pointerup`, `pointercancel` and `lostpointercapture`;
- window `blur`, `visibilitychange` (hidden) and `pagehide`;
- the overlay being disabled or hidden;
- the settings panel opening.

The engine also gets nothing from touch while the settings window is open (the hook's condition).

**Settings** are kept in `localStorage` under `bt3.touch`, as JSON, apart from the engine's settings file (`bt3.settings`) and from the game's saves (IndexedDB `bt3-saves`):
- shown: auto (on a touch screen) / on / off;
- size;
- opacity;
- left- or right-handed (mirrors the layout);
- per-control position and size from the editor.

**Settings panel:** a Touch tab with those, plus "Edit layout" and "Reset layout".

**Editor:** a mode of the overlay itself.
- Drag a control to move it; select it for − / + size.
- Global opacity.
- Done, or Reset.
- Positions are fractions of the stage box. They are clamped inside it and inside the safe area (`env(safe-area-inset-*)`), and applied again on resize and orientation change.

**Play layout:** with touch controls shown and the page in landscape, the stage fills the viewport (`position: fixed`; the picture letterboxed by the engine as in full screen). In portrait, a message asks to turn the device; it does not block, and the controls stay usable.

## 4. Limitations known before starting

- **No real phone or tablet here.** Tests use Chrome's touch emulation (`Input.dispatchTouchEvent`), which produces real pointer events with distinct ids, and a narrow viewport. **That is not real-device validation**, and the results will say so.
- Mobile browsers need **JSPI**: Chrome and Edge for Android have it. iOS Safari before 27 does not; Safari 27 and Firefox are reported to have it (compatibility tables, unconfirmed).
- **Memory:** the program reserves about 550 MB of WebAssembly memory; low-memory phones may refuse it. Not measured on a phone.
- **Full screen:** iPhone browsers do not offer element full screen. The fixed play layout is the fallback.
- **Haptics** (`navigator.vibrate`) are not used: they are not in the requirements, and the support varies.
- **Player 1 only:** touch controls play as player 1 (socket 0). A second player on the same screen is out of scope.

## 5. Result (2026-10-13)

**Implemented as planned:**
- **The hook:** `gs_input.c` gathers keyboard and on-screen actions into one mask, and `web_ui.c` provides `PortInput_VirtualActions` and `Bt3Ui_TouchSet`.
- **The overlay and its settings:** `port/web/touch.js`, the Touch tab, and the styles in `index.html`.
- **File lists:** `touch.js` is in `wasm_build.py`'s page files and in `browser_package.py`.

**Corrections found while testing:**
- The touch code may call the engine only after its `ready`. Calling it at `attach` (before the runtime is up) aborted the start, so `Bt3Settings.engineReady` was added.
- The d-pad's touch area is square, so its corners (the diagonals) can be touched; it was round like the buttons.
- The constants are prefixed `TOUCH_`, because classic scripts share one global scope (`controllers.js` has `STICK_DEAD`).

**Native:** `port/tests/native/compare.sh v0.1.33 7635ad6` (the hook commit) gave headless 70, OpenGL 12 + 4 screenshots, Vulkan 67 + 15, win64 70 lines: **all identical**.

**Test:** `port/tests/web/touch.js` (32 checks, PASS twice) uses **simulated touch input**: Chrome's touch emulation on a phone-sized viewport (915 × 412, then portrait). It reads what the game received from the engine. The emulated device has a quirk: after multi-finger sequences, touches are not delivered to the next page loaded (bisected; the controls are not involved). The reload checks therefore run first.

**Not done here:** a real phone or tablet. There is no device on this machine, so behaviour on real touch hardware, a real phone's memory, and its browser are **unverified**.
