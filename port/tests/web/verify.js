// Test of what the browser version says about the check that the game data is the original disc's
// (port/src/web/web_verify.c; natively port/src/plat_verify.c), in Chrome through the DevTools protocol:
//   node port/tests/web/verify.js
// Checks: the check is not run and that is said, once, with the reason, in the log, the crash report's context and
// the dashboard; no "modified game data" claim is made; BT3_VERIFY=0 is reported as turned off.
'use strict';
const { launch, evaluate, sleep } = require('../lib/cdp.js');

const START_MS = 15000;  // the attract demo is running by then: the game has passed the point where the check starts
const DASHBOARD_MS = 2000;
let failed = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failed++; };

(async () => {
  const b = await launch({ headless: true });
  const ev = (e) => evaluate(b.send, e);
  const run = async (env) => {
    await b.send('Page.navigate', { url: `http://127.0.0.1:8080/index.html?autostart=1&env=BT3_DEMO=1${env}` });
    await sleep(START_MS);
    const lines = JSON.parse(await ev(`JSON.stringify(window.bt3Out.map(([, l]) => l).filter((l) => /game data|modified/.test(l)))`));
    const state = await ev(`window.bt3Module.ccall('Bt3Ui_Get', 'number', ['string'], ['data_check'])`);
    const context = await ev(`window.bt3Module.UTF8ToString(window.bt3Module._Port_CrashContext())`);
    await ev(`document.getElementById('settings-open').click(); document.getElementById('dev').open = true; 0`);
    await sleep(DASHBOARD_MS);
    const row = await ev(`(() => { const dt = [...document.querySelectorAll('#dev-dashboard dt')].find((d) => d.textContent.trim() === 'Data check'); return dt ? dt.nextElementSibling.textContent : null; })()`);
    return { lines, state, context, row };
  };

  let r = await run('');
  check(r.state === 4, `state: not run in the browser (${r.state})`);
  check(r.lines.filter((l) => l.startsWith('bt3: game data: not checked against the original disc (the browser version cannot run the check')).length === 1,
        'log: "not checked", once, with the reason');
  check(!r.lines.some((l) => /modified game data/.test(l)), 'log: no claim that the data is modified');
  check(/^stage: the game is running \(vertical blank \d+\); game data: not checked \(browser version\)$/.test(r.context),
        `crash report context: the game running, the data not checked (${r.context})`);
  check(/^not checked: the browser version cannot run the check/.test(r.row || ''), `dashboard: ${r.row}`);

  r = await run(',BT3_VERIFY=0');
  check(r.state === 3, `BT3_VERIFY=0: state turned off (${r.state})`);
  check(r.lines.some((l) => l === 'bt3: game data: check turned off (BT3_VERIFY=0)'), 'log: turned off');
  check(!r.lines.some((l) => l.includes('not checked against the original disc')), 'log: no "not checked" line when turned off');
  check(r.row === 'turned off (BT3_VERIFY=0)', `dashboard: ${r.row}`);

  await b.close();
  console.log(failed ? `RESULT: FAIL (${failed})` : 'RESULT: PASS');
  process.exit(failed ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
