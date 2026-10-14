// What reaches the screen at each smooth-motion setting (the engine's in-between pictures, gs_draw.c "interp"), and
// whether the game keeps its speed. Runs the attract-demo battle (BT3_DEMO) and, once the fight is under way, sets
// each setting in turn for a measuring window.
//
//   node port/tests/bench/present_rate.js [--query gpu=webgpu] [--chrome-flags name=value,...] [--window]
//        [--settings 0,1,2,3] [--seconds 6] [--env NAME=value,...]
//
// A canvas shows a new picture at most once per display refresh, and only when the page's thread has yielded since
// it was drawn. So the page counts, per refresh (requestAnimationFrame), whether at least one picture was presented
// since the last one (WebGL 2: the default framebuffer bound; WebGPU: the canvas's current texture taken). Reported:
//   refresh/s   the display's refreshes (rAF callbacks) per second: the most pictures that can be shown
//   shown/s     refreshes that had a new picture: pictures that reached the screen
//   between/s   in-between pictures the engine made (gInterpShown)
//   at 60 Hz    pictures a 60 Hz display would show: the 16.7 ms intervals that hold at least one present (a model
//               from the presents' times; the same model for this display's own rate is printed next to its
//               measured shown/s, to check it). No 60 Hz display or Chrome switch for one was available here.
//   gap         ms between pictures that reached the screen (this display), median / p95 / longest: more pictures
//               only look smoother if they also come evenly (no longer gaps than without)
//   ticks/s     game steps drawn (gGsFrame); game speed: vertical blanks per second against the PS2's 59.94
//   work        the engine's time per tick (BT3_FRAME_LOG: game + building and submitting its pictures), ms
'use strict';
const path = require('path');
const { launch, evaluate, sleep } = require('../lib/cdp.js');

const args = Object.fromEntries(process.argv.slice(2).reduce((acc, a, i, all) => {
  if (a.startsWith('--')) acc.push([a.slice(2), all[i + 1] && !all[i + 1].startsWith('--') ? all[i + 1] : true]);
  return acc;
}, []));
const settings = (args.settings || '0,1,2,3').split(',').map(Number);
const seconds = Number(args.seconds || 6);
const WARMUP_MS = 22000;     // into the demo's fight (loading, the intro)
const SETTLE_MS = 1500;      // after a change: the engine's cost guard settles
const PS2_VBLANKS = 59.94;
const chromeFlags = args['chrome-flags'] && args['chrome-flags'] !== true ? args['chrome-flags'].split(',').map((f) => `--${f}`) : [];

// Installed before the page's own scripts: counts refreshes and the ones with a new picture.
const COUNTER = `(() => {
  const m = window.__present = { raf: 0, shown: 0, last: -1, times: [] };
  (function loop() { m.raf++; requestAnimationFrame(loop); })();
  m.shownAt = [];
  const mark = () => {
    if (m.last !== m.raf) {
      m.shown++; m.last = m.raf;
      m.shownAt.push(performance.now());
      if (m.shownAt.length > 4000) m.shownAt.splice(0, 2000);
    }
    m.times.push(performance.now());
    if (m.times.length > 20000) m.times.splice(0, 10000);  // (bounded: the window is a few seconds)
  };
  const bf = WebGL2RenderingContext.prototype.bindFramebuffer;
  WebGL2RenderingContext.prototype.bindFramebuffer = function (t, fb) { if (fb === null && t === this.FRAMEBUFFER) mark(); return bf.call(this, t, fb); }; // (the present: gs_gl.c binds the window's framebuffer)
  if (window.GPUCanvasContext) {
    const gct = GPUCanvasContext.prototype.getCurrentTexture;
    GPUCanvasContext.prototype.getCurrentTexture = function () { mark(); return gct.call(this); };
  }
})();`;

(async () => {
  const b = await launch({ headless: !args.window, extraArgs: chromeFlags });
  const ev = (e) => evaluate(b.send, e);
  await b.send('Page.enable');
  await b.send('Page.addScriptToEvaluateOnNewDocument', { source: COUNTER });
  await b.send('Page.navigate', { url: `http://127.0.0.1:8080/index.html?autostart=1${args.query ? '&' + args.query : ''}&env=BT3_DEMO=1,BT3_FRAME_LOG=1${args.env && args.env !== true ? ',' + args.env : ''}` });
  await sleep(WARMUP_MS);
  const get = (n) => ev(`window.bt3Module.ccall('Bt3Ui_Get','number',['string'],['${n}'])`);
  const set = (n, v) => ev(`window.bt3Module.ccall('Bt3Ui_Set','number',['string','number'],['${n}', ${v}])`);
  const snap = async () => JSON.parse(await ev(`JSON.stringify({ raf: __present.raf, shown: __present.shown, t: performance.now(),
    frame: window.bt3Module.ccall('Bt3Ui_Get','number',['string'],['frame']), vb: window.bt3Module.ccall('Bt3Ui_Get','number',['string'],['vblanks']),
    between: window.bt3Module.ccall('Bt3Ui_Get','number',['string'],['interp_shown']), log: window.bt3Out.length })`));
  console.log(`renderer ${args.query || 'gpu=gl'}; ${args.window ? 'window' : 'headless'}${chromeFlags.length ? '; ' + chromeFlags.join(' ') : ''}`);
  console.log('setting  refresh/s  shown/s (model)  at 60 Hz  gap med/p95/max ms  between/s  ticks/s  speed  work mean / p95 / p99 ms');
  for (const v of settings) {
    await set('interp', v);
    await sleep(SETTLE_MS);
    const a = await snap();
    await sleep(seconds * 1000);
    const z = await snap();
    const s = (z.t - a.t) / 1000;
    // frame lines: "frame <frame> <vblank> <work> ..." (gs_core.c, microseconds)
    const work = JSON.parse(await ev(`JSON.stringify(window.bt3Out.slice(${a.log}, ${z.log}).map(([, l]) => l)
      .filter((l) => l.startsWith('frame ')).map((l) => Number(l.split(' ')[3]) / 1000))`)).sort((x, y) => x - y);
    const q = (p) => work.length ? work[Math.min(work.length - 1, Math.floor(p * work.length))].toFixed(1) : '-';
    const mean = work.length ? (work.reduce((x, y) => x + y, 0) / work.length).toFixed(1) : '-';
    const f = (n) => ((n) / s).toFixed(1).padStart(7);
    // the model: a display at `hz` shows one new picture per refresh interval that holds a present
    const model = async (hz) => Number(await ev(`new Set(__present.times.filter((t) => t > ${a.t} && t <= ${z.t})
      .map((t) => Math.floor(t * ${hz} / 1000))).size`)) / s;
    const own = (z.raf - a.raf) / s;
    const gaps = JSON.parse(await ev(`JSON.stringify((() => { const t = __present.shownAt.filter((x) => x > ${a.t} && x <= ${z.t});
      return t.slice(1).map((x, i) => x - t[i]); })())`)).sort((x, y) => x - y);
    const g = (p) => gaps.length ? gaps[Math.min(gaps.length - 1, Math.floor(p * gaps.length))].toFixed(0) : '-';
    const gap = `${g(0.5)}/${g(0.95)}/${gaps.length ? gaps[gaps.length - 1].toFixed(0) : '-'}`.padStart(17);
    console.log(`${String(v).padStart(7)}  ${f(z.raf - a.raf)}  ${f(z.shown - a.shown)} (${(await model(own)).toFixed(1)})  ${(await model(60)).toFixed(1).padStart(8)}  ${gap}  ` +
                `${f(z.between - a.between)}  ${f(z.frame - a.frame)}  ` +
                `${String(Math.round((z.vb - a.vb) / s / PS2_VBLANKS * 100)).padStart(4)}%  ${mean} / ${q(0.95)} / ${q(0.99)}`);
  }
  await set('interp', 0);
  await b.close();
})().catch((e) => { console.error(e); process.exit(1); });
