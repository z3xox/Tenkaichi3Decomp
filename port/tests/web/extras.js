// Test of content added from outside the disc in the browser version: the data folder looked through over HTTP
// (port/src/web/web_fs.c, port/web/bt3_data.js, port/web/devserver.py: <folder>/?list) and what is said about it
// (port/src/web/web_extras.c, the settings panel's Extras tab), in Chrome through the DevTools protocol:
//   node port/tests/web/extras.js --data <your gamedata folder>   (needs a build in port/build/web)
// It starts its own development server (port TEST_PORT) with a temporary folder mounted at /gamedata/test_extras,
// holding a copy of one of the disc's stage models as an added stage and of one of its ADX streams as an added song
// (copied from your own data folder into the system's temporary folder, removed at the end; nothing is published), and
// a texture pack of one replacement (a plain magenta PNG made here) for a texture of the attract demo.
// Checks:
//   - listed, nothing added: nothing said for stages, songs, texture packs (as natively: none added); the panel says
//     none;
//   - listed, one of each added (BT3_STAGES / BT3_SONGS / BT3_TEXTURES name the test folders): all found, named in the
//     panel, the stage's file reached over HTTP, the replacement texture found by the game and its PNG read over HTTP;
//   - a battle on the added stage (the game's own start, its attract battle put there by BT3_TEST_STAGE): its file read,
//     the game running on;
//   - a server that does not list folders (its answers to ?list replaced by a page): said for stages, songs and
//     texture packs, in the log and the panel; a stage named through BT3_EXTRA_STAGES (no folder needed) is still found.
'use strict';
const fs = require('fs'), os = require('os'), path = require('path');
const { spawn } = require('child_process');
const { launch, evaluate, sleep } = require('../lib/cdp.js');

const args = Object.fromEntries(process.argv.slice(2).reduce((acc, a, i, all) => {
  if (a.startsWith('--')) acc.push([a.slice(2), all[i + 1] && !all[i + 1].startsWith('--') ? all[i + 1] : true]);
  return acc;
}, []));
const ROOT = path.resolve(__dirname, '../../..');
const TEST_PORT = 8090;
const START_MS = 15000;      // the attract demo is running by then: the game has looked for added content at its start
const STAGE_SOURCE = 'pzs3us1/00368.bin';      // one of the disc's stage models (plat_stages.c: 368..402)
const SONG_SOURCE = 'disc/DATA/ZS3USOP.ADX';   // one of the disc's ADX streams
// A texture of the attract demo's hit effects, by the name a pack gives its replacement (gs_texpack.c; found with
// BT3_TEX_LOG=1), and the frame by which the demo has drawn it several times
const TEXTURE = '1caec06f69c010f5-168969d9cd8be19f-00005dd3', TEXTURE_SIZE = 128, TEXTURE_FRAME = 700, TEXTURE_WAIT_MS = 60000;
// The title's first attract battle starts around frame 2400 with logos, intro and copyright page skipped (measured);
// the check waits up to this long, and wants the game this many frames past the stage's read
const BATTLE_WAIT_MS = 150000, BATTLE_RUN_FRAMES = 1000;
let failed = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failed++; };

if (!args.data || !fs.existsSync(path.join(args.data, STAGE_SOURCE)) || !fs.existsSync(path.join(args.data, SONG_SOURCE))) {
  console.error(`usage: node port/tests/web/extras.js --data <gamedata folder> (it needs ${STAGE_SOURCE} and ${SONG_SOURCE} there)`);
  process.exit(2);
}

// A PNG of one colour (RGBA 8-bit, no filter), made here so that the test's texture pack holds nothing of the game
function magentaPng(w, h) {
  const zlib = require('zlib');
  const crcTable = Array.from({ length: 256 }, (_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
  const crc = (buf) => { let c = 0xFFFFFFFF; for (const x of buf) c = crcTable[(c ^ x) & 0xFF] ^ (c >>> 8); return (c ^ 0xFFFFFFFF) >>> 0; };
  const chunk = (type, data) => {
    const td = Buffer.concat([Buffer.from(type, 'ascii'), data]), out = Buffer.alloc(td.length + 8);
    out.writeUInt32BE(data.length, 0); td.copy(out, 4); out.writeUInt32BE(crc(td), td.length + 4);
    return out;
  };
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 6;
  const row = Buffer.concat([Buffer.from([0]), Buffer.alloc(w * 4).fill(Buffer.from([255, 0, 255, 128]))]);
  return Buffer.concat([Buffer.from([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]), chunk('IHDR', ihdr),
                        chunk('IDAT', zlib.deflateSync(Buffer.concat(Array(h).fill(row)))), chunk('IEND', Buffer.alloc(0))]);
}

(async () => {
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'bt3-extras-'));
  fs.mkdirSync(path.join(tmp, 'stages'));
  fs.mkdirSync(path.join(tmp, 'songs'));
  fs.copyFileSync(path.join(args.data, STAGE_SOURCE), path.join(tmp, 'stages', 'Test Stage.unk'));
  fs.copyFileSync(path.join(args.data, SONG_SOURCE), path.join(tmp, 'songs', 'Test Song.adx'));
  fs.mkdirSync(path.join(tmp, 'textures', 'effects'), { recursive: true });
  fs.writeFileSync(path.join(tmp, 'textures', 'effects', `${TEXTURE}.png`), magentaPng(TEXTURE_SIZE, TEXTURE_SIZE));
  const server = spawn(process.env.PYTHON || 'python', [path.join(ROOT, 'port/web/devserver.py'), '--port', String(TEST_PORT),
    '--root', path.join(ROOT, 'port/build/web'), '--mount', `gamedata=${args.data}`, '--mount', `gamedata/test_extras=${tmp}`],
    { stdio: 'ignore', env: { ...process.env, DEVSERVER_QUIET: '1' } });
  const b = await launch({ headless: true });
  try {
    await sleep(1500);
    const ev = (e) => evaluate(b.send, e);
    const get = (n) => ev(`window.bt3Module.ccall('Bt3Ui_Get', 'number', ['string'], ['${n}'])`);
    const run = async (env, untilFrame = 0) => {
      await b.send('Page.navigate', { url: `http://127.0.0.1:${TEST_PORT}/index.html?autostart=1&env=BT3_DEMO=1${env}` });
      await sleep(START_MS);
      for (const until = Date.now() + TEXTURE_WAIT_MS; Date.now() < until && (await get('frame')) < untilFrame;) await sleep(1000);
      const lines = JSON.parse(await ev(`JSON.stringify(window.bt3Out.map(([, l]) => l).filter((l) => /added stages|added songs|texture pack|texpack: found|stages:|songs:|data: |failed|not implemented|abort/i.test(l)))`));
      const state = { stages: await get('added_stage_count'), songs: await get('added_song_count'), textures: await get('texture_pack_count'),
                      listable: await get('data_listable'), frame: await get('frame') };
      await ev(`document.getElementById('settings-open').click(); document.getElementById('tab-extras').click(); 0`);
      await sleep(500);
      const panel = JSON.parse(await ev(`JSON.stringify([...[...document.querySelectorAll('#tab-extras-page h3')].find((h) => h.textContent.startsWith('Added stages')).parentElement.querySelectorAll('p')].map((p) => p.textContent))`));
      return { lines, state, panel };
    };
    const notAvailable = (lines, what) => lines.filter((l) => l.startsWith(`bt3: ${what}: not available`)).length;

    // ---- listed, nothing added ----
    let r = await run('');
    check(r.state.listable === 1, `the data server lists its folders (${r.state.listable})`);
    check(r.state.stages === 0 && r.state.songs === 0 && r.state.textures === 0, 'nothing added: none found');
    check(['added stages', 'added songs', 'texture packs'].every((w) => notAvailable(r.lines, w) === 0), 'log: nothing said (none added, as natively)');
    check(r.panel[0] === 'Stages: none (the data folder has no stages folder with any).' && r.panel[1].startsWith('Songs: none') &&
          r.panel[2] === 'Texture packs: none (the data folder has no textures folder with any).', `panel: ${r.panel.join(' | ')}`);

    // ---- listed, a stage, a song and a texture pack added ----
    // (Playing an added song needs the menus: not covered. A battle on the added stage: the next case.)
    r = await run(',BT3_STAGES=gamedata/test_extras/stages,BT3_SONGS=gamedata/test_extras/songs,BT3_TEXTURES=gamedata/test_extras/textures,BT3_TEX_LOG=1', TEXTURE_FRAME);
    check(r.state.stages === 1 && r.lines.some((l) => l === 'bt3: stages: 1 added from gamedata/test_extras/stages'), 'the added stage is found (log: 1 added)');
    check(r.state.songs === 1 && r.lines.some((l) => l === 'bt3: songs: 1 added from gamedata/test_extras/songs'), 'the added song is found (log: 1 added)');
    const stageAsked = await ev(`performance.getEntriesByType('resource').some((e) => e.name.endsWith('/gamedata/test_extras/stages/Test%20Stage.unk'))`);
    check(stageAsked, 'the added stage\'s file is reached over HTTP (its size, for the stage buffer)');
    check(r.state.frame > 0 && !r.lines.some((l) => /failed|not implemented|abort/i.test(l)), `the game runs on, no read failure or abort (frame ${r.state.frame})`);
    check(r.state.textures === 1 && r.lines.some((l) => l.startsWith('bt3: texture pack: 1 replacement textures in gamedata/test_extras/textures')), 'the texture pack is found (log: 1 replacement)');
    check(r.lines.some((l) => l.startsWith('texpack: found') && l.endsWith(`gamedata/test_extras/textures/effects/${TEXTURE}.png`)), `the game uses the replacement for ${TEXTURE} (frame ${r.state.frame})`);
    check(await ev(`performance.getEntriesByType('resource').some((e) => e.name.endsWith('/gamedata/test_extras/textures/effects/${TEXTURE}.png'))`), 'its PNG is read over HTTP');
    check(r.panel[0] === 'Stages: 1 added stage.' && r.panel[1] === 'Songs: 1 added song.' && r.panel[2] === 'Texture packs: 1 replacement texture.', `panel: ${r.panel.join(' | ')}`);

    // ---- a battle on the added stage ----
    // The game's own start (no BT3_DEMO: its first-run sequence loads archive partition 2, where an added stage's file
    // id lies; the BT3_DEMO shortcut skips that sequence, so there the added stage's file cannot be found and the
    // battle's load waits for it forever). Logos, intro and copyright page skipped; the title, left alone, plays its
    // attract battle, which BT3_TEST_STAGE puts on the added stage. Its file must be read, and the game run on.
    await b.send('Page.enable');
    const { identifier } = await b.send('Page.addScriptToEvaluateOnNewDocument', { source: `
      localStorage.setItem('bt3.settings', 'skip_logos=1\\nskip_intro=1\\nskip_copyright=1\\n');
      window.stageReads = []; (() => { const t = setInterval(() => { if (typeof Bt3Data !== 'function') return; clearInterval(t);
        const read = Bt3Data.prototype.read; Bt3Data.prototype.read = function (rel, off, dst, n) {
          if (rel.endsWith('Test Stage.unk')) window.stageReads.push(performance.now());  /* (no call into the engine here) */
          return read.call(this, rel, off, dst, n); }; }, 1); })();` });
    await b.send('Page.navigate', { url: `http://127.0.0.1:${TEST_PORT}/index.html?autostart=1&env=BT3_STAGES=gamedata/test_extras/stages,BT3_TEST_STAGE=0x24` });
    let readAt = null, frameNow = -1;
    for (const end = Date.now() + BATTLE_WAIT_MS; Date.now() < end;) {
      await sleep(1000);
      const s = JSON.parse(await Promise.race([ev(`JSON.stringify({ f: window.bt3Module ? window.bt3Module.ccall('Bt3Ui_Get','number',['string'],['frame']) : -1, n: (window.stageReads || []).length })`),
                                               sleep(5000).then(() => '{"hung":true}')]));
      if (s.hung) break;
      frameNow = s.f;
      if (s.n > 0 && readAt === null) readAt = s.f;
      if (readAt !== null && frameNow >= readAt + BATTLE_RUN_FRAMES) break;
    }
    await b.send('Page.removeScriptToEvaluateOnNewDocument', { identifier });
    check(readAt !== null, `the attract battle on the added stage reads its file (by frame ${readAt})`);
    check(readAt !== null && frameNow >= readAt + BATTLE_RUN_FRAMES, `... and the game runs on (frame ${frameNow}, ${BATTLE_RUN_FRAMES} frames past the read)`);

    // ---- a server that does not list folders ----
    await b.send('Page.enable');
    await b.send('Page.addScriptToEvaluateOnNewDocument', { source: `(() => { const f = window.fetch; window.fetch = (u, o) =>
      String(u).endsWith('?list') ? Promise.resolve(new Response('<html>a folder page</html>', { status: 200, headers: { 'Content-Type': 'text/html' } })) : f(u, o); })();` });
    r = await run(',BT3_EXTRA_STAGES=0x24');
    check(r.state.listable === 0, `listing not available (${r.state.listable})`);
    check(r.lines.some((l) => l.startsWith('data: the data folder cannot be looked through')), 'page log: the data folder cannot be looked through, with the reason');
    check(notAvailable(r.lines, 'added songs') === 1 && notAvailable(r.lines, 'texture packs') === 1 &&
          r.lines.some((l) => l.includes('the data server does not list its folders')), 'log: songs and texture packs not available, with the reason');
    check(notAvailable(r.lines, 'added stages') === 0 && r.state.stages === 1, 'a stage named through BT3_EXTRA_STAGES is still found (no "not available" line)');
    check(r.panel[1].startsWith('Songs: not available: the data server does not list its folders') &&
          r.panel[2].startsWith('Texture packs: not available: the data server does not list its folders'), `panel: ${r.panel.join(' | ')}`);
  } finally {
    await b.close();
    server.kill();
    await sleep(500);
    fs.rmSync(tmp, { recursive: true, force: true });
  }
  console.log(failed ? `RESULT: FAIL (${failed})` : 'RESULT: PASS');
  process.exit(failed ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
