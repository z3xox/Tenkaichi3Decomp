// Test of the on-screen touch controls (port/web/touch.js; the engine's side: gs_input.c PortInput_VirtualActions,
// web_ui.c Bt3Ui_TouchSet) with simulated touch input, in Chrome through the DevTools protocol:
//   node port/tests/web/touch.js
// Chrome's touch emulation (Input.dispatchTouchEvent) makes real pointer events, one pointer id per finger; this is
// SIMULATED input on a desktop browser emulating a phone's screen, not a test on a real device. What the game receives
// is read from the engine (Bt3Ui_PadButtons / Bt3Ui_PadSticks: the last pad reply the game got, after the engine's
// action mapping). Checks:
//   - shown on a touch screen (auto), the play layout; not shown, and not in the way, on a desktop without one;
//   - one button: press and release; several at once; several fingers on different controls; one finger let go while
//     the others stay; the d-pad's diagonal; the analog stick's deflection and its return;
//   - a cancelled touch, the window losing the focus, the page hidden and shown again, the settings panel opening:
//     everything let go, and the controls work again afterwards;
//   - turning to portrait and back: the message, every control inside the stage;
//   - the editor: a control dragged, kept after a reload, the layout reset; size and opacity kept; turned off.
'use strict';
const { launch, evaluate, sleep } = require('../lib/cdp.js');

const PAGE = 'http://127.0.0.1:8080/index.html?autostart=1&env=BT3_DEMO=1';
const PHONE = { width: 915, height: 412, deviceScaleFactor: 1, mobile: true };  // a phone held sideways (CSS pixels)
const PORTRAIT = { ...PHONE, width: PHONE.height, height: PHONE.width };
const START_MS = 12000;   // the engine running (the attract demo reads the pads every frame)
const SETTLE_MS = 300;    // a few of the game's frames: the pads read again
let failed = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failed++; };

(async () => {
  const b = await launch({ headless: true });
  const ev = (e) => evaluate(b.send, e);
  const settle = () => sleep(SETTLE_MS);
  const phone = async (metrics) => {
    await b.send('Emulation.setDeviceMetricsOverride', metrics);
    await b.send('Emulation.setTouchEmulationEnabled', { enabled: true, maxTouchPoints: 5 });
  };
  const open = async (prefs) => {
    await b.send('Page.navigate', { url: 'http://127.0.0.1:8080/index.html' });  // (the origin's storage first)
    await sleep(500);
    await ev(prefs === null ? `localStorage.removeItem('bt3.touch'), 0` : `localStorage.setItem('bt3.touch', ${JSON.stringify(JSON.stringify(prefs))}), 0`);
    await b.send('Page.navigate', { url: PAGE });
    await sleep(START_MS);
  };
  // what the game got: its pad bits by name (Bt3Ui_PadBitName), and the sticks
  const got = async () => JSON.parse(await ev(`(() => { const c = (n, r, t, a) => window.bt3Module.ccall(n, r, t, a);
    const m = c('Bt3Ui_PadButtons', 'number', ['number', 'number'], [0, 0]), s = c('Bt3Ui_PadSticks', 'number', ['number', 'number'], [0, 0]) >>> 0;
    return JSON.stringify({ held: Array.from({ length: 16 }, (_, i) => m >> i & 1 ? c('Bt3Ui_PadBitName', 'string', ['number'], [i]) : null).filter(Boolean).sort().join('+'),
                            lx: s >>> 24, ly: s >>> 16 & 255 }); })()`));
  const centre = async (id, fx = 0.5, fy = 0.5) => JSON.parse(await ev(`(() => { const r = document.querySelector('#touch [data-id="${id}"]').getBoundingClientRect();
    return JSON.stringify({ x: r.left + r.width * ${fx}, y: r.top + r.height * ${fy} }); })()`));
  const touch = (type, points) => b.send('Input.dispatchTouchEvent', { type, touchPoints: points.map((p) => ({ x: p.x, y: p.y, id: p.id })) });

  // ---- a desktop without a touch screen: not shown, not in the way ----
  await open(null);
  const desk = JSON.parse(await ev(`JSON.stringify({ hidden: document.getElementById('touch').hidden, play: document.body.classList.contains('touch-play'),
    under: (() => { const s = document.getElementById('stage').getBoundingClientRect(); return document.elementFromPoint(s.right - 40, s.bottom - 60).id; })() })`));
  check(desk.hidden && !desk.play && desk.under === 'canvas', `desktop (no touch screen): hidden, page layout kept, the game's canvas under the controls' places (${JSON.stringify(desk)})`);

  // ---- a phone held sideways ----
  await phone(PHONE);
  await open(null);
  const shown = JSON.parse(await ev(`JSON.stringify({ hidden: document.getElementById('touch').hidden, play: document.body.classList.contains('touch-play'),
    portrait: document.querySelector('#touch .touch-portrait').hidden, frame: window.bt3Module.ccall('Bt3Ui_Get','number',['string'],['frame']) })`));
  check(!shown.hidden && shown.play && shown.portrait && shown.frame > 0, `touch screen (auto): shown, play layout, no portrait message, the game running (${JSON.stringify(shown)})`);

  const crossDefault = await centre('cross');
  // (This part reloads the page, so it comes before the multi-finger sequences: after those, Chrome's emulated touch
  // device delivers no touches to the next page loaded - its state, checked with a minimal page; not the controls')
  // ---- the editor; preferences kept; reset ----
  await ev(`settings.touch.startEdit(), 0`); await sleep(300);
  const before = await centre('cross'), to = { x: before.x - 300, y: before.y - 150 };
  await touch('touchStart', [{ ...before, id: 1 }]);
  await touch('touchMove', [{ x: (before.x + to.x) / 2, y: (before.y + to.y) / 2, id: 1 }]);
  await touch('touchMove', [{ ...to, id: 1 }]);
  await touch('touchEnd', []); await settle();
  check((await got()).held === '', 'editing: dragging Cross does not press it');
  await ev(`(() => { const t = document.querySelector('.touch-toolbar'); [...t.querySelectorAll('button')].find((x) => x.textContent === 'Bigger').click(); [...t.querySelectorAll('button')].find((x) => x.textContent === 'Done').click(); return 0; })()`);
  const moved = await centre('cross');
  check(Math.hypot(moved.x - to.x, moved.y - to.y) < 2, `Cross dragged to its new place (${Math.round(moved.x)}, ${Math.round(moved.y)})`);
  await ev(`settings.touch.set({ size: 120, opacity: 80 }), 0`);
  const kept = JSON.parse(await ev(`localStorage.getItem('bt3.touch')`));
  check(kept.layout.cross && kept.layout.cross.scale > 1 && kept.size === 120 && kept.opacity === 80, `kept in localStorage 'bt3.touch' (${JSON.stringify(kept)})`);
  check(await ev(`localStorage.getItem('bt3.settings') === null || !/touch/.test(localStorage.getItem('bt3.settings'))`), "apart from the engine's settings file ('bt3.settings')");
  await open(kept);
  const reloaded = await centre('cross');
  check(Math.hypot(reloaded.x - moved.x, reloaded.y - moved.y) < 2 && await ev(`getComputedStyle(document.getElementById('touch')).getPropertyValue('--touch-opacity').trim()`) === '0.8',
        'after a reload: Cross where it was put, the opacity kept');
  await touch('touchStart', [{ ...reloaded, id: 1 }]); await settle();
  check((await got()).held === 'Cross', 'the moved Cross plays');
  await touch('touchEnd', []); await settle();
  await ev(`settings.touch.resetLayout(), 0`);
  const reset = await centre('cross');
  check(Math.hypot(reset.x - crossDefault.x, reset.y - crossDefault.y) < 2 && JSON.stringify(JSON.parse(await ev(`localStorage.getItem('bt3.touch')`)).layout) === '{}',
        'layout reset: Cross back to its default place, the kept layout emptied');
  await ev(`settings.touch.set({ hand: 'left' }), 0`);
  const leftHand = await centre('cross'), stage = JSON.parse(await ev(`JSON.stringify(document.getElementById('stage').getBoundingClientRect())`));
  check(leftHand.x < stage.x + stage.width / 2, `left-handed: the face buttons on the left (${Math.round(leftHand.x)} of ${Math.round(stage.width)})`);
  await ev(`settings.touch.set({ hand: 'right' }), 0`);

  const cross = await centre('cross'), square = await centre('square'), l1 = await centre('l1'), triangle = await centre('triangle');
  await touch('touchStart', [{ ...cross, id: 1 }]); await settle();
  check((await got()).held === 'Cross', `one finger on Cross: the game gets Cross (${(await got()).held})`);
  await touch('touchEnd', []); await settle();
  check((await got()).held === '', 'let go: nothing held');

  await touch('touchStart', [{ ...cross, id: 1 }, { ...square, id: 2 }, { ...l1, id: 3 }]); await settle();
  check((await got()).held === 'Cross+L1+Square', `three fingers, three controls: Cross, Square and L1 together (${(await got()).held})`);
  // (Chrome's emulation: lifting some fingers is a touchEnd that lists them, adding one a touchStart with it)
  await touch('touchEnd', [{ ...square, id: 2 }]); await settle();  // finger 2 (Square) lifted
  check((await got()).held === 'Cross+L1', `one finger let go (Square): the others stay held (${(await got()).held})`);
  await touch('touchStart', [{ ...triangle, id: 4 }]); await settle();
  check((await got()).held === 'Cross+L1+Triangle', `another finger added (Triangle) (${(await got()).held})`);
  await touch('touchEnd', []); await settle();
  check((await got()).held === '', 'all let go: nothing held');

  const upRight = await centre('dpad', 0.92, 0.08);
  await touch('touchStart', [{ ...upRight, id: 1 }]); await settle();
  check((await got()).held === 'Right+Up', `d-pad up-right corner: Up and Right (${(await got()).held})`);
  const down = await centre('dpad', 0.5, 0.95);
  await touch('touchMove', [{ ...down, id: 1 }]); await settle();
  check((await got()).held === 'Down', `the same finger slid down: Down only (${(await got()).held})`);
  await touch('touchEnd', []); await settle();

  const stick = await centre('stick'), stickLeft = await centre('stick', 0, 0.5), stickFar = { x: stickLeft.x - 200, y: stickLeft.y + 200 };
  await touch('touchStart', [{ ...stick, id: 1 }]); await settle();
  let s = await got();
  check(s.lx === 0x80 && s.ly === 0x80, `stick touched in its centre: centred (${s.lx.toString(16)}, ${s.ly.toString(16)})`);
  await touch('touchMove', [{ ...stickLeft, id: 1 }]); await settle();
  s = await got();
  check(s.lx <= 0x02 && Math.abs(s.ly - 0x80) <= 2, `pushed to its left edge: full left (${s.lx.toString(16)}, ${s.ly.toString(16)})`);
  await touch('touchMove', [{ ...stickFar, id: 1 }]); await settle();
  s = await got();
  check(s.lx < 0x80 && s.ly > 0x80 && Math.hypot(s.lx - 0x80, s.ly - 0x80) <= 0x7F + 2, `dragged far outside (captured): held at the rim, down-left (${s.lx.toString(16)}, ${s.ly.toString(16)})`);
  await touch('touchEnd', []); await settle();
  s = await got();
  check(s.lx === 0x80 && s.ly === 0x80 && s.held === '', 'let go: the stick back in the centre');

  // ---- interruptions ----
  await touch('touchStart', [{ ...cross, id: 1 }, { ...l1, id: 2 }]); await settle();
  await touch('touchCancel', []); await settle();
  check((await got()).held === '', 'touch cancelled (pointercancel): everything let go');
  await touch('touchStart', [{ ...cross, id: 1 }]); await settle();
  await ev(`window.dispatchEvent(new Event('blur')), 0`); await settle();
  check((await got()).held === '', 'the window loses the focus while Cross is held: let go');
  await touch('touchMove', [{ ...cross, x: cross.x + 1, id: 1 }]); await settle();
  check((await got()).held === '', '... and the same finger moving afterwards does not press it again');
  await touch('touchEnd', []); await settle();
  await touch('touchStart', [{ ...square, id: 1 }]); await settle();
  await ev(`Object.defineProperty(document, 'hidden', { configurable: true, get: () => true }); document.dispatchEvent(new Event('visibilitychange')); 0`); await settle();
  check((await got()).held === '', 'the page hidden (another tab in front) while Square is held: let go');
  await touch('touchEnd', []);
  await ev(`delete document.hidden; document.dispatchEvent(new Event('visibilitychange')); 0`); await settle();
  await touch('touchStart', [{ ...square, id: 1 }]); await settle();
  check((await got()).held === 'Square', `back on the page: the controls work again (${(await got()).held})`);
  await touch('touchEnd', []); await settle();
  await touch('touchStart', [{ ...cross, id: 1 }]); await settle();
  await ev(`settings.open(), 0`); await settle();
  check((await got()).held === '', 'the settings panel opening while Cross is held: let go (and the engine takes nothing while it is open)');
  await touch('touchEnd', []);
  await ev(`settings.close(), 0`); await settle();

  // ---- portrait and back ----
  const inside = `(() => { const s = document.querySelector('#touch .touch-box').getBoundingClientRect();
    return [...document.querySelectorAll('#touch .touch-ctl')].every((e) => { const r = e.getBoundingClientRect();
      return r.left >= s.left - 0.5 && r.top >= s.top - 0.5 && r.right <= s.right + 0.5 && r.bottom <= s.bottom + 0.5; }); })()`;
  await phone(PORTRAIT); await sleep(800);
  check(!(await ev(`document.querySelector('#touch .touch-portrait').hidden`)) && await ev(inside), 'turned to portrait: the message shown, every control inside the stage');
  await phone(PHONE); await sleep(800);
  check(await ev(`document.querySelector('#touch .touch-portrait').hidden`) && await ev(inside), 'back to landscape: no message, every control inside the stage');
  const crossAgain = await centre('cross');
  await touch('touchStart', [{ ...crossAgain, id: 1 }]); await settle();
  check((await got()).held === 'Cross', 'after turning: the controls still play');
  await touch('touchEnd', []); await settle();

  await ev(`settings.touch.set({ mode: 'off' }), 0`);
  const off = JSON.parse(await ev(`JSON.stringify({ hidden: document.getElementById('touch').hidden, play: document.body.classList.contains('touch-play') })`));
  check(off.hidden && !off.play, 'turned off: hidden, the page layout back');

  await b.close();
  console.log(failed ? `RESULT: FAIL (${failed})` : 'RESULT: PASS (simulated touch input; no real device)');
  process.exit(failed ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
