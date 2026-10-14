// Test of port/web/bt3_data.js (the browser build's on-demand data reads) against a local HTTP server started here
// with known misbehaviours. No game data: the files are generated. Run: node port/tests/web_data/test_data.js
'use strict';
const http = require('http');
const path = require('path');
const { Bt3Data } = require(path.join(__dirname, '../../web/bt3_data.js'));

const SIZE = 600 * 1024 + 123;  // not a multiple of the block size
const file = new Uint8Array(SIZE).map((_, i) => (i * 31 + (i >> 8)) & 255);
const counts = { range: 0, full: 0, head: 0 };
let mode = 'range';             // 'range' | 'norange' | 'error' | 'short'

const server = http.createServer((req, res) => {
  const rel = decodeURIComponent(req.url.replace(/^\/data\//, ''));
  if (req.url === '/data/manifest.json') { res.writeHead(404); return res.end(); }
  if (rel === 'missing.bin') { res.writeHead(404); return res.end(); }
  if (rel === 'broken.bin' || mode === 'error') { res.writeHead(500); return res.end(); }
  if (rel !== 'dir/file one.bin') { res.writeHead(404); return res.end(); }
  if (req.method === 'HEAD') { counts.head++; res.writeHead(200, { 'Content-Length': SIZE }); return res.end(); }
  const m = /bytes=(\d+)-(\d+)/.exec(req.headers.range || '');
  if (m && mode !== 'norange') {
    counts.range++;
    const a = Number(m[1]), b = Math.min(Number(m[2]), SIZE - 1);
    const body = mode === 'short' ? file.subarray(a, b) : file.subarray(a, b + 1);  // 'short': one byte missing
    res.writeHead(206, { 'Content-Range': `bytes ${a}-${b}/${SIZE}`, 'Content-Length': body.length });
    return res.end(Buffer.from(body));
  }
  counts.full++;
  res.writeHead(200, { 'Content-Length': SIZE });
  res.end(Buffer.from(file));
});

let failures = 0;
function check(ok, what) { console.log(`  ${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failures++; }
function fresh(port) {
  const d = new Bt3Data(`http://127.0.0.1:${port}/data/`, { blockSize: 256 * 1024, cacheBytes: 600 * 1024, log: () => {} });
  d.module = { HEAPU8: new Uint8Array(2 * 1024 * 1024) };
  return d;
}
const same = (d, dst, off, n) => d.module.HEAPU8.subarray(dst, dst + n).every((v, i) => v === file[off + i]);

server.listen(0, '127.0.0.1', async () => {
  const port = server.address().port;
  let d = fresh(port);
  check(await d.init() === 0, 'no manifest: sizes by HEAD');
  check(await d.size('dir/file one.bin') === SIZE, `size by HEAD (${SIZE}), name with a space encoded`);
  check(await d.size('missing.bin') === -1, 'missing file: size -1 (404)');
  check(await d.size('broken.bin') === -2, 'server error: size -2 (500)');
  check(await d.read('missing.bin', 0, 0, 10) === -1, 'reading a missing file: -1');
  // reads within a block, across blocks, at the end, past the end
  let n = await d.read('dir/file one.bin', 1000, 0, 5000);
  check(n === 5000 && same(d, 0, 1000, 5000), 'read inside the first block');
  n = await d.read('dir/file one.bin', 256 * 1024 - 100, 10000, 300000);
  check(n === 300000 && same(d, 10000, 256 * 1024 - 100, 300000), 'read across three blocks');
  n = await d.read('dir/file one.bin', SIZE - 50, 400000, 4096);
  check(n === 50 && same(d, 400000, SIZE - 50, 50), 'read at the end returns only what is there (50 bytes)');
  check(await d.read('dir/file one.bin', SIZE + 10, 0, 100) === 0, 'read past the end returns 0');
  // all three blocks were read (614,523 bytes) with a 600 KB cache: block 0, used longest ago, was dropped
  check(d.cached <= 600 * 1024, `cache stays within its limit (${d.cached} bytes of ${600 * 1024})`);
  let before = counts.range;
  n = await d.read('dir/file one.bin', SIZE - 1000, 500000, 1000);
  check(n === 1000 && counts.range === before && same(d, 500000, SIZE - 1000, 1000), 'a block still in the cache is not fetched again');
  before = counts.range;
  n = await d.read('dir/file one.bin', 2000, 500000, 1000);
  check(n === 1000 && counts.range === before + 1 && same(d, 500000, 2000, 1000), 'the block dropped from the cache is fetched again, correctly');
  // two reads of the same block at the same time: one request
  d = fresh(port);
  const r0 = counts.range;
  await Promise.all([d.read('dir/file one.bin', 0, 0, 10), d.read('dir/file one.bin', 20, 100, 10)]);
  check(counts.range - r0 === 1, 'concurrent reads of one block share one request');
  // a server without Range support (200 with the whole file)
  mode = 'norange';
  d = fresh(port);
  n = await d.read('dir/file one.bin', 300000, 0, 1000);
  check(n === 1000 && same(d, 0, 300000, 1000), 'server without Range: the right bytes from a 200 response');
  // a server that sends fewer bytes than its Content-Range says
  mode = 'short';
  d = fresh(port);
  check(await d.read('dir/file one.bin', 0, 0, 100) === -1 && d.stats.errors === 1, 'short body: an error, not wrong data');
  // a failing server during a read
  mode = 'range';
  d = fresh(port);
  await d.size('dir/file one.bin');
  mode = 'error';
  check(await d.read('dir/file one.bin', 0, 0, 100) === -1, 'HTTP 500 during a read: -1');
  console.log(failures ? `RESULT: FAIL (${failures})` : 'RESULT: PASS');
  // The exit status is set and Node exits by itself once the server and fetch's connections are closed. Calling
  // process.exit() here, while fetch (undici) was still tearing its sockets down, made Node on Windows abort in libuv
  // (src\win\async.c: !(handle->flags & UV_HANDLE_CLOSING)) in about a third of the runs: exit code 127 after
  // "RESULT: PASS".
  process.exitCode = failures ? 1 : 0;
  server.closeAllConnections();
  server.close();
});
// A test that cannot finish (a read that never resolves) must not hang the runner or pass: it fails after this.
const DEADLINE_MS = 60000;
setTimeout(() => { console.log(`RESULT: FAIL (not finished after ${DEADLINE_MS / 1000} s)`); process.exit(1); }, DEADLINE_MS).unref();
