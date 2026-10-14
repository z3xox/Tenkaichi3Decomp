// Test of game-save persistence in the browser (port/web/saves.js, web_ui.c's card hooks) with the game's own saves:
// the attract sequence and menus are driven by scripted pad presses at fixed frames (BT3_PAD, gs_input.c; the boot is
// deterministic in frames), so the saves are the game's real memory-card saves. Storage faults (no IndexedDB, a full
// quota, an interrupted write, damaged stored data) are produced in the page. One Chrome profile for the whole run.
//   node port/tests/web/saves.js
'use strict';
const { launch, evaluate, sleep } = require('../lib/cdp.js');

// Frames of the boot and the menus (found with screenshots; deterministic): START skips the movie at 2800, the title
// waits for START at 3550, then New Game / Continue, and the memory-card prompts.
const NEW_GAME = '2800:start,3550:start,3700:cross,3850:left,3910:cross';           // New Game -> create a save: Yes
const CONTINUE = '2800:start,3550:start,3700:cross';                                  // Continue (the save is loaded)
const SAVE_AGAIN = `${CONTINUE},3900:up,4000:cross,4150:cross,4250:cross,4350:left,4410:cross,4700:left,4760:cross`;  // Options, Save/Load, Save: Yes, overwrite: Yes
const MAIN_MENU_FRAME = 3850;  // the main menu is up (Continue)
const CARD = '/saves/card1/BASLUS-21678DBZT3';
let failed = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failed++; };

(async () => {
  const b = await launch({ headless: true });
  const ev = (e) => evaluate(b.send, e);
  const frame = () => ev(`window.bt3Module ? window.bt3Module.ccall('Bt3Ui_Get','number',['string'],['frame']) : 0`);
  const lines = () => ev(`JSON.stringify(window.bt3Out.map(([, l]) => l).filter((l) => /saves|notice/.test(l)))`).then(JSON.parse);
  const open = async (script, extra = '') => {
    await b.send('Page.navigate', { url: `http://127.0.0.1:8080/index.html?autostart=1&env=BT3_PAD=${script}${extra}` });
    await sleep(3000);
  };
  const waitFor = async (test, ms) => { for (const until = Date.now() + ms; Date.now() < until; await sleep(500)) if (await test()) return true; return false; };
  const record = (slot) => ev(`new Promise((res) => { const r = indexedDB.open('bt3-saves'); r.onsuccess = () => { const g = r.result.transaction('card').objectStore('card').get('${slot}'); g.onsuccess = () => res(JSON.stringify(g.result ? { generation: g.result.generation, by: g.result.by, savedAt: g.result.savedAt, files: g.result.files.map((f) => [f.path, f.bytes.length, f.sha256]) } : null)); }; })`).then(JSON.parse);
  const kept = (gen) => waitFor(async () => (await lines()).some((l) => l.includes(`kept (generation ${gen}`)), 120000);

  // ---- 1. a new save, made by the game, kept ----
  await open(NEW_GAME);
  check(await kept(1), 'New Game: the game’s save is kept (generation 1)');
  const log1 = await lines();
  check(log1.filter((l) => l.includes('not complete yet')).length >= 1 && !log1.some((l) => l.startsWith('notice:')), 'the save under way (files not all written yet) was waited for, without a warning');
  const r1 = await record('current');
  check(r1 && r1.files.length === 3 && r1.files.some(([p, n]) => p === 'BASLUS-21678DBZT3/BASLUS-21678DBZT3' && n === 0x4000), 'stored: icon.sys, the icon, the 16 KB data file');

  // ---- 2. reload: the save is on the card before the game runs; the game loads it (Continue) ----
  await open(CONTINUE);
  check((await lines()).some((l) => l.includes('the current version') && l.includes('is on the card')), 'reload: the kept save is put on the card');
  const sameBytes = await ev(`(async () => { const r = await new Promise((res) => { const o = indexedDB.open('bt3-saves'); o.onsuccess = () => { const g = o.result.transaction('card').objectStore('card').get('current'); g.onsuccess = () => res(g.result); }; });
    return r.files.every((f) => { const b = window.bt3Module.FS.readFile('/saves/card1/' + f.path); return b.length === f.bytes.length && b.every((x, i) => x === f.bytes[i]); }); })()`);
  check(sameBytes, 'the card holds exactly the stored bytes');

  // ---- 3. an update by the game (different data: everything unlocked first) ----
  await open(SAVE_AGAIN);
  await waitFor(async () => (await frame()) >= MAIN_MENU_FRAME, 120000);
  await ev(`window.bt3Module.ccall('Bt3Ui_Action', 'number', ['string', 'number'], ['unlock_all', 0])`);
  check(await kept(2), 'Options > Save > overwrite: the update is kept (generation 2)');
  const [cur2, bak2] = [await record('current'), await record('backup')];
  check(cur2.by === 'the game' && bak2 && bak2.generation === 1, 'the previous save (generation 1) is now the backup');

  // ---- 4. export, import (valid / malformed / damaged), recovery choice ----
  const zip = await ev(`(async () => { let blob = null; const o = URL.createObjectURL; URL.createObjectURL = (b) => { blob = b; return 'blob:x'; };
    const c = HTMLAnchorElement.prototype.click; HTMLAnchorElement.prototype.click = function () {}; await saves.exportZip();
    URL.createObjectURL = o; HTMLAnchorElement.prototype.click = c; return btoa(String.fromCharCode(...new Uint8Array(await blob.arrayBuffer()))); })()`);
  const entries = await ev(`(async () => JSON.stringify((await zipRead(Uint8Array.from(atob(${JSON.stringify(zip)}), (c) => c.charCodeAt(0)).buffer)).map((f) => [f.path, f.bytes.length])))()`).then(JSON.parse);
  check(entries.length === 3 && entries.every(([p]) => p.startsWith('BASLUS-21678DBZT3/')), `export: a zip in the card's layout (${entries.map(([p, n]) => `${p} ${n}`).join(', ')})`);
  const importOf = (js) => ev(`(async () => { const before = (await saves.store.get('current')).generation; const ok = await ${js}; const after = (await saves.store.get('current')).generation; return JSON.stringify({ ok, before, after, notice: document.getElementById('notice').textContent }); })()`).then(JSON.parse);
  let r = await importOf(`saves.importZip(new File([Uint8Array.from(atob(${JSON.stringify(zip)}), (c) => c.charCodeAt(0))], 'mine.zip'))`);
  check(r.ok && r.after === r.before + 1 && /reload/.test(r.notice), 'import of the exported zip: checked, kept, asks for a reload');
  r = await importOf(`saves.importZip(new File([new Uint8Array([80, 75, 1, 2, 3])], 'broken.zip'))`);
  check(!r.ok && r.after === r.before && /not a zip/.test(r.notice), `malformed zip: refused, nothing changed (${r.notice})`);
  r = await importOf(`(async () => { const f = await zipRead(Uint8Array.from(atob(${JSON.stringify(zip)}), (c) => c.charCodeAt(0)).buffer); const d = f.find((x) => x.path.endsWith('/BASLUS-21678DBZT3')); d.bytes[100] ^= 0xFF; return saves.importFiles(f, 'damaged'); })()`);
  check(!r.ok && r.after === r.before && /checksum/.test(r.notice), `a save that fails the game's checksum: refused (${r.notice})`);
  r = await importOf(`(async () => { const f = await zipRead(Uint8Array.from(atob(${JSON.stringify(zip)}), (c) => c.charCodeAt(0)).buffer); const d = f.find((x) => x.path.endsWith('/BASLUS-21678DBZT3')); d.bytes = d.bytes.slice(0, 9000); return saves.importFiles(f, 'cut short'); })()`);
  check(!r.ok && r.after === r.before && /bytes/.test(r.notice), `a save cut short: refused (${r.notice})`);
  // the import made generation 3 the same game save as generation 2 (the backup stays generation 1? no: 2 had other data than 1)
  const before = [await record('current'), await record('backup')];
  await ev(`saves.useBackup().then(() => 0)`);
  const after = [await record('current'), await record('backup')];
  check(after[0].generation === before[1].generation && after[1].generation === before[0].generation, 'use the previous version: current and backup change places');
  await ev(`saves.useBackup().then(() => 0)`);  // (back, for what follows)

  // ---- 5. an interrupted write leaves the stored save as it was ----
  const cur5 = await record('current');
  const aborted = await ev(`new Promise((res) => { const o = indexedDB.open('bt3-saves'); o.onsuccess = () => { const tx = o.result.transaction('card', 'readwrite');
    tx.objectStore('card').put({ slot: 'current', schema: 1, savedAt: 0, generation: 999, by: 'half', saves: [], files: [] }); tx.onabort = () => res(true); tx.abort(); }; })`);
  check(aborted && (await record('current')).generation === cur5.generation, 'an interrupted (aborted) write: the stored save is unchanged');

  // ---- 6. damaged stored data: the start uses the backup and says so ----
  await ev(`new Promise((res) => { const o = indexedDB.open('bt3-saves'); o.onsuccess = () => { const tx = o.result.transaction('card', 'readwrite'), st = tx.objectStore('card');
    const g = st.get('current'); g.onsuccess = () => { const rec = g.result; rec.files.find((f) => f.path.endsWith('/BASLUS-21678DBZT3')).bytes[200] ^= 0xFF; st.put(rec); }; tx.oncomplete = () => res(0); }; })`);
  await open(CONTINUE);
  const log6 = await lines();
  check(log6.some((l) => l.includes('damaged') && l.includes('previous one is used')) && log6.some((l) => l.includes('the backup version')), 'damaged current version: the backup is put on the card, with a notice');

  // ---- 7. storage not available; quota full ----
  await b.send('Page.enable');
  await b.send('Page.addScriptToEvaluateOnNewDocument', { source: `Object.defineProperty(window, 'indexedDB', { value: { open() { throw new DOMException('denied', 'SecurityError'); } } });` });
  await open(NEW_GAME);
  const log7 = await lines();
  check(log7.some((l) => l.includes('cannot be kept in this browser')), 'no IndexedDB: said at start (saves last only while the page is open)');
  check(await waitFor(async () => (await lines()).some((l) => l.includes('a complete save is on the card, but not kept')), 120000) &&
        await ev(`window.bt3Module.FS.analyzePath('${CARD}/BASLUS-21678DBZT3').exists`), 'no IndexedDB: the game still saves to its card for the session, and says it is not kept'); 
  await b.close();

  const q = await launch({ headless: true });
  const qev = (e) => evaluate(q.send, e);
  await q.send('Page.enable');
  await q.send('Page.addScriptToEvaluateOnNewDocument', { source: `(() => { const put = IDBObjectStore.prototype.put; IDBObjectStore.prototype.put = function (v) { if (v && v.slot === 'current') throw new DOMException('quota', 'QuotaExceededError'); return put.call(this, v); }; })();` });
  await q.send('Page.navigate', { url: `http://127.0.0.1:8080/index.html?autostart=1&env=BT3_PAD=${NEW_GAME}` });
  const quota = await (async () => { for (let i = 0; i < 240; i++) { await sleep(500); const t = await qev(`document.getElementById('notice').textContent`); if (/full/.test(t)) return t; } return ''; })();
  check(/storage for this page is full/.test(quota), `quota exceeded: reported (${quota})`);
  await q.close();

  console.log(failed ? `RESULT: FAIL (${failed})` : 'RESULT: PASS');
  process.exit(failed ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
