// Test of the Controls tab's controller part (port/web/controllers.js) and the engine's input path behind it, with
// SIMULATED controllers (port/tests/web/fake_gamepads.js replaces the browser's Gamepad API before the page loads; the
// engine's SDL code reads them as real ones). No physical controller is involved: this proves the page and the engine's
// handling of what the Gamepad API reports, not any particular device.
//   node port/tests/web/controllers.js
'use strict';
const fs = require('fs');
const path = require('path');
const { launch, evaluate, sleep } = require('../lib/cdp.js');

const ROOT = path.resolve(__dirname, '../../..');
const PAD_A = 'DualSense Wireless Controller (STANDARD GAMEPAD Vendor: 054c Product: 0ce6)';
const PAD_B = 'Xbox 360 Controller (XInput STANDARD GAMEPAD Vendor: 045e Product: 028e)';
const PAD_RAW = 'Unknown Test Joystick (Vendor: 1234 Product: 5678)';  // (a make SDL's database does not have)
// the Gamepad API's standard layout (button indices) and the engine's values (SDL_GamepadButton, include/sys/pad.h bits)
const STD = { south: 0, east: 1, west: 2, north: 3, start: 9, guide: 16 };
const SDL_BUTTON = { south: 0, east: 1, west: 2, north: 3, guide: 5 };  // (guide: bound to nothing by default)
const PAD_CROSS = 0x4000, PAD_START = 0x0008;
const ENGINE_POLL_MS = 2500;  // gs_input.c looks for plugged and unplugged controllers about once a second
const STEP_MS = 400;
let failed = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failed++; };

(async () => {
  const b = await launch({ headless: true });
  const ev = (e) => evaluate(b.send, e);
  const call = (n, r, t, a) => ev(`window.bt3Module.ccall('${n}', ${JSON.stringify(r)}, ${JSON.stringify(t)}, ${JSON.stringify(a)})`);
  const binding = (kind, player, action) => call('Bt3Ui_Binding', 'number', ['number', 'number', 'number'], [kind, player, action]);
  const gameSaw = () => call('Bt3Ui_PadButtons', 'number', ['number', 'number'], [0, 0]);
  const press = async (pad, button, down = true) => { await ev(`__pads.button(${pad}, ${button}, ${down})`); await sleep(STEP_MS); };
  const tap = async (pad, button) => { await press(pad, button, true); await press(pad, button, false); };
  const key = async (code, key, vk) => {
    for (const type of ['rawKeyDown', 'keyUp']) await b.send('Input.dispatchKeyEvent', { type, code, key, windowsVirtualKeyCode: vk });
    await sleep(STEP_MS);
  };
  const text = (sel) => ev(`document.querySelector(${JSON.stringify(sel)}).textContent`);
  // a button of the controller part (the bindings table above has buttons named after controller buttons: "Back / Select")
  const click = (label) => ev(`(() => { const b = [...document.querySelectorAll('#tab-controls-page button')].find((x) => (x.textContent === ${JSON.stringify(label)} || x.textContent.startsWith(${JSON.stringify(label)} + ' ') || x.textContent.startsWith(${JSON.stringify(label)} + '…')) && !x.disabled && x.offsetParent); if (!b) throw new Error('no button ' + ${JSON.stringify(label)}); b.click(); return 0; })()`);

  await b.send('Page.enable');
  await b.send('Page.addScriptToEvaluateOnNewDocument', { source: fs.readFileSync(path.join(__dirname, 'fake_gamepads.js'), 'utf8') });
  await b.send('Page.navigate', { url: 'http://127.0.0.1:8080/index.html?autostart=1' });
  await sleep(12000);

  // ---- connection, device information, the live view ----
  await ev(`__pads.connect({ id: ${JSON.stringify(PAD_A)} }); 0`);
  await sleep(ENGINE_POLL_MS);
  await ev(`document.getElementById('settings-open').click(); document.getElementById('tab-controls').click(); 0`);
  await sleep(STEP_MS);
  check(await call('Bt3Ui_GamepadCount', 'number', [], []) === 1, 'connected: the engine lists one controller');
  const list = await text('#settings ul.pads');
  check(list.includes('054c') && list.includes('17 buttons') && list.includes('4 axes') && list.includes("the game's controller 1"), `list: ${list}`);
  await press(0, STD.north, true);
  check(await ev(`document.querySelectorAll('#settings .padview .cell')[${STD.north}].classList.contains('on')`), 'live view: a held button is lit');
  await press(0, STD.north, false);
  await ev(`__pads.axis(0, 0, -1); 0`); await sleep(STEP_MS);
  check(Number(await ev(`document.querySelectorAll('#settings .padview meter')[0].value`)) === -1, 'live view: an axis follows');
  await ev(`__pads.axis(0, 0, 0); 0`);

  // ---- a controller without the standard layout, several controllers, browser indices reused ----
  await ev(`__pads.connect({ id: ${JSON.stringify(PAD_RAW)}, mapping: '', buttons: 12, axes: 2 }); 0`);
  await ev(`__pads.connect({ id: ${JSON.stringify(PAD_B)} }); 0`);
  await sleep(ENGINE_POLL_MS);
  await ev(`settings.refresh(); 0`);
  const many = await text('#settings ul.pads');
  check(many.includes('reports no standard layout: its buttons may be placed wrongly'), 'a controller without the standard layout: listed, with the warning');
  check(await call('Bt3Ui_GamepadCount', 'number', [], []) === 3, 'the engine lists all three (SDL takes every controller in a browser)');
  await ev(`__pads.disconnect(0); 0`);
  await sleep(STEP_MS);
  check((await text('#toasts')).includes('Controller disconnected'), 'disconnect: a notification');
  await ev(`__pads.connect({ id: ${JSON.stringify(PAD_A)} }); 0`);  // takes browser index 0 again
  await sleep(ENGINE_POLL_MS);
  await ev(`settings.refresh(); 0`);
  const map = JSON.parse(await ev(`JSON.stringify(settings.controllers.enginePads())`));
  const pads = JSON.parse(await ev(`JSON.stringify(navigator.getGamepads().filter(Boolean).map((p) => ({ i: p.index, id: p.id })))`));
  const idOf = (i) => (pads.find((p) => p.i === i) || {}).id || '';
  // (SDL names controllers its own way, so the check is by make: vendor and product as the engine has them)
  const vps = []; for (const m of map) vps.push(await call('Bt3Ui_GamepadVendorProduct', 'number', ['number'], [m.slot]) >>> 0);
  const vpOf = (id) => { const m = /Vendor:\s*([0-9a-f]{4})\s*Product:\s*([0-9a-f]{4})/i.exec(id); return m ? (parseInt(m[1], 16) << 16 | parseInt(m[2], 16)) >>> 0 : 0; };
  check(map.length === 3 && map.every((m, k) => m.browserIndex >= 0 && vpOf(idOf(m.browserIndex)) === vps[k]),
        `indices changed: engine slots matched to the right browser controllers (${map.map((m) => `${m.slot}->${m.browserIndex}`).join(', ')})`);

  // ---- the disconnected-controller warning ----
  await ev(`document.getElementById('bind-player-1').click(); 0`);
  await ev(`window.bt3Module.ccall('Bt3Ui_PadSlotSet', null, ['number', 'number'], [1, 3]); settings.refresh(); 0`);
  check(!(await ev(`document.querySelector('#settings .warning').hidden`)) && (await text('#settings .warning')).includes('controller 4'), 'player 2 on a missing controller: warned');
  await ev(`window.bt3Module.ccall('Bt3Ui_PadSlotSet', null, ['number', 'number'], [1, 1]); document.getElementById('bind-player-0').click(); 0`);
  // player 1 back on the DualSense (engine slot of PAD_A)
  const slotA = map.find((m) => idOf(m.browserIndex).startsWith('DualSense')).slot, browserA = map.find((m) => m.slot === slotA).browserIndex;
  await call('Bt3Ui_PadSlotSet', null, ['number', 'number'], [0, slotA]);

  // ---- guided set-up: capture, duplicate (swap), back, skip, cancel ----
  const before = []; for (let a = 0; a < 16; a++) before.push(await binding(2, 0, a));
  await click('Set up all controller buttons');
  await sleep(STEP_MS);
  check((await text('#settings .wizard')).includes('D-pad up'), 'set-up: asks for D-pad up first');
  await tap(browserA, STD.guide);   // step 0 (D-pad up) <- guide (free)
  check(await binding(2, 0, 0) === SDL_BUTTON.guide, 'set-up: the engine took the button (D-pad up = guide)');
  await click('Skip'); await sleep(STEP_MS);   // step 1 kept
  await click('Back'); await sleep(STEP_MS);   // back to step 1
  const backText = await text('#settings .wizard');
  check(backText.includes('D-pad down'), `set-up: Back returns one step (${backText})`);
  await click('Skip'); await click('Skip'); await click('Skip'); await sleep(STEP_MS);  // steps 1..3 kept
  await tap(browserA, STD.guide);   // step 4 (Cross) <- guide: already D-pad up's
  check((await text('#settings .wizard')).includes('already D-pad up'), 'set-up: a duplicate is found');
  await click('Swap'); await sleep(STEP_MS);
  const swapped = [await binding(2, 0, 0), await binding(2, 0, 4)];
  check(swapped[1] === SDL_BUTTON.guide && swapped[0] === before[4], `set-up: Swap gives D-pad up Cross's old one (D-pad up ${swapped[0]}, Cross ${swapped[1]}, Cross before ${before[4]})`);
  await click('Cancel'); await sleep(STEP_MS);
  const after = []; for (let a = 0; a < 16; a++) after.push(await binding(2, 0, a));
  check(JSON.stringify(after) === JSON.stringify(before), 'set-up: Cancel puts every binding back');

  // ---- a completed set-up is kept, also after a reload ----
  await click('Set up all controller buttons'); await sleep(STEP_MS);
  for (let a = 0; a < 4; a++) await click('Skip');
  await tap(browserA, STD.north);   // Cross <- north (Triangle's): swap
  await click('Swap'); await sleep(STEP_MS);
  for (let a = 5; a < 16; a++) { await click('Skip'); }
  await sleep(STEP_MS);
  check((await text('#settings .wizard')).includes('Set-up done: 1 of 16'), 'set-up: done, 1 changed');

  // ---- keys: the engine takes a key; presets; defaults ----
  await click('Set up all keys'); await sleep(STEP_MS);
  check(await ev(`document.activeElement.id`) === 'canvas', 'key set-up: the game area takes the key');
  await key('KeyG', 'g', 71);
  check(await binding(1, 0, 0) === await call('Bt3Ui_ScancodeFromName', 'number', ['string'], ['G']), 'key set-up: D-pad up = G');
  await click('Cancel'); await sleep(STEP_MS);
  await ev(`document.getElementById('key-preset').value = '1'; 0`);
  await click('Use these keys'); await sleep(STEP_MS);
  check(await binding(1, 0, 0) === await call('Bt3Ui_ScancodeFromName', 'number', ['string'], ['W']) &&
        await binding(1, 0, 16) === await call('Bt3Ui_ScancodeFromName', 'number', ['string'], ['Up']), 'preset: W for D-pad up, Up for the left stick');
  await ev(`document.getElementById('key-preset').value = '0'; 0`);
  await click('Use these keys'); await sleep(1000);
  check(await binding(1, 0, 0) === await call('Bt3Ui_ScancodeFromName', 'number', ['string'], ['Up']), "defaults: the engine's own (arrows)");
  check(await binding(2, 0, 4) === SDL_BUTTON.north, 'defaults for keys leave the controller buttons alone');

  // ---- nothing of the panel reaches the game; the test shows the bindings; back to the game ----
  await press(browserA, STD.north, true);
  check(await gameSaw() === 0, 'panel open: a held button does not reach the game');
  await click('Test'); await sleep(STEP_MS);
  check((await call('Bt3Ui_PadButtons', 'number', ['number', 'number'], [1, 0]) & PAD_CROSS) !== 0 && await gameSaw() === 0,
        'test: the binding gives Cross (north, as set up), the game still gets nothing');
  check(await ev(`[...document.querySelectorAll('#settings .gp-control.is-pressed')].map((c) => c.id).join()`) === 'gp-cross', 'test: Cross lights on the controller picture');
  await ev(`document.getElementById('settings-close').click(); 0`);  // closed with the button still held
  await sleep(STEP_MS);
  check(await gameSaw() === 0, 'closed while held: the held button stays out of the game');
  await press(browserA, STD.north, false);
  await press(browserA, STD.north, true);
  check((await gameSaw() & PAD_CROSS) !== 0, 'back to the game: a new press is Cross');
  await press(browserA, STD.north, false);
  check(await ev(`document.activeElement.id`) === 'canvas', 'closed: the focus is back on the game');
  await b.send('Input.dispatchKeyEvent', { type: 'rawKeyDown', code: 'Enter', key: 'Enter', windowsVirtualKeyCode: 13 });
  await sleep(STEP_MS);
  check((await gameSaw() & PAD_START) !== 0, 'back to the game: Enter (START) reaches it');
  await b.send('Input.dispatchKeyEvent', { type: 'keyUp', code: 'Enter', key: 'Enter', windowsVirtualKeyCode: 13 });

  // ---- persistence: the set-up survives a reload ----
  await b.send('Page.reload');
  await sleep(12000);
  check(await binding(2, 0, 4) === SDL_BUTTON.north, 'reload: Cross is still north');

  await b.close();
  console.log(failed ? `RESULT: FAIL (${failed})` : 'RESULT: PASS (simulated controllers; no physical hardware)');
  process.exit(failed ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
