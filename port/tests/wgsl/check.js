// Compiles every generated WGSL shader (port/tools/wasm_shaders.py -> port/build/gen_wasm/wgsl) with the browser's
// own WebGPU shader compiler (Chrome: Tint), which applies rules naga's validation may not (uniformity of
// derivatives and of texture sampling, the browser's limits). Fails on any error; lists warnings.
//
//   node port/tests/wgsl/check.js [--browser <exe>] [--headful]
'use strict';
const fs = require('fs');
const http = require('http');
const path = require('path');
const { launch, evaluate } = require('../lib/cdp.js');

const WGSL_DIR = path.join(__dirname, '../../build/gen_wasm/wgsl');
const argv = process.argv.slice(2);
const args = new Set(argv);
const browserArg = argv.includes('--browser') ? argv[argv.indexOf('--browser') + 1] : undefined;

// The page side: one device, every module compiled, the compiler's messages returned.
async function compileAll(shaders) {
  if (!navigator.gpu) return { error: 'navigator.gpu is not available (WebGPU off, or not a secure context)' };
  const adapter = await navigator.gpu.requestAdapter();
  if (!adapter) return { error: 'no WebGPU adapter' };
  const info = adapter.info || {};
  const device = await adapter.requestDevice();
  const results = [];
  for (const [name, code] of shaders) {
    const module = device.createShaderModule({ label: name, code });
    const ci = await module.getCompilationInfo();
    results.push({ name, messages: ci.messages.map((m) => ({ type: m.type, line: m.lineNum, col: m.linePos, text: m.message })) });
  }
  return { adapter: `${info.vendor || ''} ${info.architecture || ''} ${info.description || ''}`.trim(), results };
}

(async () => {
  const files = fs.readdirSync(WGSL_DIR).filter((f) => f.endsWith('.wgsl')).sort();
  if (!files.length) throw new Error(`no WGSL in ${WGSL_DIR}: run port/tools/wasm_shaders.py first`);
  const shaders = files.map((f) => [f, fs.readFileSync(path.join(WGSL_DIR, f), 'utf8')]);
  const server = http.createServer((req, res) => { res.writeHead(200, { 'Content-Type': 'text/html' }); res.end('<!doctype html><title>wgsl check</title>'); });
  await new Promise((r) => server.listen(0, '127.0.0.1', r));
  const browser = await launch({ browser: browserArg, headless: !args.has('--headful') });
  let failed = 0;
  try {
    await browser.send('Page.navigate', { url: `http://127.0.0.1:${server.address().port}/` });
    await new Promise((r) => setTimeout(r, 500));
    const out = await evaluate(browser.send, `(${compileAll.toString()})(${JSON.stringify(shaders)})`);
    if (out.error) throw new Error(out.error);
    console.log(`${browser.version.Browser}; adapter: ${out.adapter}`);
    for (const r of out.results) {
      const errors = r.messages.filter((m) => m.type === 'error');
      failed += errors.length ? 1 : 0;
      console.log(`  ${errors.length ? 'FAIL' : 'ok  '} ${r.name}${r.messages.length ? '' : ' (no messages)'}`);
      for (const m of r.messages) console.log(`         ${m.type} ${m.line}:${m.col} ${m.text}`);
    }
    console.log(failed ? `RESULT: FAIL (${failed} of ${out.results.length})` : `RESULT: PASS (${out.results.length} shaders)`);
  } finally {
    await browser.close();
    server.close();
  }
  process.exit(failed ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });
