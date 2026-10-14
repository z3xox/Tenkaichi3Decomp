// Test of the game's sound in the browser (SDL's audio streams through Emscripten into Web Audio: gs/snd_adx.c for
// music, gs/snd_se.c for effects and voices), in Chrome through the DevTools protocol:
//   node port/tests/web/audio.js
// Every connection to the page's audio output also feeds an analyser (AudioNode.prototype.connect is wrapped before
// the page loads), whose level is sampled once a second. Checks, with the volume settings put in the page's kept
// settings file before the start:
//   - the attract-demo battle (BT3_DEMO), defaults: sound comes out;
//   - the battle with every volume at 0: exact silence (so the measurement is the game's own output, and the settings
//     reach it);
//   - the battle with music at 0: sound - the effects and voices path (the attract battle has no music: measured, its
//     level is the same with music on or off);
//   - the title screen (the game's own start, logos / intro / copyright page skipped) with music alone: sound - the
//     music path (nothing else can sound: with every volume at 0 the title is silent, checked too; the title is not
//     silent with music at 0 alone - its announcer's voice plays as it appears).
'use strict';
const { launch, evaluate, sleep } = require('../lib/cdp.js');

const SAMPLE_FROM_S = 9, SAMPLE_TO_S = 24;  // the battle is under way from about 8 s (frame ~250) on; the title from ~4 s
const DEMO = '&env=BT3_DEMO=1', TITLE = '';
const SKIPS = ['skip_logos=1', 'skip_intro=1', 'skip_copyright=1'];
const AUDIBLE_RMS = 0.01;                    // mean level of the samples counted as sound (measured: 0.05..0.26)
let failed = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failed++; };

const TAP = `
  window.audioTap = { nodes: [], ctx: null };
  const connect = AudioNode.prototype.connect;
  AudioNode.prototype.connect = function (dst, ...rest) {
    if (dst instanceof AudioDestinationNode) {
      const ctx = dst.context;
      if (!ctx.__tap) { ctx.__tap = ctx.createAnalyser(); ctx.__tap.fftSize = 2048; window.audioTap.nodes.push(ctx.__tap); window.audioTap.ctx = ctx; }
      connect.call(this, ctx.__tap);
    }
    return connect.call(this, dst, ...rest);
  };
  window.audioLevel = () => {
    let peak = 0, sum = 0, n = 0; const buf = new Float32Array(2048);
    for (const a of window.audioTap.nodes) { a.getFloatTimeDomainData(buf); for (const v of buf) { peak = Math.max(peak, Math.abs(v)); sum += v * v; n++; } }
    return { rms: n ? Math.sqrt(sum / n) : 0, peak, state: window.audioTap.ctx ? window.audioTap.ctx.state : 'none' };
  };`;

(async () => {
  const b = await launch({ headless: true, extraArgs: ['--autoplay-policy=no-user-gesture-required'] });
  const ev = (e) => evaluate(b.send, e);
  await b.send('Page.enable');
  const run = async (settings, query) => {
    const { identifier } = await b.send('Page.addScriptToEvaluateOnNewDocument', {
      source: `localStorage.setItem('bt3.settings', ${JSON.stringify(settings.map((s) => s + '\n').join(''))});` + TAP });
    await b.send('Page.navigate', { url: 'http://127.0.0.1:8080/index.html?autostart=1' + query });
    const samples = [];
    let state = 'none';
    for (let t = 1; t <= SAMPLE_TO_S; t++) {
      await sleep(1000);
      const s = JSON.parse(await ev(`JSON.stringify(window.audioLevel())`));
      state = s.state;
      if (t >= SAMPLE_FROM_S) samples.push(s);
    }
    await b.send('Page.removeScriptToEvaluateOnNewDocument', { identifier });
    const mean = samples.reduce((a, s) => a + s.rms, 0) / samples.length, peak = Math.max(...samples.map((s) => s.peak));
    return { mean, peak, state, text: `mean level ${mean.toFixed(4)}, peak ${peak.toFixed(3)}, audio ${state}` };
  };

  let r = await run([], DEMO);
  check(r.state === 'running' && r.mean > AUDIBLE_RMS, `battle, defaults: sound (${r.text})`);
  r = await run(['music=0', 'effects=0', 'voices=0', 'stage_sounds=0'], DEMO);
  check(r.state === 'running' && r.peak === 0, `battle, every volume at 0: silence (${r.text})`);
  r = await run(['music=0'], DEMO);
  check(r.mean > AUDIBLE_RMS, `battle, music at 0 (effects / voices / stage sounds on): sound (${r.text})`);
  r = await run(SKIPS.concat(['effects=0', 'voices=0', 'stage_sounds=0']), TITLE);
  check(r.mean > AUDIBLE_RMS, `title, music alone: sound (${r.text})`);
  r = await run(SKIPS.concat(['music=0', 'effects=0', 'voices=0', 'stage_sounds=0']), TITLE);
  check(r.state === 'running' && r.peak === 0, `title, every volume at 0: silence (${r.text})`);

  await b.close();
  console.log(failed ? `RESULT: FAIL (${failed})` : 'RESULT: PASS');
  process.exit(failed ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
