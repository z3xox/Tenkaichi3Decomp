// Test of upstream 0.1.32's start and menu options in the browser's settings panel (port/web/settings.js, web_ui.c:
// skip_logos, skip_intro, skip_copyright, menus_43), in Chrome through the DevTools protocol:
//   node port/tests/web/start_options.js
// What each option is checked by (the engine logs none of them):
//   - the panel: the four controls exist; ticking "Skip logos" saves it, and after a reload (the next start) it is on;
//   - skip_logos / skip_intro: when the opening movie's file (ZS3USOP.PSS, read over HTTP) is first fetched -
//     by default after the logos; much earlier without them; not at all without the intro;
//   - skip_copyright: the shown picture's brightness every 30 frames (BT3_SHOT) - the copyright page is a dim page held
//     for 300 frames before the title, which is bright;
//   - menus_43 at 16:9: the title screen (a menu) with black side columns - WebGL 2 from the window's own picture
//     (BT3_GL_WINSHOT), WebGPU from a capture of the page's canvas.
'use strict';
const zlib = require('zlib');
const { launch, evaluate, sleep } = require('../lib/cdp.js');

const PAGE = 'http://127.0.0.1:8080/index.html?autostart=1';
const MOVIE = /ZS3USOP\.PSS/i;
const DEFAULT_LATEST_MOVIE = 2400;  // by default the movie starts after the logos (measured: frame 1746)
const NO_LOGOS_BEFORE = 1000;       // without the logos it starts within the first seconds (measured: frame 242)
const COPYRIGHT = [300, 540];       // with logos and intro skipped: the copyright page's frames (measured: 270..570)
const TITLE_BY = 270;               // ... and with it skipped, the title is up by then (measured: from 240)
const LIT = (sum) => sum > 120;     // a pixel counted as lit (the three channels' sum)
const DARK = (sum) => sum < 15;     // ... and as black
let failed = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failed++; };

// The pixels of a PNG (8-bit RGB or RGBA, not interlaced: what Chrome's captureScreenshot gives)
function decodePng(buf) {
  let pos = 8, w = 0, h = 0, ch = 0;
  const idat = [];
  while (pos < buf.length) {
    const len = buf.readUInt32BE(pos), type = buf.toString('ascii', pos + 4, pos + 8), data = buf.subarray(pos + 8, pos + 8 + len);
    if (type === 'IHDR') { w = data.readUInt32BE(0); h = data.readUInt32BE(4); ch = data[9] === 6 ? 4 : data[9] === 2 ? 3 : 0;
      if (data[8] !== 8 || !ch || data[12] !== 0) throw new Error('unsupported PNG'); }
    if (type === 'IDAT') idat.push(data);
    pos += 12 + len;
  }
  const raw = zlib.inflateSync(Buffer.concat(idat)), stride = w * ch, out = Buffer.alloc(h * stride);
  for (let y = 0; y < h; y++) {
    const f = raw[y * (stride + 1)], src = raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1));
    for (let x = 0; x < stride; x++) {
      const a = x >= ch ? out[y * stride + x - ch] : 0, b = y ? out[(y - 1) * stride + x] : 0, c = x >= ch && y ? out[(y - 1) * stride + x - ch] : 0;
      const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
      const pred = f === 0 ? 0 : f === 1 ? a : f === 2 ? b : f === 3 ? (a + b) >> 1 : pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
      out[y * stride + x] = (src[x] + pred) & 255;
    }
  }
  return { w, h, ch, px: out };
}
// Of a picture: the share of black pixels in the outer tenth on each side, and in the middle two fifths
function sides({ w, h, ch, px }) {
  let side = 0, sideDark = 0, mid = 0, midDark = 0;
  for (let y = 0; y < h; y += 4) for (let x = 0; x < w; x += 4) {
    const k = (y * w + x) * ch, dark = DARK(px[k] + px[k + 1] + px[k + 2]);
    if (x < w * 0.1 || x >= w * 0.9) { side++; sideDark += dark; } else if (x > w * 0.3 && x < w * 0.7) { mid++; midDark += dark; }
  }
  return { side: sideDark / side, mid: midDark / mid };
}

(async () => {
  const b = await launch({ headless: true });
  const ev = (e) => evaluate(b.send, e);
  const frame = () => ev(`window.bt3Module ? window.bt3Module.ccall('Bt3Ui_Get', 'number', ['string'], ['frame']) : -1`);
  const shotsDir = `(window.bt3Module.FS.analyzePath('port/build/shots').exists || window.bt3Module.FS.mkdirTree('port/build/shots'), 0)`;
  await b.send('Page.enable');
  // the page's kept settings file before the start (as the page keeps it: localStorage 'bt3.settings'); null: as it is
  const start = async (settings, query = '') => {
    if (settings !== null) await ev(`localStorage.setItem('bt3.settings', ${JSON.stringify(settings.map((s) => s + '\n').join(''))}), 0`).catch(() => 0);
    await b.send('Page.navigate', { url: PAGE + query });
    for (let i = 0; i < 60 && (await frame()) < 0; i++) await sleep(250);
    await ev(shotsDir);
  };
  const until = async (f, maxMs) => { for (const end = Date.now() + maxMs; Date.now() < end && (await frame()) < f;) await sleep(250); return frame(); };
  // the frame at which the movie's file was first fetched (polled), or null if not by `last`
  const movieFrame = async (last) => {
    for (const end = Date.now() + 150000; Date.now() < end;) {
      const s = JSON.parse(await ev(`JSON.stringify({ f: window.bt3Module.ccall('Bt3Ui_Get','number',['string'],['frame']),
        m: performance.getEntriesByType('resource').some((e) => ${MOVIE}.test(e.name)) })`));
      if (s.m) return s.f;
      if (s.f >= last) return null;
      await sleep(250);
    }
    return null;
  };
  await b.send('Page.navigate', { url: 'http://127.0.0.1:8080/index.html' });  // (the origin's storage, before the first start)

  // ---- the panel; "Skip logos" ticked there is kept for the next start ----
  await start([]);
  const controls = await ev(`JSON.stringify(['menus_43', 'skip_logos', 'skip_intro', 'skip_copyright'].map((k) => {
    const i = document.getElementById('set-' + k); return [k, !!i, i ? i.closest('[role=tabpanel]').id : null]; }))`).then(JSON.parse);
  check(controls.every(([, there]) => there) && controls[0][2] === 'tab-video-page' && controls.slice(1).every(([, , tab]) => tab === 'tab-extras-page'),
        `panel: Menus in 4:3 (Video), Skip logos / intro / copyright page (Extras): ${controls.map(([k, t, tab]) => `${k}:${t ? tab : 'missing'}`).join(' ')}`);
  await ev(`document.getElementById('settings-open').click(); document.getElementById('tab-extras').click(); document.getElementById('set-skip_logos').click(); 0`);
  await sleep(1500);
  check(/skip_logos=1/.test(await ev(`localStorage.getItem('bt3.settings') || ''`)), 'ticking "Skip logos" keeps it in the settings file');
  await start(null);  // the next start, with the settings as the page kept them
  check(await ev(`window.bt3Module.ccall('Bt3Ui_Get','number',['string'],['skip_logos'])`) === 1 &&
        await ev(`document.getElementById('set-skip_logos').checked`), 'after a reload: Skip logos is on (engine and panel)');
  const noLogos = await movieFrame(NO_LOGOS_BEFORE);
  check(noLogos !== null && noLogos < NO_LOGOS_BEFORE, `without the logos the movie starts early (frame ${noLogos})`);

  // ---- defaults; no intro ----
  await start([]);
  const byDefault = await movieFrame(DEFAULT_LATEST_MOVIE);
  check(byDefault !== null && byDefault >= NO_LOGOS_BEFORE, `by default the movie starts after the logos (frame ${byDefault})`);
  await start(['skip_intro=1']);
  check(await movieFrame(DEFAULT_LATEST_MOVIE) === null, `without the intro the movie is never fetched (to frame ${DEFAULT_LATEST_MOVIE})`);

  // ---- the copyright page ----
  const brightness = async (settings) => {
    await start(settings, '&env=BT3_SHOT=30');
    await until(COPYRIGHT[1] + 120, 60000);  // (past the title's first frames: 600 on)
    return JSON.parse(await ev(`JSON.stringify(Object.fromEntries(window.bt3Module.FS.readdir('port/build/shots').filter((n) => /^gpu_\\d+\\.ppm$/.test(n)).map((n) => {
      const d = window.bt3Module.FS.readFile('port/build/shots/' + n); let i = 0, nl = 0; while (nl < 3) { if (d[i++] === 10) nl++; }
      let lit = 0, total = 0; for (let k = i; k + 2 < d.length; k += 21) { total++; if (d[k] + d[k + 1] + d[k + 2] > 120) lit++; }
      return [+n.slice(4, 9), lit / total]; })))`));
  };
  const withPage = await brightness(['skip_logos=1', 'skip_intro=1']);
  const without = await brightness(['skip_logos=1', 'skip_intro=1', 'skip_copyright=1']);
  const inRange = (m, a, z) => Object.entries(m).filter(([f]) => +f >= a && +f <= z).map(([, v]) => v);
  check(inRange(withPage, ...COPYRIGHT).every((v) => v < 0.3) && inRange(withPage, COPYRIGHT[1] + 60, 1e9).some((v) => v > 0.6),
        `with the copyright page: a dim page over frames ${COPYRIGHT.join('-')}, then the bright title`);
  check(inRange(without, TITLE_BY, COPYRIGHT[1]).every((v) => v > 0.6), `without it: the bright title from frame ${TITLE_BY} on`);

  // ---- menus in 4:3 at 16:9: the title screen (a menu) with black sides ----
  const skips = ['skip_logos=1', 'skip_intro=1', 'skip_copyright=1', 'aspect_milli=1778'];
  const glSides = async (on) => {
    await start(skips.concat(on ? ['menus_43=1'] : []), '&env=BT3_GL_WINSHOT=300');
    await until(310, 60000);
    const ppm = await ev(`(() => { const d = window.bt3Module.FS.readFile('port/build/shots/window_00300.ppm'); let s = ''; for (const x of d) s += String.fromCharCode(x); return btoa(s); })()`);
    const d = Buffer.from(ppm, 'base64'), head = d.toString('latin1', 0, 32).split(/\s+/);
    let i = 0, nl = 0; while (nl < 3) { if (d[i++] === 10) nl++; }
    return sides({ w: +head[1], h: +head[2], ch: 3, px: d.subarray(i) });
  };
  let off = await glSides(false), on = await glSides(true);
  check(off.side < 0.05 && off.mid < 0.05, `WebGL 2, off: the title fills the width (black: sides ${(off.side * 100).toFixed(0)}%, middle ${(off.mid * 100).toFixed(0)}%)`);
  check(on.side > 0.95 && on.mid < 0.05, `WebGL 2, on: black sides (${(on.side * 100).toFixed(0)}%), the title in the middle (black ${(on.mid * 100).toFixed(0)}%)`);
  const canvasSides = async (on) => {
    await start(skips.concat(on ? ['menus_43=1'] : []), '&gpu=webgpu');
    await until(300, 60000);
    const r = JSON.parse(await ev(`JSON.stringify((() => { const c = document.getElementById('canvas').getBoundingClientRect(); return { x: c.x, y: c.y, w: c.width, h: c.height }; })())`));
    const shot = await b.send('Page.captureScreenshot', { format: 'png', clip: { x: r.x, y: r.y, width: r.w, height: r.h, scale: 1 } });
    return sides(decodePng(Buffer.from(shot.data, 'base64')));
  };
  off = await canvasSides(false); on = await canvasSides(true);
  check(off.side < 0.05 && off.mid < 0.05, `WebGPU, off: the title fills the width (black: sides ${(off.side * 100).toFixed(0)}%, middle ${(off.mid * 100).toFixed(0)}%)`);
  check(on.side > 0.95 && on.mid < 0.05, `WebGPU, on: black sides (${(on.side * 100).toFixed(0)}%), the title in the middle (black ${(on.mid * 100).toFixed(0)}%)`);

  await ev(`localStorage.removeItem('bt3.settings'), 0`);
  await b.close();
  console.log(failed ? `RESULT: FAIL (${failed})` : 'RESULT: PASS');
  process.exit(failed ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
