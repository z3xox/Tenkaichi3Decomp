// The Developer section's live performance numbers and the diagnostics export (port/web/settings.js mounts it).
//
// Sources, and what each number is:
//   engine ticks  port/src/web/web_ui.c keeps one sample per game tick in a fixed ring (Bt3Ui_PerfRing), the values
//                 gs_core.c measures: work = the tick's CPU time without the wait for the vertical blank; game = of that,
//                 the game's own code (the simulation); drawing = the display list walked plus the renderer's frame;
//                 submit = of drawing, the renderer's replay and submission (GsGpu_FrameEnd: CPU time, not GPU time).
//   GPU time      the GPU's own time per frame where the browser lets a page measure it: WebGPU timestamp-query (the
//                 first pass's start to the present pass's end), WebGL 2 EXT_disjoint_timer_query_webgl2 (a TIME_ELAPSED
//                 query around the frame's commands). Browsers round these on purpose; the two APIs measure differently.
//   shown         pictures that reached the screen: display refreshes (requestAnimationFrame) with a new picture
//                 presented since the last one, counted only while this section is open.
//   memory        JavaScript heap: Chrome's performance.memory (rounded by the browser); measureUserAgentSpecificMemory
//                 on request (cross-origin isolated pages only, takes a while). WebAssembly: the size of the engine's
//                 linear memory (reserved, not all of it in use). GPU: no browser API reports it; with WebGPU the
//                 engine's own sum of the sizes of the textures and buffers it asked for (an estimate), with WebGL 2
//                 nothing (not tracked).
// Bounded: the engine's ring holds 1024 ticks; the page keeps no history of its own beyond the shown-picture times
// (SHOWN_KEPT). Nothing runs while the section is closed (no sampling, no wrapped calls).
'use strict';

const DIAGNOSTICS_SCHEMA = 'bt3-diagnostics';
const DIAGNOSTICS_VERSION = 1;
const TICK_BUDGET_MS = 2 * 1000 / 59.94;   // a fight tick lasts two vertical blanks of the PS2's display
const WINDOW_S = 10;                       // the measuring window of the statistics
const UPDATE_MS = 500;                     // text and graph updates
const GRAPH_TICKS = 300;                   // ticks in the graph (10 s at 30 a second)
const SHOWN_KEPT = 4096;                   // shown-picture times kept (about 20 s at 200 a second)
const LONG_TASK_MS = 50;                   // the browser's own threshold for a long animation frame
// The check that the game data is the original disc's, by the engine's state (web_verify.c, plat_verify.c)
const DATA_CHECK = {
  [-1]: 'not started yet',
  0: 'running',
  1: 'the original disc’s',
  2: 'modified (see the log)',
  3: 'turned off (BT3_VERIFY=0)',
  4: 'not checked: the browser version cannot run the check (it reads every file of the data folder, about 3 GB, and needs a thread)',
};
const PS2_VBLANKS_PER_S = 59.94;
const LOG_ERROR = /error|FAIL|STOPPED|not implemented|abort/i;
const LOG_ERRORS_KEPT = 200;
const HIDDEN_KEPT = 32;                    // hidden-tab intervals remembered (older ones fall out of the window anyway)

class Bt3Dashboard {
  constructor({ root, renderer, data, log, getLog, getSettings }) {
    this.root = root;
    this.renderer = renderer;
    this.data = data;
    this.log = log;
    this.getLog = getLog;  // () => the page's log lines (for the errors in the export)
    this.getSettings = getSettings;  // () => { name: the engine's current value } of every setting the panel shows
    this.mod = null;
    this.gpuName = '';
    this.timer = 0;
    this.shown = { times: [], raf: 0, last: -1, unwrap: null, loop: 0 };
    this.longFrames = { count: 0, observer: null };
    this.cost = { ms: 0, since: performance.now() };  // the dashboard's own time
    // When the tab is hidden the browser throttles the page: those ticks are not the game's own pace and are left out
    // of the statistics (the intervals [start, end] it was hidden; end null while it still is).
    this.hidden = document.hidden ? [{ start: performance.now(), end: null }] : [];
    this.build();
    document.addEventListener('visibilitychange', () => {
      const now = performance.now();
      if (document.hidden) this.hidden.push({ start: now, end: null });
      else if (this.hidden.length && this.hidden[this.hidden.length - 1].end === null) this.hidden[this.hidden.length - 1].end = now;
      if (this.hidden.length > HIDDEN_KEPT) this.hidden.splice(0, this.hidden.length - HIDDEN_KEPT);
    });
  }

  attach(mod) { this.mod = mod; }

  get(name) { return this.mod.ccall('Bt3Ui_Get', 'number', ['string'], [name]); }

  // ---- the engine's ring ----------------------------------------------------------------------------------------
  // Layout (web_ui.c sPerf): u32 capacity, written, gpuWritten, sampleBytes; then `capacity` samples of
  // {u32 frame, u32 vblanks, f32 timeMs, workMs, gameMs, listMs, submitMs}; then `capacity` of {u32 frame, f32 gpuMs}.
  ring() {
    const base = this.mod.ccall('Bt3Ui_PerfRing', 'number', [], []), buf = this.mod.HEAPU8.buffer;
    const [capacity, written, gpuWritten, sampleBytes] = new Uint32Array(buf, base, 4);
    const words = sampleBytes / 4, f = new Float32Array(buf, base + 16, capacity * words), u = new Uint32Array(buf, base + 16, capacity * words);
    const gbase = base + 16 + capacity * sampleBytes, gf = new Float32Array(buf, gbase, capacity * 2), gu = new Uint32Array(buf, gbase, capacity * 2);
    const ticks = [], gpu = [];
    for (let i = Math.max(0, written - capacity); i < written; i++) {
      const o = (i % capacity) * words;
      ticks.push({ frame: u[o], vblanks: u[o + 1], t: f[o + 2], work: f[o + 3], game: f[o + 4], draw: f[o + 5], submit: f[o + 6] });
    }
    for (let i = Math.max(0, gpuWritten - capacity); i < gpuWritten; i++) {
      const o = (i % capacity) * 2;
      gpu.push({ frame: gu[o], ms: gf[o + 1] });
    }
    return { ticks, gpu };
  }

  // ---- pictures that reach the screen (only while open) ----------------------------------------------------------
  shownStart() {
    const s = this.shown;
    const mark = () => {
      if (s.last === s.raf) return;
      s.last = s.raf;
      s.times.push(performance.now());
      if (s.times.length > SHOWN_KEPT) s.times.splice(0, s.times.length - SHOWN_KEPT);
    };
    const loop = () => { s.raf++; s.loop = requestAnimationFrame(loop); };
    s.loop = requestAnimationFrame(loop);
    if (this.renderer === 'webgpu' && window.GPUCanvasContext) {
      const proto = GPUCanvasContext.prototype, orig = proto.getCurrentTexture;
      proto.getCurrentTexture = function () { mark(); return orig.call(this); };
      s.unwrap = () => { proto.getCurrentTexture = orig; };
    } else if (window.WebGL2RenderingContext) {
      const proto = WebGL2RenderingContext.prototype, orig = proto.bindFramebuffer;
      // (the present: gs_gl.c binds the window's framebuffer)
      proto.bindFramebuffer = function (t, fb) { if (fb === null && t === this.FRAMEBUFFER) mark(); return orig.call(this, t, fb); };
      s.unwrap = () => { proto.bindFramebuffer = orig; };
    }
  }

  shownStop() {
    const s = this.shown;
    cancelAnimationFrame(s.loop);
    if (s.unwrap) s.unwrap();
    s.unwrap = null;
    s.times = [];
  }

  // ---- running only while open ----------------------------------------------------------------------------------
  run(on) {
    clearInterval(this.timer);
    this.timer = 0;
    this.shownStop();
    if (this.longFrames.observer) this.longFrames.observer.disconnect();
    this.longFrames.observer = null;
    if (!on || !this.mod) return;
    this.shownStart();
    if (PerformanceObserver.supportedEntryTypes && PerformanceObserver.supportedEntryTypes.includes('long-animation-frame')) {
      this.longFrames.observer = new PerformanceObserver((l) => { this.longFrames.count += l.getEntries().length; });
      this.longFrames.observer.observe({ type: 'long-animation-frame' });
    }
    this.cost = { ms: 0, since: performance.now() };
    this.update();
    this.timer = setInterval(() => this.update(), UPDATE_MS);
  }

  // ---- statistics -----------------------------------------------------------------------------------------------
  static quantiles(values) {
    if (!values.length) return null;
    const v = [...values].sort((a, b) => a - b), q = (p) => v[Math.min(v.length - 1, Math.floor(p * v.length))];
    return { n: v.length, mean: v.reduce((a, b) => a + b, 0) / v.length, p95: q(0.95), p99: q(0.99), max: v[v.length - 1] };
  }

  // The window's numbers. Ticks from a hidden tab (throttled, not the game's own pace) are left out.
  wasHidden(t) {
    return this.hidden.some((h) => t >= h.start && (h.end === null || t <= h.end));
  }

  summary(ring, now = performance.now()) {
    const from = now - WINDOW_S * 1000;
    const ticks = ring.ticks.filter((k) => k.t >= from && !this.wasHidden(k.t));
    const frames = new Set(ticks.map((k) => k.frame)), gpu = ring.gpu.filter((g) => frames.has(g.frame)).map((g) => g.ms);
    const span = ticks.length > 1 ? (ticks[ticks.length - 1].t - ticks[0].t) / 1000 : 0;
    const shown = this.shown.times.filter((t) => t >= from), shownLast = this.shown.times.filter((t) => t >= now - 1000);
    return {
      windowS: WINDOW_S,
      hiddenTicksLeftOut: ring.ticks.filter((k) => k.t >= from && this.wasHidden(k.t)).length,
      ticks: ticks.length,
      ticksPerS: span > 0 ? (ticks.length - 1) / span : null,
      gameSpeed: span > 0 ? (ticks[ticks.length - 1].vblanks - ticks[0].vblanks) / span / PS2_VBLANKS_PER_S : null,
      shownNow: this.shown.loop ? shownLast.length : null,
      shownAvg: this.shown.loop && shown.length > 1 ? (shown.length - 1) / ((shown[shown.length - 1] - shown[0]) / 1000) : null,
      work: Bt3Dashboard.quantiles(ticks.map((k) => k.work)),
      game: Bt3Dashboard.quantiles(ticks.map((k) => k.game)),
      draw: Bt3Dashboard.quantiles(ticks.map((k) => k.draw)),
      submit: Bt3Dashboard.quantiles(ticks.map((k) => k.submit)),
      gpu: Bt3Dashboard.quantiles(gpu),
      overBudget: ticks.filter((k) => k.work > TICK_BUDGET_MS).length,
    };
  }

  memory() {
    const m = { wasmBytes: this.mod ? this.mod.HEAPU8.byteLength : null, jsHeapBytes: null, gpuEstimateBytes: null };
    if (performance.memory) m.jsHeapBytes = performance.memory.usedJSHeapSize;
    if (this.mod && this.renderer === 'webgpu') m.gpuEstimateBytes = this.get('webgpu_kbytes') * 1024;
    return m;
  }

  loading() {
    const d = this.data, s = d.stats;
    return { filesOpened: s.files, requests: s.requests, bytesFetched: s.bytes, inFlight: d.pending.size, cacheBytes: d.cached,
             cacheHits: s.hits, cacheMisses: s.misses, errors: s.errors, manifest: !!d.manifest };
  }

  // ---- the section ----------------------------------------------------------------------------------------------
  build() {
    const el = (tag, attrs = {}, ...kids) => { const e = document.createElement(tag); for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v); e.append(...kids); return e; };
    this.statsEl = el('dl', { class: 'stats' });
    this.graph = el('canvas', { class: 'graph', width: '600', height: '120', role: 'img',
      'aria-label': `CPU work per tick for the last ${GRAPH_TICKS} ticks; the line marks the ${TICK_BUDGET_MS.toFixed(1)} ms budget` });
    const exportButton = el('button', { type: 'button', class: 'secondary' }, 'Export diagnostics (JSON)');
    exportButton.addEventListener('click', () => this.exportDiagnostics());
    this.uaMemoryButton = el('button', { type: 'button', class: 'secondary' }, 'Measure memory (browser)');
    this.uaMemoryButton.addEventListener('click', () => this.measureUaMemory());
    this.uaMemoryText = el('span', { class: 'dim' });
    const legend = el('p', { class: 'help' }, `Graph: CPU work per tick (orange) and GPU time (blue), last ${GRAPH_TICKS} ticks; ` +
      `line: the ${TICK_BUDGET_MS.toFixed(1)} ms a tick may take. Statistics over the last ${WINDOW_S} s. ` +
      'Work: a tick’s CPU time without the wait for the display. Game: the simulation. Drawing: the display list and the ' +
      'renderer. Submit: the renderer’s replay and submission (CPU). GPU: the graphics chip’s own time, where the browser ' +
      'lets a page measure it (rounded by the browser).');
    this.root.append(this.statsEl, this.graph, legend, el('div', { class: 'row inline' }, exportButton, this.uaMemoryButton, this.uaMemoryText));
  }

  update() {
    const t0 = performance.now();
    if (!this.mod) return;
    const ring = this.ring(), s = this.summary(ring, t0), mem = this.memory(), load = this.loading();
    const ms = (q) => q ? `${q.mean.toFixed(1)} / ${q.p95.toFixed(1)} / ${q.p99.toFixed(1)} ms` : '—';
    const mb = (b) => b === null ? null : `${(b / 1048576).toFixed(0)} MB`;
    const gpuTiming = this.get('gpu_timing');
    const rows = [
      ['Renderer', `${this.renderer === 'webgpu' ? 'WebGPU' : 'WebGL 2'}${this.gpuName ? ' — ' + this.gpuName : ''}`],
      ['Pictures shown', document.hidden ? 'paused (the tab is hidden)' : s.shownNow === null ? '—'
        : `${s.shownNow} now, ${s.shownAvg === null ? '—' : s.shownAvg.toFixed(1)} average /s`],
      ['Game ticks', s.ticksPerS === null ? '—' : `${s.ticksPerS.toFixed(1)} /s, game speed ${(s.gameSpeed * 100).toFixed(0)} %`],
      ['Work (mean / p95 / p99)', ms(s.work)],
      [' game', ms(s.game)],
      [' drawing', ms(s.draw)],
      ['  submit (CPU)', ms(s.submit)],
      ['GPU time', gpuTiming ? ms(s.gpu) + (this.get('gpu_timing_skipped') ? ` (${this.get('gpu_timing_skipped')} frames not timed)` : '')
        : 'not measurable here (the browser offers no GPU timer to this page)'],
      ...(s.hiddenTicksLeftOut ? [['Left out', `${s.hiddenTicksLeftOut} ticks while the tab was hidden (throttled by the browser)`]] : []),
      ['Long frames', `${s.overBudget} of ${s.ticks} ticks over ${TICK_BUDGET_MS.toFixed(1)} ms` +
        (this.longFrames.observer ? `; ${this.longFrames.count} page frames over ${LONG_TASK_MS} ms since opened` : '')],
      ['Memory', [`JS heap ${mem.jsHeapBytes === null ? 'not reported by this browser' : mb(mem.jsHeapBytes) + ' (browser estimate)'}`,
        `WebAssembly ${mb(mem.wasmBytes)} (reserved)`,
        `GPU ${mem.gpuEstimateBytes === null ? 'not measurable (WebGL 2 reports none; not tracked)' : mb(mem.gpuEstimateBytes) + ' estimated from sizes asked for'}`].join('; ')],
      ['Game data', `${load.filesOpened} files, ${load.requests} requests, ${mb(load.bytesFetched)} fetched, ${load.inFlight} in flight, ` +
        `cache ${mb(load.cacheBytes)}, ${load.errors} errors (read on demand: no total)`],
      ['Data check', DATA_CHECK[this.get('data_check')] || `unknown state ${this.get('data_check')}`],
    ];
    if (this.renderer === 'webgpu') {
      const n = this.get('webgpu_errors');
      rows.push(['WebGPU errors', n ? `${n}; latest: ${this.mod.ccall('Bt3Ui_WebGpuError', 'string', ['number'], [0])}` : '0']);
    }
    // (its updates so far: this one is counted at its end)
    const share = this.cost.ms / Math.max(1, t0 - this.cost.since) * 100;
    rows.push(['This section’s cost', `${share.toFixed(2)} % of the time since it was opened (its own updates)`]);
    // one replacement per update (twice a second), not per frame
    const frag = document.createDocumentFragment();
    for (const [k, v] of rows) { const dt = document.createElement('dt'), dd = document.createElement('dd'); dt.textContent = k; dd.textContent = v; frag.append(dt, dd); }
    this.statsEl.replaceChildren(frag);
    this.drawGraph(ring);
    this.cost.ms += performance.now() - t0;
  }

  drawGraph(ring) {
    const c = this.graph, g = c.getContext('2d'), w = c.width, h = c.height;
    const ticks = ring.ticks.slice(-GRAPH_TICKS), byFrame = new Map(ring.gpu.map((x) => [x.frame, x.ms]));
    const top = Math.max(TICK_BUDGET_MS * 1.5, ...ticks.map((k) => k.work));
    const y = (v) => h - (v / top) * (h - 4) - 2, x = (i) => (i / (GRAPH_TICKS - 1)) * w;
    const css = getComputedStyle(document.documentElement);
    g.clearRect(0, 0, w, h);
    g.strokeStyle = css.getPropertyValue('--line').trim() || '#555';
    g.beginPath(); g.moveTo(0, y(TICK_BUDGET_MS)); g.lineTo(w, y(TICK_BUDGET_MS)); g.stroke();
    const series = (values, color) => {
      g.strokeStyle = color; g.beginPath();
      let started = false;
      values.forEach((v, i) => { if (v === undefined) { started = false; return; } if (!started) { g.moveTo(x(i), y(v)); started = true; } else g.lineTo(x(i), y(v)); });
      g.stroke();
    };
    series(ticks.map((k) => k.work), css.getPropertyValue('--accent').trim() || '#f0b85a');
    series(ticks.map((k) => byFrame.get(k.frame)), css.getPropertyValue('--focus').trim() || '#7fb2ff');
  }

  async measureUaMemory() {
    if (typeof performance.measureUserAgentSpecificMemory !== 'function' || !self.crossOriginIsolated) {
      this.uaMemoryText.textContent = 'not available in this browser or page';
      return;
    }
    this.uaMemoryText.textContent = 'measuring (the browser may take up to a minute)…';
    try {
      const r = await performance.measureUserAgentSpecificMemory();
      this.uaMemory = r.bytes;
      this.uaMemoryText.textContent = `${(r.bytes / 1048576).toFixed(0)} MB (the browser's measurement of this page)`;
    } catch (e) {
      this.uaMemoryText.textContent = `failed: ${e.message}`;
    }
  }

  // ---- the export -----------------------------------------------------------------------------------------------
  // A versioned JSON file with what a bug report needs, downloaded here (never sent anywhere). Left out: save data, game
  // files, the full log (only its error lines), and anything personal beyond the browser's own description of itself.
  diagnostics() {
    const ring = this.mod ? this.ring() : { ticks: [], gpu: [] };
    let settingsFile = null;
    try { if (this.mod && this.mod.FS.analyzePath('bt3_settings.txt').exists) settingsFile = this.mod.FS.readFile('bt3_settings.txt', { encoding: 'utf8' }); } catch (e) { settingsFile = null; }
    const nav = navigator;
    return {
      schema: DIAGNOSTICS_SCHEMA,
      version: DIAGNOSTICS_VERSION,
      created: new Date().toISOString(),
      renderer: this.renderer,
      gpu: this.gpuName || null,
      page: { query: location.search, visibility: document.visibilityState, crossOriginIsolated: self.crossOriginIsolated },
      browser: {
        userAgent: nav.userAgent, logicalProcessors: nav.hardwareConcurrency, deviceMemoryGB: nav.deviceMemory ?? null,
        webgpu: !!nav.gpu, jspi: typeof WebAssembly.Suspending === 'function',
        devicePixelRatio: window.devicePixelRatio, screen: `${screen.width}x${screen.height}`,
      },
      engine: this.mod ? {
        started: true, frame: this.get('frame'), gpuTiming: !!this.get('gpu_timing'), gpuTimingSkipped: this.get('gpu_timing_skipped'),
        webgpuErrors: this.renderer === 'webgpu' ? this.get('webgpu_errors') : null,
        webgpuErrorTexts: this.renderer === 'webgpu' ? [0, 1, 2, 3, 4, 5, 6, 7].map((i) => this.mod.ccall('Bt3Ui_WebGpuError', 'string', ['number'], [i])).filter(Boolean) : [],
      } : { started: false },
      settings: { current: this.mod ? this.getSettings() : null, file: settingsFile },  // (the file exists once one was changed)
      performance: {
        definitions: 'ms; work = CPU time per tick without the wait for the display; game = the simulation; draw = display list and renderer; ' +
                     'submit = the renderer’s replay and submission (CPU); gpu = the GPU’s own time per frame (browser-rounded)',
        tickBudgetMs: TICK_BUDGET_MS,
        summary: this.mod ? this.summary(ring) : null,
        ticks: ring.ticks.map((k) => [k.frame, k.vblanks, +k.t.toFixed(1), +k.work.toFixed(3), +k.game.toFixed(3), +k.draw.toFixed(3), +k.submit.toFixed(3)]),
        ticksColumns: ['frame', 'vblanks', 'timeMs', 'workMs', 'gameMs', 'drawMs', 'submitMs'],
        gpu: ring.gpu.map((x) => [x.frame, +x.ms.toFixed(3)]),
        gpuColumns: ['frame', 'gpuMs'],
      },
      memory: { ...this.memory(), browserMeasuredBytes: this.uaMemory ?? null },
      loading: this.loading(),
      errors: this.getLog().filter((l) => LOG_ERROR.test(l)).slice(-LOG_ERRORS_KEPT),
    };
  }

  exportDiagnostics() {
    const blob = new Blob([JSON.stringify(this.diagnostics(), null, 1)], { type: 'application/json' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = `bt3-diagnostics-${new Date().toISOString().replace(/[:.]/g, '-')}.json`;
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 0);
  }
}
