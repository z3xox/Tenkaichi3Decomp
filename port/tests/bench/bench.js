// Benchmark of the browser build on the game's attract-demo battle (BT3_DEMO): the same scene in every run (the
// game's random seed is fixed, plat_sys.c; no input). Drives Chrome / Edge through the DevTools protocol with a
// fresh throwaway profile (port/tests/lib/cdp.js), reads the per-frame lines of BT3_FRAME_LOG (gs_core.c) and the
// fight trace of BT3_TRACE (headless.c), and optionally records Chrome's CPU profile over the measured frames.
//
//   node port/tests/bench/bench.js --label <name> [--browser <exe>] [--url http://127.0.0.1:8080/index.html]
//        [--from 300] [--to 2100] [--profile] [--headless] [--env NAME=value,...] [--query page-option=value]
//        [--shots N] [--boot] [--chrome-flags name=value,...]
//
// The page and the data must be served (port/web/devserver.py with the gamedata mount). Frames [from, to) are
// measured; frames before `from` are the warm-up (loading, first textures and pipelines). Writes
// port/build/bench/<label>.{json,frames.csv,trace.txt,log.txt[,cpuprofile]} and prints a summary.
'use strict';
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { launch, evaluate, sleep } = require('../lib/cdp.js');

// A fight frame lasts two vertical blanks of the PS2's 59.94 Hz display (the game paces itself so).
const VBLANK_US = 1e6 / 59.94;
const FIGHT_FRAME_BUDGET_US = 2 * VBLANK_US;
const PROFILE_SAMPLING_US = 200;  // Chrome's sampling interval for --profile
const POLL_MS = 250;              // how often the page's log queue is collected
const TRACE_EVERY_VBLANKS = 60;   // BT3_TRACE: one fight-state line per second

const args = Object.fromEntries(process.argv.slice(2).reduce((acc, a, i, all) => {
  if (a.startsWith('--')) acc.push([a.slice(2), all[i + 1] && !all[i + 1].startsWith('--') ? all[i + 1] : true]);
  return acc;
}, []));
const label = args.label || 'run';
const from = Number(args.from || 300), to = Number(args.to || 2100);
// --shots N: the shown render target every N frames of the window (the back ends' BT3_SHOT), copied out of the
// engine's in-memory file system at the end, for picture comparisons (port/tests/bench/compare_shots.js).
const SHOT_DIR = 'port/build/shots';  // where the back ends write them (gs_gl.c, gs_gpu.c), relative to the engine's cwd
const shotEvery = args.shots ? Number(args.shots) : 0;
// --boot: the game's own start (logos, the opening movie, the title) instead of the attract-demo battle
// --chrome-flags: switches for the browser itself, without their dashes, e.g. use-angle=swiftshader,use-webgpu-adapter=
// swiftshader,enable-unsafe-swiftshader to draw both APIs with the same software rasterizer (picture comparisons free of
// the drivers' differences)
const chromeFlags = args['chrome-flags'] && args['chrome-flags'] !== true ? args['chrome-flags'].split(',').map((f) => `--${f}`) : [];
const env = (args.boot ? [] : ['BT3_DEMO=1']).concat(['BT3_FRAME_LOG=1', `BT3_TRACE=${TRACE_EVERY_VBLANKS}`])
  .concat(shotEvery ? [`BT3_SHOT=${shotEvery}`, `BT3_SHOT_FROM=${from}`, `BT3_SHOT_TO=${to - 1}`] : [])
  .concat(args.env && args.env !== true ? args.env.split(',') : []);
const url = `${args.url || 'http://127.0.0.1:8080/index.html'}?autostart=1${args.query ? '&' + args.query : ''}&env=${env.join(',')}`;
const outDir = path.join(__dirname, '../../build/bench');

// One BT3_FRAME_LOG line (gs_core.c): "frame <frame> <vblank> <work> <game> <list> <tex> <pipe> <submit>" (us),
// plus the time the page logged it (ms, performance.now).
function parseFrame(line, t) {
  const [frame, vblank, work, game, list, tex, pipe, submit] = line.split(' ').slice(1).map(Number);
  return { frame, vblank, work, game, list, tex, pipe, submit, t };
}

function stats(xs) {
  const s = [...xs].sort((a, b) => a - b), n = s.length;
  const q = (p) => s[Math.min(n - 1, Math.ceil(p * n) - 1)];
  return { n, mean: s.reduce((a, b) => a + b, 0) / n, p50: q(0.5), p95: q(0.95), p99: q(0.99), max: s[n - 1],
           over: s.filter((v) => v > FIGHT_FRAME_BUDGET_US).length };
}

// Self time per function from a CDP profile (sampled), grouped in categories.
function summarize(profile) {
  const self = new Map(), byId = new Map(profile.nodes.map((n) => [n.id, n]));
  profile.samples.forEach((id, i) => {
    const n = byId.get(id), dt = profile.timeDeltas[i] || 0;
    const key = (n.callFrame.functionName || '(anonymous)') + (n.callFrame.url.endsWith('.wasm') ? '' : ` [${path.basename(n.callFrame.url) || 'native'}]`);
    self.set(key, (self.get(key) || 0) + dt);
  });
  const total = [...self.values()].reduce((a, b) => a + b, 0);
  const cat = (k) =>
    /^\((idle|program|garbage collector)\)/.test(k) ? k.replace(/ \[.*/, '') :
    /fpcast-emu|byn\$fpcast/.test(k) ? 'function-pointer cast thunks' :
    /^(__(add|sub|mul|div|neg)sf3|__(lt|le|gt|ge|eq|ne|unord)sf2|__fix\w*sf\w*|__float\w*sf|Port_Fast\w+|RefVu0_\w+|Sf_\w+|fpu_add|mul_core\w*|add_core\w*|divide)\b/.test(k) ? 'PS2 float routines' :
    /^(Port_(sinf|cosf|tanf|atan2f|atanf|asinf|acosf|sqrtf|powf|floorf|ceilf|fmodf)|__ieee754|__kernel|Port_scalbnf)/.test(k) ? 'PS2 maths library' :
    /\[bt3\.js\]/.test(k) && /^_?(gl|emscripten_gl|webgl|wgpu)/i.test(k) ? 'graphics API calls (JS glue)' :
    /\[bt3\.js\]/.test(k) ? 'other JS glue (bt3.js)' :
    /\[(native|)\]$/.test(k) ? 'browser native' :
    /^(Gs|gs_|GsGpu|GsVu1|GsDraw|Port_Gs|vif|run_chain|unpack|hle_|vu1_|draw_|record_|tex_|pipe|target_|gl_|wg_|apply_|set_params|present)/.test(k) ? 'renderer (GS front end, VU1, back end)' :
    /^(memcpy|memset|memmove|malloc|free|dlmalloc|dlfree|strlen|strcmp|sprintf|printf|vfprintf|fwrite|__stdio)/.test(k) ? 'C library' :
    'game code and the rest';
  const cats = new Map();
  for (const [k, v] of self) cats.set(cat(k), (cats.get(cat(k)) || 0) + v);
  const top = [...self].sort((a, b) => b[1] - a[1]).slice(0, 40).map(([k, v]) => ({ fn: k, ms: v / 1000, pct: 100 * v / total }));
  return { totalMs: total / 1000, categories: [...cats].sort((a, b) => b[1] - a[1]).map(([k, v]) => ({ cat: k, pct: 100 * v / total })), top };
}

// Copies the screenshots out of the engine's file system into port/build/bench/<label>.shots/. Returns their hashes.
async function copyShots(send) {
  const dir = path.join(outDir, `${label}.shots`);
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir, { recursive: true });
  const names = await evaluate(send, `window.bt3Module.FS.readdir(${JSON.stringify(SHOT_DIR)}).filter((n) => n.endsWith('.ppm'))`);
  const hashes = {};
  for (const name of names.sort()) {
    const base64 = await evaluate(send, `new Promise((done) => {
      const reader = new FileReader();
      reader.onload = () => done(reader.result.slice(reader.result.indexOf(',') + 1));
      reader.readAsDataURL(new Blob([window.bt3Module.FS.readFile(${JSON.stringify(SHOT_DIR + '/')} + ${JSON.stringify(name)})]));
    })`);
    const bytes = Buffer.from(base64, 'base64');
    fs.writeFileSync(path.join(dir, name), bytes);
    hashes[name] = crypto.createHash('sha1').update(bytes).digest('hex').slice(0, 16);
  }
  return hashes;
}

function write(result, frames, trace, log, profile) {
  fs.mkdirSync(outDir, { recursive: true });
  fs.writeFileSync(path.join(outDir, `${label}.json`), JSON.stringify(result, null, 1));
  fs.writeFileSync(path.join(outDir, `${label}.trace.txt`), trace.join('\n') + '\n');
  if (profile) fs.writeFileSync(path.join(outDir, `${label}.cpuprofile`), JSON.stringify(profile));
  fs.writeFileSync(path.join(outDir, `${label}.frames.csv`), 'frame,vblank,work_us,game_us,list_us,tex_us,pipe_us,submit_us,t_ms\n' +
    frames.map((f) => [f.frame, f.vblank, f.work, f.game, f.list, f.tex, f.pipe, f.submit, f.t].join(',')).join('\n') + '\n');
  fs.writeFileSync(path.join(outDir, `${label}.log.txt`), log.join('\n') + '\n');
}

function print(result) {
  const f = (s) => s ? `mean ${(s.mean / 1000).toFixed(2)}  p50 ${(s.p50 / 1000).toFixed(2)}  p95 ${(s.p95 / 1000).toFixed(2)}  p99 ${(s.p99 / 1000).toFixed(2)}  max ${(s.max / 1000).toFixed(2)} ms  over ${s.over}/${s.n}` : '-';
  console.log(`${label}: ${result.browser}; gpu ${result.gpu}; frames ${from}..${to}${result.stopped ? '; STOPPED: ' + result.stopped : ''}`);
  console.log(`  work   ${f(result.work)}\n  game   ${f(result.game)}\n  list   ${f(result.list)}\n  submit ${f(result.submit)}`);
  console.log(`  wall   ${result.fps ? result.fps.toFixed(1) : '-'} frames/s over ${result.wallSeconds ? result.wallSeconds.toFixed(1) : '-'} s; ` +
              `${result.vblanksPerFrame ? result.vblanksPerFrame.toFixed(2) : '-'} vblanks per frame; interval ${f(result.interval)}`);
  console.log(`  trace: ${result.traceLines} lines, hash ${result.traceHash}; GL errors logged: ${result.glErrors} ${result.glErrorCounts}`);
  if (result.shots) console.log(`  shots: ${Object.keys(result.shots).length} saved in ${label}.shots/`);
  if (result.profile) {
    console.log('  profile (self time):');
    for (const c of result.profile.categories) console.log(`    ${c.pct.toFixed(1).padStart(5)}%  ${c.cat}`);
    console.log('  top functions:');
    for (const t of result.profile.top.slice(0, 25)) console.log(`    ${t.pct.toFixed(1).padStart(5)}%  ${t.fn}`);
  }
}

(async () => {
  const browser = await launch({ browser: args.browser, headless: !!args.headless, extraArgs: chromeFlags });
  const { send } = browser;
  const frames = [], trace = [], log = [];
  let profiling = false, stopped = null, done = false;

  // The page queues its log lines with their time (index.html, window.bt3Out); they are collected a few times a
  // second, so that the measurement does not go through the console inside the measured frames.
  let shotDirMade = false;
  async function handle(t, line) {
    if (line.startsWith('frame ')) {
      const f = parseFrame(line, t);
      frames.push(f);
      if (shotEvery && !shotDirMade) {  // (the engine runs from here on; its first screenshot is at `from`)
        await evaluate(send, `window.bt3Module.FS.mkdirTree(${JSON.stringify(SHOT_DIR)})`);
        shotDirMade = true;
      }
      if (f.frame >= from && !profiling && args.profile) {
        profiling = true;
        await send('Profiler.setSamplingInterval', { interval: PROFILE_SAMPLING_US });
        await send('Profiler.start');
      }
      if (f.frame >= to) done = true;
      return;
    }
    if (line.startsWith('vb ')) trace.push(line);
    else log.push(line);
    if (line.startsWith('STOPPED')) { stopped = line; done = true; }
  }

  await send('Runtime.enable');
  await send('Profiler.enable');
  await send('Page.enable');
  const t0 = Date.now(), deadline = t0 + Number(args.timeout || 600) * 1000;
  await send('Page.navigate', { url });
  while (!done) {
    if (Date.now() > deadline) { stopped = `timeout (no frame reached ${to})`; break; }
    await sleep(POLL_MS);
    let queued;
    try {
      queued = JSON.parse(await evaluate(send, 'JSON.stringify(window.bt3Out ? window.bt3Out.splice(0) : [])') || '[]');
    } catch (e) { continue; }  // (the page is being replaced)
    for (const [t, line] of queued) {
      if (!done) await handle(t, line);
    }
  }
  const profile = profiling ? (await send('Profiler.stop')).profile : null;
  const shots = shotEvery && shotDirMade ? await copyShots(send) : null;
  const win = frames.filter((f) => f.frame >= from && f.frame < to);
  const first = win[0], last = win[win.length - 1];
  const result = {
    label, url, browser: browser.version.Browser, when: new Date().toISOString(), seconds: (Date.now() - t0) / 1000, stopped,
    gpu: (log.find((l) => l.startsWith('gpu: ')) || '').slice(5), window: [from, to],
    work: win.length ? stats(win.map((f) => f.work)) : null, game: win.length ? stats(win.map((f) => f.game)) : null,
    list: win.length ? stats(win.map((f) => f.list)) : null, submit: win.length ? stats(win.map((f) => f.submit)) : null,
    // wall clock over the window: real frames per second, and the time between frames
    wallSeconds: win.length > 1 ? (last.t - first.t) / 1000 : null,
    fps: win.length > 1 ? (win.length - 1) / ((last.t - first.t) / 1000) : null,
    interval: win.length > 1 ? stats(win.slice(1).map((f, i) => (f.t - win[i].t) * 1000)) : null,
    vblanksPerFrame: win.length > 1 ? (last.vblank - first.vblank) / (win.length - 1) : null,
    traceLines: trace.length, traceHash: crypto.createHash('sha1').update(trace.join('\n')).digest('hex').slice(0, 16),
    // (GL errors are only seen with BT3_GS_VERBOSE, once per frame, or with --query gldebug=1, per failing call:
    //  the first dozen calls listed, then a running count every 5 s)
    glErrors: log.filter((l) => /gl: frame error|^gldebug: [a-zA-Z]+\(/.test(l)).length,
    glErrorCounts: log.filter((l) => l.startsWith('gldebug: counts')).pop() || '',
    profile: profile ? summarize(profile) : null,
    shots,  // { file name: sha1 } of the screenshots, saved in port/build/bench/<label>.shots/
  };
  write(result, frames, trace, log, profile);
  print(result);
  await browser.close();
  process.exit(stopped ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });
