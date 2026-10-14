// Game saves in the browser: the emulated memory card's files (plat_mc.c: /saves/card1/<save folder>/...) kept in
// IndexedDB, apart from the settings (localStorage), the data cache and the diagnostics.
//
// Keeping: the engine says when the card's files are whole (web_ui.c cardWritten: a written file closed, none open);
// the card is copied at that moment, checked, and written in one IndexedDB transaction with strict durability. Only
// when the transaction completes is the save reported kept. A card that fails the checks is not kept, and the stored
// one is not touched.
// Checks: every save folder (BASLUS-21678...) has icon.sys and its data file; the data file has a size the game writes
// (the game save 0x4000 bytes, a replay 0x1AC00) and passes the game's own checksum (McCard_Checksum, through
// Bt3Ui_SaveChecksumOk); every stored file has a SHA-256 to find damage in storage.
// Versions: "current" and "backup", the previous distinct game save (a copy of the same save made again does not push
// the backup out). At start the current one is put on the card before the game runs; if it is damaged, the backup is,
// and the page says so; both can be chosen between in the Saves tab.
// Export / import: a .zip of the save folders in the card's own layout (a PS2 card folder, or the native build's
// saves/card1), or a folder of them; imported saves are checked the same way and become the current version.
'use strict';

const SAVES_DB = 'bt3-saves';
const SAVES_DB_VERSION = 1;        // (object store "card"; a new version would migrate here)
const RECORD_SCHEMA = 1;
const CARD_DIR = '/saves/card1';   // plat_mc.c host_path, from the engine's current directory "/"
const SAVE_PREFIX = 'BASLUS-21678';
const ICON_SYS = 'icon.sys';
const ICON_SYS_BYTES = 964;        // the PS2 icon.sys is always this size
const DATA_SIZES = { 0x4000: 'game save', 0x1AC00: 'replay' };
const ZIP = { local: 0x04034b50, central: 0x02014b50, end: 0x06054b50, stored: 0, deflated: 8 };

// ---- checks ------------------------------------------------------------------------------------------------------
async function sha256(bytes) {
  return [...new Uint8Array(await crypto.subtle.digest('SHA-256', bytes))].map((b) => b.toString(16).padStart(2, '0')).join('');
}

// files: [{ path: 'BASLUS-21678DBZT3/icon.sys', bytes }]. checksumOk(bytes) -> the game's own check of a data file.
// Returns { ok, problems[], missing, saves: [{ folder, kind }] }; missing = every problem is a file not there yet (the game
// writes a new save's files one after another: icon.sys, the icon, then the data file).
function checkCard(files, checksumOk) {
  const problems = [], saves = [];
  let missingOnly = true;
  const bad = (text) => { problems.push(text); missingOnly = false; };
  const folders = new Map();
  for (const f of files) {
    const [folder, name, ...rest] = f.path.split('/');
    if (!folder.startsWith(SAVE_PREFIX) || !name || rest.length) { bad(`${f.path}: not a file of a save folder`); continue; }
    if (!folders.has(folder)) folders.set(folder, new Map());
    folders.get(folder).set(name, f.bytes);
  }
  if (!folders.size) problems.push('no save folder');
  for (const [folder, names] of folders) {
    const sys = names.get(ICON_SYS), data = names.get(folder);
    if (!sys) problems.push(`${folder}: no ${ICON_SYS}`);
    else if (sys.length !== ICON_SYS_BYTES) bad(`${folder}: ${ICON_SYS} is ${sys.length} bytes, not ${ICON_SYS_BYTES}`);
    if (!data) { problems.push(`${folder}: no data file`); continue; }
    const kind = DATA_SIZES[data.length];
    if (!kind) { bad(`${folder}: the data file is ${data.length} bytes (not a size the game writes)`); continue; }
    if (!checksumOk(data)) { bad(`${folder}: the ${kind} fails the game's checksum`); continue; }
    saves.push({ folder, kind });
  }
  return { ok: problems.length === 0, problems, missing: problems.length > 0 && missingOnly, saves };
}

// ---- zip (stored entries written; stored and deflated read) -------------------------------------------------------
const CRC_TABLE = (() => { const t = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; })();
function crc32(bytes) { let c = 0xFFFFFFFF; for (const b of bytes) c = CRC_TABLE[(c ^ b) & 0xFF] ^ (c >>> 8); return (c ^ 0xFFFFFFFF) >>> 0; }

function zipWrite(files) {
  const enc = new TextEncoder(), parts = [], central = [];
  let offset = 0;
  for (const f of files) {
    const name = enc.encode(f.path), crc = crc32(f.bytes), h = new DataView(new ArrayBuffer(30));
    h.setUint32(0, ZIP.local, true); h.setUint16(4, 20, true); h.setUint16(8, ZIP.stored, true);
    h.setUint32(14, crc, true); h.setUint32(18, f.bytes.length, true); h.setUint32(22, f.bytes.length, true); h.setUint16(26, name.length, true);
    parts.push(new Uint8Array(h.buffer), name, f.bytes);
    const c = new DataView(new ArrayBuffer(46));
    c.setUint32(0, ZIP.central, true); c.setUint16(4, 20, true); c.setUint16(6, 20, true); c.setUint16(10, ZIP.stored, true);
    c.setUint32(16, crc, true); c.setUint32(20, f.bytes.length, true); c.setUint32(24, f.bytes.length, true); c.setUint16(28, name.length, true);
    c.setUint32(42, offset, true);
    central.push(new Uint8Array(c.buffer), name);
    offset += 30 + name.length + f.bytes.length;
  }
  const size = central.reduce((n, p) => n + p.length, 0), e = new DataView(new ArrayBuffer(22));
  e.setUint32(0, ZIP.end, true); e.setUint16(8, files.length, true); e.setUint16(10, files.length, true); e.setUint32(12, size, true); e.setUint32(16, offset, true);
  return new Blob([...parts, ...central, new Uint8Array(e.buffer)], { type: 'application/zip' });
}

async function zipRead(buffer) {
  const v = new DataView(buffer), bytes = new Uint8Array(buffer), dec = new TextDecoder(), files = [];
  let end = -1;
  for (let i = buffer.byteLength - 22; i >= Math.max(0, buffer.byteLength - 65557); i--) if (v.getUint32(i, true) === ZIP.end) { end = i; break; }
  if (end < 0) throw new Error('not a zip file');
  let p = v.getUint32(end + 16, true);
  for (let n = v.getUint16(end + 10, true); n > 0; n--) {
    if (v.getUint32(p, true) !== ZIP.central) throw new Error('damaged zip directory');
    const method = v.getUint16(p + 10, true), crc = v.getUint32(p + 16, true), csize = v.getUint32(p + 20, true), usize = v.getUint32(p + 24, true);
    const nlen = v.getUint16(p + 28, true), xlen = v.getUint16(p + 30, true), clen = v.getUint16(p + 32, true), local = v.getUint32(p + 42, true);
    const name = dec.decode(bytes.subarray(p + 46, p + 46 + nlen));
    p += 46 + nlen + xlen + clen;
    if (name.endsWith('/')) continue;
    const start = local + 30 + v.getUint16(local + 26, true) + v.getUint16(local + 28, true), raw = bytes.slice(start, start + csize);
    let data;
    if (method === ZIP.stored) data = raw;
    else if (method === ZIP.deflated) data = new Uint8Array(await new Response(new Blob([raw]).stream().pipeThrough(new DecompressionStream('deflate-raw'))).arrayBuffer());
    else throw new Error(`${name}: compression method ${method} is not supported`);
    if (data.length !== usize || crc32(data) !== crc) throw new Error(`${name}: damaged in the zip`);
    files.push({ path: name, bytes: data });
  }
  return files;
}

// a zip or a folder may hold the save folders inside other folders (saves/card1/...): keep the part from the save folder
function cardPaths(files) {
  return files.map((f) => {
    const parts = f.path.split('/'), at = parts.findIndex((x) => x.startsWith(SAVE_PREFIX));
    return at < 0 ? f : { path: parts.slice(at).join('/'), bytes: f.bytes };
  });
}

// ---- storage ------------------------------------------------------------------------------------------------------
class SaveStore {
  constructor() { this.db = null; this.error = null; }

  async open() {
    try {
      if (!self.indexedDB) throw new Error('this browser gives the page no IndexedDB');
      this.db = await new Promise((resolve, reject) => {
        const r = indexedDB.open(SAVES_DB, SAVES_DB_VERSION);
        r.onupgradeneeded = () => { if (!r.result.objectStoreNames.contains('card')) r.result.createObjectStore('card', { keyPath: 'slot' }); };
        r.onsuccess = () => resolve(r.result);
        r.onerror = () => reject(r.error);
        r.onblocked = () => reject(new Error('the save storage is in use by another tab of this page'));
      });
    } catch (e) {
      this.error = e;
    }
    return !this.error;
  }

  async get(slot) {
    return new Promise((resolve, reject) => {
      const r = this.db.transaction('card').objectStore('card').get(slot);
      r.onsuccess = () => resolve(r.result || null);
      r.onerror = () => reject(r.error);
    });
  }

  // records written together, or not at all; resolves once the browser reports them durably written
  async put(records) {
    return new Promise((resolve, reject) => {
      const tx = this.db.transaction('card', 'readwrite', { durability: 'strict' });
      for (const rec of records) tx.objectStore('card').put(rec);
      tx.oncomplete = () => resolve();
      tx.onerror = () => reject(tx.error);
      tx.onabort = () => reject(tx.error || new Error('the write was aborted'));
    });
  }
}

class Bt3Saves {
  constructor({ log, notice }) {
    this.log = log;
    this.notice = notice;
    this.store = new SaveStore();
    this.mod = null;
    this.queue = Promise.resolve();  // commits one after another
    this.last = null;                // the last commit's outcome, for the tab
    this.loaded = null;              // which version went on the card at start
    this.ui = null;
  }

  // ---- before the game runs: the stored card onto the engine's card ----------------------------------------------
  async prepare(mod) {
    this.mod = mod;
    if (!(await this.store.open())) {
      this.say(`Saves cannot be kept in this browser (${this.store.error.message}): they last until the page is closed. Export them to keep them.`, true);
      return;
    }
    const current = await this.store.get('current'), backup = await this.store.get('backup');
    const [cur, bak] = [await this.verify(current), await this.verify(backup)];
    const use = cur.ok ? current : bak.ok ? backup : null;
    if (current && !cur.ok) this.say(`The last kept save is damaged (${cur.problems.join('; ')})` + (bak.ok ? ': the previous one is used instead.' : ' and there is no usable previous one.'), true);
    if (use) {
      this.writeCard(use.files);
      this.loaded = use.slot;
      this.log(`saves: the ${use.slot} version (${new Date(use.savedAt).toLocaleString()}) is on the card`);
    }
  }

  // stored record -> { ok, problems }: its files' hashes, then the game's checks
  async verify(rec) {
    if (!rec) return { ok: false, problems: ['none'] };
    if (rec.schema !== RECORD_SCHEMA) return { ok: false, problems: [`record format ${rec.schema} is not known`] };
    for (const f of rec.files) if (await sha256(f.bytes) !== f.sha256) return { ok: false, problems: [`${f.path} changed in storage`] };
    return checkCard(rec.files, (b) => this.checksumOk(b));
  }

  checksumOk(bytes) {
    const p = this.mod._malloc(bytes.length);
    try {
      this.mod.HEAPU8.set(bytes, p);
      return this.mod.ccall('Bt3Ui_SaveChecksumOk', 'number', ['number', 'number'], [p, bytes.length]) === 1;
    } finally {
      this.mod._free(p);
    }
  }

  readCard() {
    const FS = this.mod.FS, out = [];
    if (!FS.analyzePath(CARD_DIR).exists) return out;
    for (const folder of FS.readdir(CARD_DIR)) {
      if (folder === '.' || folder === '..') continue;
      const dir = `${CARD_DIR}/${folder}`;
      if (!FS.isDir(FS.stat(dir).mode)) continue;
      for (const name of FS.readdir(dir)) {
        if (name === '.' || name === '..') continue;
        out.push({ path: `${folder}/${name}`, bytes: FS.readFile(`${dir}/${name}`) });
      }
    }
    return out;
  }

  writeCard(files) {
    const FS = this.mod.FS;
    for (const f of files) {
      const path = `${CARD_DIR}/${f.path}`;
      FS.mkdirTree(path.slice(0, path.lastIndexOf('/')));
      FS.writeFile(path, f.bytes);
    }
  }

  // ---- the engine says the card is whole ---------------------------------------------------------------------------
  cardWritten() {
    const files = this.readCard();  // (now: the files are whole at this moment)
    this.queue = this.queue.then(() => this.commit(files, 'the game')).catch((e) => this.say(`The save was not kept: ${e.message}`, true));
  }

  // files checked, then kept as the current version (the previous distinct game save becomes the backup)
  async commit(files, by) {
    const check = checkCard(files, (b) => this.checksumOk(b));
    if (!check.ok && check.missing && by === 'the game') {  // (a save under way: its next file completes it)
      this.log(`saves: the card is not complete yet (${check.problems.join('; ')}); kept when it is`);
      return false;
    }
    if (!check.ok) {
      this.last = { ok: false, at: Date.now(), why: check.problems.join('; ') };
      this.say(`A save was not kept: ${this.last.why}. The stored save is unchanged.`, true);
      this.render();
      return false;
    }
    if (!this.store.db) {
      this.last = { ok: false, at: Date.now(), why: 'browser storage is not available' };
      this.log('saves: a complete save is on the card, but not kept: browser storage is not available (it lasts until the page is closed)');
      this.render();
      return false;
    }
    const current = await this.store.get('current');
    const record = { slot: 'current', schema: RECORD_SCHEMA, savedAt: Date.now(), by, generation: current ? current.generation + 1 : 1,
                     saves: check.saves, files: await Promise.all(files.map(async (f) => ({ path: f.path, bytes: f.bytes, sha256: await sha256(f.bytes) }))) };
    const records = [record];
    // the current version becomes the backup only if it is a different game save and passes the checks itself (a damaged
    // one never pushes out a good backup)
    if (current && Bt3Saves.gameData(current) !== Bt3Saves.gameData(record) && (await this.verify(current)).ok) records.push({ ...current, slot: 'backup' });
    try {
      await this.store.put(records);
    } catch (e) {
      const full = e && (e.name === 'QuotaExceededError' || /quota/i.test(e.message));
      this.last = { ok: false, at: Date.now(), why: full ? 'the browser’s storage for this page is full' : e.message };
      this.say(`The save was not kept: ${this.last.why}. The stored save is unchanged.`, true);
      this.render();
      return false;
    }
    this.last = { ok: true, at: record.savedAt, generation: record.generation };
    this.log(`saves: kept (generation ${record.generation}, ${check.saves.map((s) => `${s.folder}: ${s.kind}`).join(', ')})`);
    if (navigator.storage && navigator.storage.persist) navigator.storage.persist().then((p) => { this.persisted = p; this.render(); });
    this.render();
    return true;
  }

  // the hash of a record's game save data (what tells two versions apart)
  static gameData(rec) {
    return rec.files.filter((f) => f.path.split('/')[1] === f.path.split('/')[0]).map((f) => f.sha256).sort().join();
  }

  say(text, important) {
    this.log(`saves: ${text}`);
    if (important) this.notice(text);
  }

  // ---- export, import, recovery --------------------------------------------------------------------------------------
  async exportZip() {
    const rec = this.store.db ? await this.store.get('current') : null;
    const files = rec ? rec.files : this.readCard();
    if (!files.length) { this.notice('There is no save to export yet.'); return; }
    const a = document.createElement('a');
    a.href = URL.createObjectURL(zipWrite(files.map((f) => ({ path: f.path, bytes: f.bytes }))));
    a.download = `bt3-saves-${new Date().toISOString().slice(0, 10)}.zip`;
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 0);
  }

  // files: [{ path, bytes }] from a zip or a folder. Checked like a save of the game; kept as the current version.
  async importFiles(files, source) {
    const card = cardPaths(files);
    const check = checkCard(card, (b) => this.checksumOk(b));
    if (!check.ok) { this.notice(`Not imported (${source}): ${check.problems.join('; ')}.`); return false; }
    if (!(await this.commit(card, `import of ${source}`))) return false;
    this.notice('Imported. The game reads its memory card when it starts: reload the page to play with it.');
    this.render(true);
    return true;
  }

  async importZip(file) {
    try {
      return await this.importFiles(await zipRead(await file.arrayBuffer()), file.name);
    } catch (e) {
      this.notice(`Not imported (${file.name}): ${e.message}.`);
      return false;
    }
  }

  async importFolder(fileList) {
    const files = await Promise.all([...fileList].map(async (f) => ({ path: f.webkitRelativePath || f.name, bytes: new Uint8Array(await f.arrayBuffer()) })));
    return this.importFiles(files, 'a folder');
  }

  // the backup becomes the current version (the current one the backup): takes effect when the page is reloaded
  async useBackup() {
    const current = await this.store.get('current'), backup = await this.store.get('backup');
    if (!backup || !(await this.verify(backup)).ok) { this.notice('There is no usable previous version.'); return; }
    await this.store.put([{ ...backup, slot: 'current' }, ...(current ? [{ ...current, slot: 'backup' }] : [])]);
    this.notice('The previous version is now the current one. Reload the page to play with it.');
    this.render(true);
  }

  // ---- the Saves tab ----------------------------------------------------------------------------------------------------
  buildUi() {
    const el = (tag, attrs = {}, ...kids) => { const e = document.createElement(tag); for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v); e.append(...kids); return e; };
    this.el = el;
    this.statusEl = el('dl', { class: 'stats' });
    this.versionsEl = el('ul', { class: 'pads' });
    const exportButton = el('button', { type: 'button', class: 'secondary' }, 'Export saves (.zip)');
    exportButton.addEventListener('click', () => this.exportZip());
    const zipInput = el('input', { type: 'file', accept: '.zip,application/zip', hidden: '' });
    zipInput.addEventListener('change', () => { if (zipInput.files[0]) this.importZip(zipInput.files[0]); zipInput.value = ''; });
    const zipButton = el('button', { type: 'button', class: 'secondary' }, 'Import a .zip…');
    zipButton.addEventListener('click', () => zipInput.click());
    const dirInput = el('input', { type: 'file', webkitdirectory: '', hidden: '' });
    dirInput.addEventListener('change', () => { if (dirInput.files.length) this.importFolder(dirInput.files); dirInput.value = ''; });
    const dirButton = el('button', { type: 'button', class: 'secondary' }, 'Import a folder…');
    dirButton.addEventListener('click', () => dirInput.click());
    this.backupButton = el('button', { type: 'button', class: 'secondary' }, 'Use the previous version');
    this.backupButton.addEventListener('click', () => this.useBackup());
    this.reload = el('button', { type: 'button' }, 'Reload now');
    this.reload.addEventListener('click', () => location.reload());
    this.reload.hidden = true;
    this.ui = el('div', { class: 'saves' },
      el('p', { class: 'help' }, 'The game saves to its memory card as on the console; this page keeps the card in the browser’s storage ' +
        'for this site, apart from the settings. A save is reported kept only once the browser has written it. The browser may delete ' +
        'this storage: when its site data is cleared, in a private window when it closes, or when the computer runs short of space ' +
        '(less likely once the browser grants this page lasting storage). Export your saves to keep a copy of your own.'),
      this.statusEl, el('h3', {}, 'Versions'), this.versionsEl,
      el('div', { class: 'row inline' }, this.backupButton, this.reload),
      el('h3', {}, 'Export and import'),
      el('p', { class: 'help' }, 'A .zip of the save folders as on a memory card (BASLUS-21678…), the layout of the native build’s saves/card1. ' +
        'Imported saves are checked with the game’s own checksum before they are kept.'),
      el('div', { class: 'row inline' }, exportButton, zipButton, dirButton), zipInput, dirInput);
    return this.ui;
  }

  async render(needsReload) {
    if (!this.ui) return;
    if (needsReload) this.reload.hidden = false;
    const rows = [];
    if (!this.store.db) rows.push(['Storage', `not available: ${this.store.error ? this.store.error.message : 'not opened yet'}`]);
    else {
      const est = navigator.storage && navigator.storage.estimate ? await navigator.storage.estimate() : null;
      const persisted = navigator.storage && navigator.storage.persisted ? await navigator.storage.persisted() : null;
      rows.push(['Storage', `${persisted === null ? 'unknown' : persisted ? 'lasting (the browser keeps it)' : 'may be cleared by the browser when space runs short'}` +
        (est ? `; this site uses ${(est.usage / 1048576).toFixed(1)} MB of ${(est.quota / 1048576).toFixed(0)} MB` : '')]);
    }
    rows.push(['Last save', this.last === null ? 'none since the page was opened' : this.last.ok
      ? `kept ${new Date(this.last.at).toLocaleTimeString()} (generation ${this.last.generation})` : `NOT kept ${new Date(this.last.at).toLocaleTimeString()}: ${this.last.why}`]);
    rows.push(['On the card since start', this.loaded ? `the ${this.loaded} version` : 'nothing stored (a new card)']);
    this.statusEl.replaceChildren(...rows.flatMap(([k, v]) => [this.el('dt', {}, k), this.el('dd', {}, v)]));
    const items = [];
    let backupOk = false;
    if (this.store.db) {
      for (const slot of ['current', 'backup']) {
        const rec = await this.store.get(slot);
        if (!rec) { items.push(this.el('li', { class: 'dim' }, `${slot}: none`)); continue; }
        const v = await this.verify(rec);
        if (slot === 'backup') backupOk = v.ok;
        items.push(this.el('li', {}, `${slot}: ${new Date(rec.savedAt).toLocaleString()}, generation ${rec.generation}, by ${rec.by}; ` +
          `${rec.saves.map((s) => `${s.folder.slice(SAVE_PREFIX.length) || s.folder} (${s.kind})`).join(', ')}; ${v.ok ? 'checks pass' : 'DAMAGED: ' + v.problems.join('; ')}`));
      }
    }
    this.versionsEl.replaceChildren(...items);
    this.backupButton.disabled = !backupOk;
  }
}
