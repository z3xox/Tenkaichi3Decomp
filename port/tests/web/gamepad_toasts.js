// Test of the controller connection notifications (port/web/toasts.js, controllers.js) with SIMULATED controllers
// (fake_gamepads.js: the browser's own connection events, no physical device). Checks:
//   - a connection and a disconnection each show a toast naming that controller (without the browser's vendor text);
//   - two controllers: unplugging one names that one, and the Controls tab still shows the other as connected;
//   - one plugged back in is announced again; the same one told twice is announced once;
//   - while playing: the game keeps running and keeps the focus, the toast lets clicks through to the game;
//   - toasts are in a live region (a disconnection as an alert), go away by themselves, and close on a click;
//   - the Controls tab's status follows at once, and says "No controller connected" once (no stream of toasts);
//   - in full screen the toasts are inside the full-screen element (they show over the game).
//   node port/tests/web/gamepad_toasts.js
'use strict';
const fs = require('fs'), path = require('path');
const { launch, evaluate, sleep } = require('../lib/cdp.js');

const XBOX = 'Xbox Wireless Controller (STANDARD GAMEPAD Vendor: 045e Product: 0b13)';
const DS = '054c-0ce6-DualSense Wireless Controller';  // (Firefox's form of an id)
const TOAST_GONE_MS = 5200;  // toasts.js TOAST_MS and its fade
let failed = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failed++; };

(async () => {
  const b = await launch({ headless: true });
  const ev = (e) => evaluate(b.send, e);
  const toasts = async () => JSON.parse(await ev(`JSON.stringify([...document.querySelectorAll('#toasts .toast:not(.leaving)')].map((t) => ({ text: t.textContent.replace('×', '').trim(), role: t.getAttribute('role') })))`));
  const status = () => ev(`document.querySelector('#settings .pad-status').textContent`);
  const frame = () => ev(`window.bt3Module.ccall('Bt3Ui_Get', 'number', ['string'], ['frame'])`);
  await b.send('Page.enable');
  await b.send('Page.addScriptToEvaluateOnNewDocument', { source: fs.readFileSync(path.join(__dirname, 'fake_gamepads.js'), 'utf8') });
  await b.send('Page.navigate', { url: 'http://127.0.0.1:8080/index.html?autostart=1' });
  await sleep(12000);

  check(await ev(`document.getElementById('toasts').getAttribute('aria-live')`) === 'polite' && (await toasts()).length === 0, 'a polite live region, empty at start');
  check((await status()).startsWith('No controller connected'), `no controller: the Controls tab says so (${await status()})`);

  // ---- while playing: one connects ----
  await ev(`document.getElementById('canvas').focus(), 0`);
  const f0 = await frame();
  await ev(`__pads.connect({ id: ${JSON.stringify(XBOX)} }); 0`);
  await sleep(400);
  let t = await toasts();
  check(t.length === 1 && t[0].text === 'Controller connected — Xbox Wireless Controller' && t[0].role === null, `connected: "${t[0] && t[0].text}"`);
  const focus = await ev(`document.activeElement.id`);
  const hit = await ev(`(() => { const r = document.querySelector('#toasts .toast').getBoundingClientRect(), c = document.getElementById('canvas').getBoundingClientRect();
    const over = r.top >= c.top && r.bottom <= c.bottom && r.left >= c.left && r.right <= c.right;
    const e = document.elementFromPoint(r.left - 6, r.top + r.height / 2); return JSON.stringify({ over, beside: e && e.id }); })()`);
  await sleep(600);
  check(focus === 'canvas' && (await frame()) > f0, `the game keeps the focus and keeps running (frame ${f0} -> ${await frame()})`);
  check(JSON.parse(hit).over && JSON.parse(hit).beside === 'canvas', `over the game's picture; beside it a click reaches the game (${hit})`);

  // ---- told twice: once; a second controller ----
  await ev(`(() => { const e = new Event('gamepadconnected'); Object.defineProperty(e, 'gamepad', { value: navigator.getGamepads()[0] }); window.dispatchEvent(e); return 0; })()`);
  await sleep(200);
  check((await toasts()).length === 1, 'the same controller told twice: announced once');
  await ev(`__pads.connect({ id: ${JSON.stringify(DS)} }); 0`);
  await sleep(400);
  t = await toasts();
  check(t.length === 2 && t[1].text === 'Controller connected — DualSense Wireless Controller', `a second one: "${t[1] && t[1].text}"`);

  // ---- the Controls tab follows at once ----
  await ev(`settings.open(), document.getElementById('tab-controls').click(), 0`);
  await sleep(300);
  check(await status() === '2 controllers connected: Xbox Wireless Controller, DualSense Wireless Controller', `the Controls tab: ${await status()}`);
  check(await ev(`getComputedStyle(document.getElementById('toasts')).zIndex > getComputedStyle(document.getElementById('settings')).zIndex`), 'toasts show over the settings overlay');

  // ---- one of two unplugged ----
  await ev(`__pads.disconnect(0); 0`);
  await sleep(400);
  t = await toasts();
  const last = t[t.length - 1];
  check(last.text === 'Controller disconnected — Xbox Wireless Controller' && last.role === 'alert', `disconnected (an alert): "${last.text}"`);
  check(await status() === 'Controller connected: DualSense Wireless Controller', `the other still shown connected: ${await status()}`);
  check(!t.some((x) => /No controller/.test(x.text)), 'no "all disconnected" message while one is left');

  // ---- plugged back in: announced again ----
  await ev(`__pads.connect({ id: ${JSON.stringify(XBOX)} }); 0`);
  await sleep(400);
  t = await toasts();
  check(t.length <= 3 && t[t.length - 1].text === 'Controller connected — Xbox Wireless Controller', `reconnected: announced again (at most 3 shown: ${t.length})`);

  // ---- a click closes one; the rest go by themselves ----
  const before = (await toasts()).length;
  await ev(`document.querySelector('#toasts .toast-close').click(), 0`);
  await sleep(300);
  check((await toasts()).length === before - 1, 'the close button dismisses a toast');
  await sleep(TOAST_GONE_MS);
  check(await ev(`document.querySelectorAll('#toasts .toast').length`) === 0, 'the others go away by themselves');

  // ---- both unplugged: one notification each, the status says none (once, persistent) ----
  await ev(`__pads.disconnect(0); __pads.disconnect(1); 0`);
  await sleep(400);
  t = await toasts();
  check(t.length === 2 && t.every((x) => x.text.startsWith('Controller disconnected')), `both unplugged: one each (${t.map((x) => x.text).join(' | ')})`);
  check((await status()).startsWith('No controller connected'), 'the Controls tab: no controller connected');
  await sleep(TOAST_GONE_MS);
  check(await ev(`document.querySelectorAll('#toasts .toast').length`) === 0 && (await status()).startsWith('No controller connected'),
        'the toasts go, the status stays');

  // ---- full screen: the toasts are inside what is shown ----
  await ev(`settings.close(), 0`);
  const inStage = await ev(`document.getElementById('stage').contains(document.getElementById('toasts'))`);
  check(inStage, 'the toasts are inside the stage (what goes full screen)');

  await b.close();
  console.log(failed ? `RESULT: FAIL (${failed})` : 'RESULT: PASS (simulated controllers; no physical hardware)');
  process.exit(failed ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
