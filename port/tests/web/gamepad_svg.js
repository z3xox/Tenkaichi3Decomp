// Validation of the controller artwork (port/web/gamepad.svg) as the settings overlay's Controls tab shows it, in Chrome
// through the DevTools protocol. Checks:
//   - it is served as SVG; it has a title, 24 controls, each a group with a unique stable id, role="button",
//     tabindex="0", an aria-label, and data-action naming one of the engine's 24 actions - every action exactly once
//     (labels from the engine itself, Bt3Ui_ActionLabel); decoration is aria-hidden and takes no clicks;
//   - every control can be told apart: a point inside it hits that control and no other (elementFromPoint);
//   - it fits the panel's width without overflowing, keeps its shape (viewBox ratio), and its smallest text stays
//     legible at the panel's width; it scales from a narrow to a wide box without changing its proportions;
//   - the states look different: normal, hover (the real mouse), keyboard focus, selected, pressed, remapping, unbound;
//   - screenshots of the panel (port/build/gamepad_svg_panel.png, _states.png) to look at.
//   node port/tests/web/gamepad_svg.js
'use strict';
const fs = require('fs'), path = require('path');
const { launch, evaluate, sleep } = require('../lib/cdp.js');

const PAGE = 'http://127.0.0.1:8080/index.html?autostart=1&env=BT3_DEMO=1';
const START_MS = 10000;
const MIN_TEXT_PX = 7.5;  // the smallest rendered text size counted as legible
const OUT = path.join(__dirname, '../../build');
let failed = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failed++; };

(async () => {
  const b = await launch({ headless: true });
  const ev = (e) => evaluate(b.send, e);
  await b.send('Emulation.setDeviceMetricsOverride', { width: 1280, height: 900, deviceScaleFactor: 2, mobile: false });
  await b.send('Page.navigate', { url: PAGE });
  await sleep(START_MS);

  const type = await ev(`fetch('gamepad.svg').then((r) => r.headers.get('Content-Type'))`);
  check(type === 'image/svg+xml', `served as SVG (${type})`);
  await ev(`settings.open(), document.getElementById('tab-controls').click(), 0`);
  await sleep(800);
  await ev(`document.querySelector('.gp-picture').id = 'gp-test', 0`);  // the Controls tab's own picture

  // ---- structure and identifiers ----
  const s = JSON.parse(await ev(`(() => { const svg = document.querySelector('#gp-test svg'), c = [...svg.querySelectorAll('.gp-control')];
    const engine = Array.from({ length: window.bt3Module.ccall('Bt3Ui_Get','number',['string'],['actions']) }, (_, a) => window.bt3Module.ccall('Bt3Ui_ActionLabel','string',['number'],[a]));
    return JSON.stringify({ title: svg.querySelector('title') && svg.querySelector('title').textContent, n: c.length,
      ids: c.map((e) => e.id), actions: c.map((e) => e.dataset.action), engine,
      a11y: c.every((e) => e.getAttribute('role') === 'button' && e.getAttribute('tabindex') === '0' && e.getAttribute('aria-label')),
      anchors: c.every((e) => /^\\d+(\\.\\d+)? \\d+(\\.\\d+)?$/.test(e.dataset.anchor || '')),
      deco: [...svg.querySelectorAll('.gp-deco')].every((d) => d.getAttribute('aria-hidden') === 'true' && getComputedStyle(d).pointerEvents === 'none'),
      raster: svg.querySelectorAll('image, foreignObject').length, external: [...svg.querySelectorAll('*')].some((e) => /^https?:/.test(e.getAttribute('href') || '')) }); })()`));
  check(s.title === 'Controller' && s.n === 24, `a title and 24 controls (${s.title}, ${s.n})`);
  check(new Set(s.ids).size === s.ids.length && s.ids.every((id) => /^gp-[a-z0-9-]+$/.test(id)), `stable, unique ids: ${s.ids.join(' ')}`);
  const engineSet = new Set(s.engine);
  check(s.actions.length === s.engine.length && s.actions.every((a) => engineSet.has(a)) && new Set(s.actions).size === s.actions.length,
        `every engine action (${s.engine.length}) on exactly one control`);
  check(s.a11y, 'every control: role=button, tabindex=0, aria-label');
  check(s.anchors, 'every control: a binding-label anchor (data-anchor)');
  check(s.deco, 'decoration: aria-hidden, takes no clicks');
  check(s.raster === 0 && !s.external, 'no raster images, nothing external');

  // ---- each control told apart: a point inside it hits it and no other ----
  const hits = JSON.parse(await ev(`(() => { const out = {};
    for (const c of document.querySelectorAll('#gp-test .gp-control')) {
      const r = c.getBoundingClientRect(); let found = null;
      for (let i = 1; i < 12 && !found; i++) for (let j = 1; j < 12 && !found; j++) {
        const x = r.left + r.width * i / 12, y = r.top + r.height * j / 12, e = document.elementFromPoint(x, y);
        if (e && e.closest('.gp-control') === c) found = [Math.round(x), Math.round(y)];
      }
      out[c.id] = found; }
    return JSON.stringify(out); })()`));
  const missing = Object.entries(hits).filter(([, p]) => !p).map(([id]) => id);
  check(missing.length === 0, `every control reachable by a click on it alone${missing.length ? ' (not: ' + missing.join(', ') + ')' : ''}`);

  // ---- size, shape, legibility in the panel; scaling ----
  const fit = JSON.parse(await ev(`(() => { const svg = document.querySelector('#gp-test svg'), r = svg.getBoundingClientRect(),
      page = document.getElementById('tab-controls-page').getBoundingClientRect(), vb = svg.viewBox.baseVal, k = r.width / vb.width;
    const sizes = [...svg.querySelectorAll('text')].map((t) => parseFloat(getComputedStyle(t).fontSize) * k);
    return JSON.stringify({ w: r.width, h: r.height, page: page.width, ratio: r.width / r.height, vb: vb.width / vb.height, minText: Math.min(...sizes) }); })()`));
  check(fit.w <= fit.page + 0.5 && fit.w >= 300, `fits the panel's width (${Math.round(fit.w)} of ${Math.round(fit.page)} px)`);
  check(Math.abs(fit.ratio - fit.vb) < 0.01, `keeps its shape (${fit.ratio.toFixed(3)} vs ${fit.vb.toFixed(3)})`);
  check(fit.minText >= MIN_TEXT_PX, `smallest text ${fit.minText.toFixed(1)} px at the panel's width (at least ${MIN_TEXT_PX})`);
  const scaled = JSON.parse(await ev(`(() => { const box = document.getElementById('gp-test'), svg = box.querySelector('svg'), out = [];
    box.style.maxWidth = 'none';
    for (const w of [240, 900]) { box.style.width = w + 'px'; const r = svg.getBoundingClientRect(); out.push([w, r.width, r.height]); }
    box.style.width = ''; box.style.maxWidth = ''; return JSON.stringify(out); })()`));
  check(scaled.every(([w, rw, rh]) => Math.abs(rw - w) < 1 && Math.abs(rw / rh - fit.vb) < 0.01), `scales with its box, same shape (${scaled.map(([w, rw, rh]) => `${rw}x${Math.round(rh)}`).join(', ')})`);

  // ---- the states ----
  const fill = (id) => ev(`getComputedStyle(document.querySelector('#${id} .gp-face')).fill + ' ' + getComputedStyle(document.querySelector('#${id} .gp-face')).stroke + ' ' + getComputedStyle(document.querySelector('#${id} .gp-face')).strokeDasharray`);
  const normal = await fill('gp-square');  // (Square: bound by default, not chosen)
  const sq = hits['gp-square'];
  await b.send('Input.dispatchMouseEvent', { type: 'mouseMoved', x: sq[0], y: sq[1] }); await sleep(250);
  const hover = await fill('gp-square');
  await b.send('Input.dispatchMouseEvent', { type: 'mouseMoved', x: 5, y: 5 }); await sleep(250);
  await ev(`document.getElementById('gp-square').focus(), 0`); await b.send('Input.dispatchKeyEvent', { type: 'keyDown', key: 'Shift' });
  await b.send('Input.dispatchKeyEvent', { type: 'keyUp', key: 'Shift' }); await sleep(150);
  const focus = await ev(`document.activeElement.id + ' ' + document.getElementById('gp-square').matches(':focus-visible') + ' ' + getComputedStyle(document.querySelector('#gp-square .gp-face')).stroke`);
  await ev(`document.getElementById('gp-square').blur(), 0`);
  const states = {};
  for (const cls of ['is-selected', 'is-pressed', 'is-capturing', 'is-unbound']) {
    await ev(`document.getElementById('gp-square').classList.add('${cls}'), 0`); await sleep(120);
    states[cls] = await fill('gp-square');
    await ev(`document.getElementById('gp-square').classList.remove('${cls}'), 0`);
  }
  const all = [normal, hover, ...Object.values(states)];
  check(new Set(all).size === all.length, `normal, hover, selected, pressed, remapping, unbound all look different:\n       ${['normal', 'hover', ...Object.keys(states)].map((k, i) => `${k}: ${all[i]}`).join('\n       ')}`);
  check(/^gp-square true/.test(focus) && !focus.endsWith(normal.split(' ').slice(3, 6).join(' ')), `keyboard focus shows (${focus})`);

  // ---- the configurator: choosing a control shows its bindings; a capture marks it; Esc keeps the binding ----
  await ev(`document.querySelector('details.more').open = true, 0`);  // (the list: its chosen row is checked too)
  const tri = hits['gp-triangle'];
  await b.send('Input.dispatchMouseEvent', { type: 'mousePressed', x: tri[0], y: tri[1], button: 'left', clickCount: 1 });
  await b.send('Input.dispatchMouseEvent', { type: 'mouseReleased', x: tri[0], y: tri[1], button: 'left', clickCount: 1 });
  await sleep(300);
  const pick = JSON.parse(await ev(`JSON.stringify({ sel: document.querySelector('#gp-triangle').classList.contains('is-selected'),
    text: document.querySelector('.gp-chosen').textContent, row: (document.querySelector('tr.chosen th') || {}).textContent,
    key: settings.bindingName(1, 7) })`));
  check(pick.sel && pick.row === 'Triangle' && pick.text.includes('Triangle') && pick.text.includes(pick.key), `a click on Triangle chooses it: "${pick.text}"`);
  await ev(`document.querySelector('.gp-chosen button.bind').click(), 0`); await sleep(200);
  const cap = await ev(`document.querySelector('#gp-triangle').classList.contains('is-capturing')`);
  await b.send('Input.dispatchKeyEvent', { type: 'keyDown', key: 'Escape', code: 'Escape', windowsVirtualKeyCode: 27 });
  await b.send('Input.dispatchKeyEvent', { type: 'keyUp', key: 'Escape', code: 'Escape', windowsVirtualKeyCode: 27 });
  await sleep(400);
  const after = JSON.parse(await ev(`JSON.stringify({ cap: document.querySelector('#gp-triangle').classList.contains('is-capturing'),
    key: settings.bindingName(1, 7), focus: document.activeElement.className, open: settings.dialog.open })`));
  check(cap && !after.cap && after.key === pick.key && after.open && after.focus === 'bind pick',
        `its key binding marks it as remapping; Esc keeps "${after.key}", the overlay stays open, focus back on its button`);
  const unbound = await ev(`[...document.querySelectorAll('.gp-control.is-unbound')].map((g) => g.id).join(' ')`);
  check(unbound === 'gp-rs-up gp-rs-right gp-rs-down gp-rs-left', `the unbound ones marked (the right stick by default): ${unbound}`);
  const overlay = JSON.parse(await ev(`(() => { const r = settings.dialog.getBoundingClientRect(), bd = document.getElementById('settings-backdrop');
    return JSON.stringify({ cx: r.x + r.width / 2, vw: document.documentElement.clientWidth, top: r.top, bottom: innerHeight - r.bottom, back: getComputedStyle(bd).display }); })()`));
  check(Math.abs(overlay.cx - overlay.vw / 2) < 2 && overlay.top > 0 && overlay.bottom > 0 && overlay.back === 'block',
        `an overlay: centred, over a backdrop (${JSON.stringify(overlay)})`);
  await ev(`document.querySelector('#gp-triangle').classList.remove('is-selected'), 0`);

  // ---- screenshots to look at ----
  const shot = async (file) => {
    const r = JSON.parse(await ev(`JSON.stringify(document.getElementById('settings').getBoundingClientRect())`));
    const p = await b.send('Page.captureScreenshot', { format: 'png', clip: { x: r.x, y: r.y, width: r.width, height: Math.min(r.height, 520), scale: 1 } });
    fs.writeFileSync(path.join(OUT, file), Buffer.from(p.data, 'base64'));
  };
  await shot('gamepad_svg_panel.png');
  await ev(`(() => { const on = (id, c) => document.getElementById(id).classList.add(c);
    on('gp-cross', 'is-pressed'); on('gp-dpad-up', 'is-pressed'); on('gp-rs-right', 'is-pressed'); on('gp-triangle', 'is-selected');
    on('gp-r2', 'is-capturing'); on('gp-l3', 'is-unbound'); on('gp-select', 'is-unbound'); return 0; })()`);
  await sleep(200);
  await shot('gamepad_svg_states.png');
  console.log(`  screenshots: ${path.join(OUT, 'gamepad_svg_panel.png')}, gamepad_svg_states.png`);

  await b.send('Input.dispatchMouseEvent', { type: 'mousePressed', x: 4, y: 4, button: 'left', clickCount: 1 });
  await b.send('Input.dispatchMouseEvent', { type: 'mouseReleased', x: 4, y: 4, button: 'left', clickCount: 1 });
  await sleep(200);
  check(!(await ev(`settings.dialog.open`)), 'a click on the backdrop closes it');

  await b.close();
  console.log(failed ? `RESULT: FAIL (${failed})` : 'RESULT: PASS');
  process.exit(failed ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
