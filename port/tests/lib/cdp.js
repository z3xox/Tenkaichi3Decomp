// A browser driven through the Chrome DevTools Protocol, for the browser build's tests (port/tests/bench,
// port/tests/wgsl): Chrome or Edge started with a fresh throwaway profile and a debugging port, one page, and the
// protocol's request / response calls. Nothing of the user's own browser profile is used.
'use strict';
const { spawn } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const DEFAULT_BROWSER = 'C:/Program Files/Google/Chrome/Application/chrome.exe';
const PORT_BASE = 9300, PORT_RANGE = 500;     // debugging ports tried: a random one in this range
const START_TIMEOUT_MS = 20000, POLL_MS = 200;

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// Starts the browser and connects to its first page. Returns { send(method, params), version, close() }.
async function launch({ browser = DEFAULT_BROWSER, headless = true, extraArgs = [] } = {}) {
  const port = PORT_BASE + Math.floor(Math.random() * PORT_RANGE);
  const profileDir = fs.mkdtempSync(path.join(os.tmpdir(), 'bt3-cdp-'));
  const proc = spawn(browser, [`--remote-debugging-port=${port}`, `--user-data-dir=${profileDir}`, '--no-first-run',
    '--no-default-browser-check', '--disable-extensions', '--disable-sync', '--autoplay-policy=no-user-gesture-required',
    '--disable-background-timer-throttling', '--disable-renderer-backgrounding', '--window-size=1280,1150',
    ...(headless ? ['--headless=new'] : []), ...extraArgs, 'about:blank'], { stdio: 'ignore' });
  let version;
  for (const until = Date.now() + START_TIMEOUT_MS; !version && Date.now() < until;) {
    try { version = await (await fetch(`http://127.0.0.1:${port}/json/version`)).json(); } catch (e) { await sleep(POLL_MS); }
  }
  if (!version) {
    proc.kill();
    throw new Error(`${browser} did not open its debugging port`);
  }
  const target = (await (await fetch(`http://127.0.0.1:${port}/json/list`)).json()).find((t) => t.type === 'page');
  const ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((r) => ws.addEventListener('open', r, { once: true }));
  let id = 0;
  const waiting = new Map();
  ws.addEventListener('message', (ev) => {
    const m = JSON.parse(ev.data);
    if (m.id && waiting.has(m.id)) {
      const w = waiting.get(m.id);
      waiting.delete(m.id);
      if (m.error) w.reject(new Error(`${w.method}: ${m.error.message}`));
      else w.resolve(m.result);
    }
  });
  const send = (method, params = {}) => new Promise((resolve, reject) => {
    const i = ++id;
    waiting.set(i, { resolve, reject, method });
    ws.send(JSON.stringify({ id: i, method, params }));
  });
  async function close() {
    ws.close();
    proc.kill();
    await sleep(500);
    try { fs.rmSync(profileDir, { recursive: true, force: true }); } catch (e) { /* the browser may still hold files */ }
  }
  return { send, version, close };
}

// Evaluates an expression in the page and returns its value (awaiting a promise).
async function evaluate(send, expression) {
  const r = await send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
  if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description || r.exceptionDetails.text);
  return r.result.value;
}

module.exports = { launch, evaluate, sleep };
