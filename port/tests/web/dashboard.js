// Test of the Developer section's live numbers and its diagnostics export (port/web/dashboard.js), in Chrome through the
// DevTools protocol, on the attract-demo battle:
//   node port/tests/web/dashboard.js [--query gpu=webgpu]
// Checks: every row has a value; the graph is drawn; the present counter is installed only while the section is open
// and removed when the panel closes; the export is the versioned schema, holds tick and (where the GPU is timed) GPU
// samples, the settings file, error lines only, and nothing of the saves or the game's data; ticks from while the tab
// was hidden (another tab in front) are left out of the statistics.
'use strict';
const { launch, evaluate, sleep } = require('../lib/cdp.js');

const args = Object.fromEntries(process.argv.slice(2).reduce((acc, a, i, all) => {
  if (a.startsWith('--')) acc.push([a.slice(2), all[i + 1] && !all[i + 1].startsWith('--') ? all[i + 1] : true]);
  return acc;
}, []));
const WARMUP_MS = 18000, OPEN_MS = 11000, HIDDEN_MS = 4000;
let failed = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failed++; };

(async () => {
  const b = await launch({ headless: !args.window });
  const ev = (e) => evaluate(b.send, e);
  await b.send('Page.navigate', { url: `http://127.0.0.1:8080/index.html?autostart=1${args.query ? '&' + args.query : ''}&env=BT3_DEMO=1` });
  await sleep(WARMUP_MS);
  const webgpu = (args.query || '').includes('webgpu');
  const proto = webgpu ? 'GPUCanvasContext.prototype.getCurrentTexture' : 'WebGL2RenderingContext.prototype.bindFramebuffer';
  const native = () => ev(`${proto}.toString().includes('[native code]')`);

  check(await native(), 'closed: the present counter is not installed');
  await ev(`document.getElementById('settings-open').click(); document.getElementById('dev').open = true; 0`);
  await sleep(OPEN_MS);
  check(!(await native()), 'open: the present counter is installed');
  const rows = JSON.parse(await ev(`JSON.stringify(Object.fromEntries([...document.querySelectorAll('#dev-dashboard dt')].map((dt) => [dt.textContent.trim(), dt.nextElementSibling.textContent])))`));
  for (const k of ['Renderer', 'Pictures shown', 'Game ticks', 'Work (mean / p95 / p99)', 'GPU time', 'Long frames', 'Memory', 'Game data']) {
    check(rows[k] !== undefined && rows[k] !== '—' && !/NaN|undefined/.test(rows[k]), `row "${k}": ${rows[k]}`);
  }
  check(/\d+(\.\d)? \/ \d/.test(rows['GPU time'] || ''), 'GPU time measured (timestamp / timer query)');
  check(/game speed 9\d|game speed 10\d/.test(rows['Game ticks'] || ''), 'game speed near 100 %');
  check(Number(await ev(`(() => { const c = document.querySelector('#dev-dashboard canvas.graph'); const p = c.getContext('2d').getImageData(0, 0, c.width, c.height).data; let n = 0; for (let i = 3; i < p.length; i += 4) if (p[i]) n++; return n; })()`)) > 500, 'the graph is drawn');

  const d = JSON.parse(await ev(`JSON.stringify(settings.dashboard.diagnostics())`));
  check(d.schema === 'bt3-diagnostics' && d.version === 1, 'export: schema bt3-diagnostics, version 1');
  check(d.performance.ticks.length > 300 && d.performance.ticksColumns.length === d.performance.ticks[0].length, `export: ${d.performance.ticks.length} tick samples`);
  check(d.performance.gpu.length > 100, `export: ${d.performance.gpu.length} GPU samples`);
  check(d.settings.current && d.settings.current.scale >= 1 && 'game_speed' in d.settings.current,
        `export: the current settings (${Object.keys(d.settings.current || {}).length}; file ${d.settings.file === null ? 'not written yet' : 'too'})`);
  check(d.errors.every((l) => /error|FAIL|STOPPED|not implemented|abort/i.test(l)), 'export: only error lines of the log');
  const text = JSON.stringify(d);
  check(!/\.(sav|bin|iso|afs)\b|SLUS_|gamedata\//i.test(text.replace(d.page.query, '')), 'export: no save or game-data names or contents');
  check(text.length < 2_000_000, `export: ${(text.length / 1024).toFixed(0)} KB`);
  check(await ev(`(() => { let made = null; const o = HTMLAnchorElement.prototype.click; HTMLAnchorElement.prototype.click = function () { made = this.download; }; settings.dashboard.exportDiagnostics(); HTMLAnchorElement.prototype.click = o; return /^bt3-diagnostics-.*\\.json$/.test(made); })()`), 'export: a .json download, nothing sent');

  // another tab in front: this one is hidden and throttled; its ticks must be left out
  const other = await b.send('Target.createTarget', { url: 'about:blank' });
  await sleep(HIDDEN_MS);
  const hiddenSeen = await ev(`document.hidden`);
  await b.send('Target.closeTarget', { targetId: other.targetId });
  await sleep(1500);
  const left = Number(await ev(`settings.dashboard.summary(settings.dashboard.ring()).hiddenTicksLeftOut`));
  check(hiddenSeen && left > 0, `hidden tab: document.hidden ${hiddenSeen}, ${left} ticks left out`);

  await ev(`document.getElementById('settings-close').click(); 0`);
  await sleep(500);
  check(await native(), 'closed again: the present counter is removed');
  check(await ev(`settings.dashboard.timer === 0`), 'closed again: no update timer');
  await b.close();
  console.log(failed ? `RESULT: FAIL (${failed})` : 'RESULT: PASS');
  process.exit(failed ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
