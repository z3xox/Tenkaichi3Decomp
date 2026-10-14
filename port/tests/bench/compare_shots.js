// Compares the screenshots of two benchmark runs (bench.js --shots), frame by frame.
//
//   node port/tests/bench/compare_shots.js <labelA> <labelB>
//
// Per frame present in both: identical, or how many pixels differ, the largest and the mean difference of a channel,
// with a picture of where (differences in red over a dimmed copy of A) in port/build/bench/compare_<A>_<B>/.
// Exit status 0 when every frame is identical, 1 otherwise, 2 when the runs cannot be compared.
'use strict';
const fs = require('fs');
const path = require('path');

const BENCH = path.join(__dirname, '../../build/bench');
const DIM = 0.25;  // how bright A is drawn under the marked differences

function readPpm(file) {
  const buf = fs.readFileSync(file);
  // header: "P6\n<w> <h>\n255\n" (as the back ends write it)
  const m = /^P6\s+(\d+)\s+(\d+)\s+255\s/.exec(buf.subarray(0, 64).toString('latin1'));
  if (!m) throw new Error(`${file}: not a binary PPM with 8-bit channels`);
  return { w: Number(m[1]), h: Number(m[2]), px: buf.subarray(m[0].length) };
}

function compare(a, b) {
  if (a.w !== b.w || a.h !== b.h) return { sizeA: [a.w, a.h], sizeB: [b.w, b.h] };
  let differ = 0, maxDiff = 0, sum = 0;
  const marked = Buffer.alloc(a.px.length);
  for (let i = 0; i < a.px.length; i += 3) {
    const d = Math.max(Math.abs(a.px[i] - b.px[i]), Math.abs(a.px[i + 1] - b.px[i + 1]), Math.abs(a.px[i + 2] - b.px[i + 2]));
    if (d) {
      differ++;
      sum += d;
      maxDiff = Math.max(maxDiff, d);
      marked[i] = 255;
    } else {
      for (let c = 0; c < 3; c++) marked[i + c] = Math.round(a.px[i + c] * DIM);
    }
  }
  const pixels = a.px.length / 3;
  return { pixels, differ, share: differ / pixels, maxDiff, meanDiff: differ ? sum / differ : 0, marked };
}

function main() {
  const [la, lb] = process.argv.slice(2);
  if (!la || !lb) throw new Error('usage: compare_shots.js <labelA> <labelB>');
  const da = path.join(BENCH, `${la}.shots`), db = path.join(BENCH, `${lb}.shots`);
  const fa = new Set(fs.readdirSync(da)), fb = new Set(fs.readdirSync(db));
  const both = [...fa].filter((f) => fb.has(f)).sort();
  if (!both.length) throw new Error(`no frame in both ${da} and ${db}`);
  const out = path.join(BENCH, `compare_${la}_${lb}`);
  fs.rmSync(out, { recursive: true, force: true });
  let identical = 0;
  for (const f of both) {
    const a = readPpm(path.join(da, f)), b = readPpm(path.join(db, f));
    const r = compare(a, b);
    if (r.sizeA) {
      console.log(`  ${f}: sizes differ (${r.sizeA} / ${r.sizeB})`);
      continue;
    }
    if (!r.differ) {
      identical++;
      console.log(`  ${f}: identical`);
      continue;
    }
    fs.mkdirSync(out, { recursive: true });
    fs.writeFileSync(path.join(out, f), Buffer.concat([Buffer.from(`P6\n${a.w} ${a.h}\n255\n`), r.marked]));
    console.log(`  ${f}: ${r.differ} of ${r.pixels} pixels differ (${(100 * r.share).toFixed(3)} %), largest channel difference ` +
                `${r.maxDiff}, mean ${r.meanDiff.toFixed(1)}`);
  }
  const only = [...fa].filter((f) => !fb.has(f)).concat([...fb].filter((f) => !fa.has(f)));
  if (only.length) console.log(`  in one run only: ${only.join(' ')}`);
  console.log(`RESULT: ${identical} of ${both.length} frames identical${identical < both.length ? ` (differences: ${out})` : ''}`);
  process.exit(identical === both.length && !only.length ? 0 : 1);
}

try { main(); } catch (e) { console.error(e.message); process.exit(2); }
