// The browser build's settings panel: the page's own HTML, beside the game (the canvas shows the game alone). Every
// value lives in the engine (port/src/web/web_ui.c, the Bt3Ui_* exports); nothing here keeps its own copy. A change is
// sent to the engine, which applies it between two frames and answers with `changed`, after which the panel reads the
// values again. The engine's settings key (F1, START + SELECT, or the bound one) opens and closes the panel too.
'use strict';

// What a PS2 frame is drawn at (GsGpu_ReadFrame, gs_draw.h GS_DISPLAY_W / _H): the internal resolution's 1x.
const GS_DISPLAY = { w: 512, h: 448 };
// SDL_GamepadButton values (SDL3) and the engine's two trigger sources (ui.h PORT_PAD_LT / PORT_PAD_RT).
const PAD_LABELS = ['A / Cross', 'B / Circle', 'X / Square', 'Y / Triangle', 'Back / Select', 'Guide', 'Start',
  'Left stick click', 'Right stick click', 'Left shoulder', 'Right shoulder', 'D-pad up', 'D-pad down', 'D-pad left',
  'D-pad right', 'Misc 1', 'Right paddle 1', 'Left paddle 1', 'Right paddle 2', 'Left paddle 2', 'Touchpad'];
const PAD_TRIGGERS = { 100: 'Left trigger', 101: 'Right trigger' };
const CAPTURE_KEY = 1, CAPTURE_PAD = 2;
// The game's pad bits (include/sys/pad.h, as Bt3Ui_PadButtons gives them) by the engine's action labels: the picture's test view
const PICTURE_BITS = { SELECT: 0, L3: 1, R3: 2, START: 3, 'D-pad up': 4, 'D-pad right': 5, 'D-pad down': 6, 'D-pad left': 7,
  L2: 8, R2: 9, L1: 10, R1: 11, Triangle: 12, Circle: 13, Cross: 14, Square: 15 };
const NOTICE_MS = 6000;
// The unlock is carried out by the game at its next vertical blank (headless.c): the panel looks for the result this long.
const UNLOCK_WAIT_MS = 2000, UNLOCK_POLL_MS = 100;
// The renderers the browser build has (gs_draw.c: BT3_GPU_API), chosen with the page's ?gpu=.
const RENDERERS = [['gl', 'WebGL 2'], ['webgpu', 'WebGPU']];
// web_extras.c: stages, songs and texture packs have all been looked for (bits 1, 2, 4)
const ADDED_ALL_CHECKED = 7;

// The engine's start-up switches the developer panel can restart with (each read once at start).
const DEBUG_SWITCHES = [
  { env: 'BT3_GS_VERBOSE', label: 'Renderer counts in the log every 30 frames (with WebGL 2 this checks GL errors each frame: slower)' },
  { env: 'BT3_FX_DEBUG', label: 'Outline pass debug view' },
  { env: 'BT3_GPU_MAGENTA', label: 'Clear render targets to magenta (shows what is never drawn)' },
  { query: 'gldebug', label: 'Check every WebGL call for errors (WebGL 2 only; very slow)' },
];

// The settings, by tab. key: the engine's name (web_ui.c kSettings). A control is one of select / checkbox / range;
// `bit` makes a checkbox one bit of the value (shown as on when the bit is clear: "fx_off" counts what is off).
const PANEL = [
  { tab: 'Video', items: [
    { key: 'scale', label: 'Internal resolution', type: 'select',
      options: [1, 2, 3, 4, 5, 6, 7, 8].map((n) => [n, `${n}× (${GS_DISPLAY.w * n} × ${GS_DISPLAY.h * n})`]),
      help: 'How finely the game is drawn. 1× is the PlayStation 2\'s own resolution; higher is sharper and uses more video memory.' },
    { key: 'aspect_milli', label: 'Aspect ratio', type: 'select',
      options: [[1333, '4:3 (original)'], [1600, '16:10'], [1778, '16:9'], [2389, '21:9'], [3556, '32:9']],
      help: 'Wider than 4:3 shows more of the fight to the sides. Menus are stretched.' },
    { key: 'smooth_2d', label: '2D filtering', type: 'select', options: [[0, 'Sharp (console)'], [1, 'Smooth']],
      help: 'Above 1×: sharp keeps each PlayStation 2 pixel a block, smooth samples it per screen pixel.' },
    { key: 'interp', label: 'Smooth motion', type: 'select', limit: (s) => s.get('interp_limit'),
      options: [[0, 'Off (30 pictures a second, as the console)'], [1, '60 pictures a second'], [2, '120 pictures a second'],
                [3, '240 pictures a second']],
      help: 'In-between pictures drawn between the game’s 30 steps a second; the game itself does not change. ' +
            'In a browser only what was measured to reach the screen evenly is offered: 60 with WebGPU. With WebGL 2 the ' +
            'extra pictures cost so much that real ones had to be left out (uneven motion), so it is off.' },
    { key: 'hud_layout', label: 'HUD style', type: 'select',
      options: [[0, "At the screen's edges"], [1, 'At the edges, long health bars'], [2, 'In the middle, as in 4:3']],
      help: "Where a fight's health bars and gauges sit in a picture wider than 4:3." },
    { key: 'menus_43', label: 'Menus in 4:3', type: 'checkbox',
      help: 'In a picture wider than 4:3, the menus are shown at their own shape with black at the sides instead of stretched. Fights are not changed.' },
    { key: 'frame_skip', label: 'Skip pictures instead of slowing down', type: 'checkbox',
      help: 'When this machine cannot show a picture in time, the game stays at full speed and leaves pictures out instead (choppier, not slower).' },
    { key: 'texture_pack', label: 'Texture pack', type: 'checkbox', when: (s) => s.get('texture_pack_count') > 0,
      help: 'Replacement textures found next to the game.' },
  ] },
  { tab: 'Effects', items: [
    ...[['Outline', 'The black line around the fighters.'], ['See-through tint', 'The tint that shows a fighter behind scenery.'],
      ['Depth tint', 'The haze that colours distant things.'], ['Glare and glow', 'The bloom around bright things and the sky\'s glare.'],
      ['Distance blur', 'The soft focus on distant scenery.'], ['Movement blur', 'The blur of the scenery while the view turns or moves fast.']]
      .map(([label, help], bit) => ({ key: 'fx_off', bit, label, help, type: 'checkbox' })),
    { key: 'glow', label: 'Glow strength', type: 'range', min: 0, max: 200, unit: '%',
      help: '100% is what the PlayStation 2 draws; the default is 60%.' },
  ] },
  { tab: 'Audio', items: [
    { key: 'music', label: 'Music', type: 'range', min: 0, max: 200, unit: '%' },
    { key: 'effects', label: 'Sound effects', type: 'range', min: 0, max: 200, unit: '%',
      help: 'Hits, blasts, menu sounds. Takes effect with the next sound that starts.' },
    { key: 'voices', label: 'Voices', type: 'range', min: 0, max: 200, unit: '%',
      help: "The fighters' voices in a fight and the spoken lines outside one." },
    { key: 'stage_sounds', label: 'Stage sounds', type: 'range', min: 0, max: 200, unit: '%',
      help: "A stage's background sound (wind, water) and its own effects." },
    { key: 'music_follows_speed', label: 'Music follows game speed', type: 'checkbox',
      help: 'At a game speed other than 100% (Extras): off, the music plays as it is; on, it is sped up or slowed down with the game, at its own pitch.' },
  ] },
  { tab: 'Controls', items: [
    { bindings: true },
    { controllers: true },
    { heading: 'Gameplay' },
    { key: 'clash_square', label: 'Clashes: Square counts as a direction', type: 'checkbox',
      help: 'In a beam or rush clash, each press of Square counts like a d-pad tap. Works online.' },
    { key: 'clash_square_x', label: 'Worth of one press', type: 'select', when: (s) => s.get('clash_square') === 1,
      options: [[4, '1'], [6, '1.5'], [7, '1.75'], [8, '2']] },
  ] },
  { tab: 'Saves', items: [{ saves: true }] },
  { tab: 'Touch', items: [{ touch: true }] },
  { tab: 'Extras', items: [
    { key: 'game_speed', label: 'Game speed', type: 'range', min: 50, max: 200, unit: '%', reset: 100,
      help: "How fast the game runs (the sound keeps its pitch). 100% is the game's own; 114% is what competitive play uses." },
    { key: 'skip_logos', label: 'Skip logos', type: 'checkbox',
      help: 'Start without the publisher and developer logos. From the next start (the next time this page is loaded).' },
    { key: 'skip_intro', label: 'Skip intro video', type: 'checkbox',
      help: 'Start without the opening movie. From the next start (the next time this page is loaded).' },
    { key: 'skip_copyright', label: 'Skip copyright page', type: 'checkbox',
      help: 'The page of legal text shown for five seconds before the title screen, every time.' },
    { unlock: true },
    { added: true },
  ] },
];

class Bt3Settings {
  constructor({ canvas, stage, renderer, query, log, data, getLog, saves }) {
    this.canvas = canvas;
    this.stage = stage;
    this.renderer = renderer;
    this.query = query;
    this.log = log;
    this.mod = null;
    this.engineReady = false;  // the engine's runtime is up (its `ready`): only then may it be called
    this.player = 0;
    this.capturing = null;  // the button waiting for a key or controller button
    this.chosen = -1;       // the action chosen on the controller picture (its bindings show under it)
    this.pad = null;        // the controller picture (gamepad.svg) once loaded: { svg, controls: Map action -> its group }
    this.opener = null;     // what had the focus before the panel opened (it gets it back)
    this.refreshers = [];   // one per control: shows the engine's value again
    this.dialog = document.getElementById('settings');
    this.saves = saves;  // the game's saves in the browser's storage (saves.js): the Saves tab
    this.captureListener = null;  // the guided set-up's, while it runs: the engine's captureDone goes there
    this.controllers = new Bt3Controllers(this);  // the Controls tab's controller part (controllers.js)
    this.touch = new Bt3Touch({ stage, settings: this, log });  // the on-screen touch controls and the Touch tab (touch.js)
    // the Developer section's live numbers and the diagnostics export (dashboard.js)
    this.dashboard = new Bt3Dashboard({ root: document.getElementById('dev-dashboard'), renderer, data, log, getLog,
                                        getSettings: () => this.snapshot() });
    this.openButton = document.getElementById('settings-open');
    this.noticeEl = document.getElementById('notice');
    this.build();
    this.openButton.addEventListener('click', () => this.toggle());
    document.getElementById('settings-close').addEventListener('click', () => this.close());
    document.getElementById('settings-backdrop').addEventListener('click', () => { if (!this.capturing) this.close(); });
    document.getElementById('fullscreen').addEventListener('click', () => this.fullscreen());
    document.addEventListener('keydown', (e) => this.onKey(e));
    // The engine quits on Escape (a desktop program's rule, gs_draw.c GsDraw_PollEvents). A browser tab has no "quit",
    // and Escape is how a page leaves full screen: it is kept from the game (caught on the way to the canvas), except
    // while a binding is being taken, where Escape means "keep it".
    stage.addEventListener('keydown', (e) => {
      if (e.key !== 'Escape' || this.capturing) return;
      e.stopPropagation();
      if (document.fullscreenElement) document.exitFullscreen();
    }, true);
    document.addEventListener('fullscreenchange', () => this.fitStage());
  }

  // The handlers the engine calls (web_ui.c page_call / page_notice / page_screenshot): Module.bt3Ui.
  handlers() {
    return {
      ready: () => { this.engineReady = true; this.openButton.disabled = false; this.refresh(); this.fitStage(); this.touch.apply(); },
      changed: () => { this.persist(); this.refresh(); this.fitStage(); },
      toggle: () => this.toggle(),
      captureDone: () => this.captureDone(),
      notice: (text) => this.notice(text),
      screenshot: (rgba, w, h) => this.saveScreenshot(rgba, w, h),
      cardWritten: () => this.saves.cardWritten(),
    };
  }

  attach(mod) {
    this.mod = mod;
    this.dashboard.attach(mod);
  }

  // the GPU's name as the page's checks found it (shown in the Developer section)
  set gpuName(name) { this.dashboard.gpuName = name; }
  get gpuName() { return this.dashboard.gpuName; }

  // ---- the engine's interface ------------------------------------------------------------------------------------
  get(name) { return this.mod.ccall('Bt3Ui_Get', 'number', ['string'], [name]); }
  set(name, value) {
    if (!this.mod.ccall('Bt3Ui_Set', 'number', ['string', 'number'], [name, value])) this.notice(`The setting "${name}" was not changed (see the log).`);
  }
  action(name, arg = 0) {
    if (!this.mod.ccall('Bt3Ui_Action', 'number', ['string', 'number'], [name, arg])) this.notice(`"${name}" could not be done (see the log).`);
  }
  call(name, ret, args) { return this.mod.ccall(name, ret, args.map((a) => typeof a === 'string' ? 'string' : 'number'), args); }

  // The engine's settings file (plat_settings.c writes bt3_settings.txt in its file system) kept in the browser between
  // visits: written back into the file system before the engine starts (restore), saved after every change.
  static restore(fs) {
    try {
      const text = localStorage.getItem('bt3.settings');
      if (text) fs.writeFile('bt3_settings.txt', text);
    } catch (e) { /* storage blocked: the engine starts with its defaults */ }
  }
  persist() {
    try {
      if (this.mod.FS.analyzePath('bt3_settings.txt').exists) localStorage.setItem('bt3.settings', this.mod.FS.readFile('bt3_settings.txt', { encoding: 'utf8' }));
    } catch (e) { this.log(`settings: not kept for the next visit (${e.message})`); }
  }

  // The engine's current value of every setting the panel shows (for the diagnostics export).
  snapshot() {
    const keys = new Set(PANEL.flatMap((t) => t.items).filter((i) => i.key).map((i) => i.key));
    return Object.fromEntries([...keys].map((k) => [k, this.get(k)]));
  }

  // ---- building the panel ----------------------------------------------------------------------------------------
  build() {
    const tabs = document.getElementById('settings-tabs'), pages = document.getElementById('settings-pages');
    PANEL.forEach((t, i) => {
      const id = `tab-${t.tab.toLowerCase()}`;
      const tab = el('button', { role: 'tab', id, type: 'button', 'aria-controls': `${id}-page`, tabindex: i ? '-1' : '0',
                                'aria-selected': String(!i) }, t.tab);
      const page = el('div', { role: 'tabpanel', id: `${id}-page`, 'aria-labelledby': id, class: 'page' });
      page.hidden = i !== 0;
      tab.addEventListener('click', () => this.selectTab(tab));
      tabs.appendChild(tab);
      for (const item of t.items) {
        page.appendChild(item.heading ? el('h3', {}, item.heading) : item.bindings ? this.buildBindings() : item.controllers ? this.controllers.root : item.saves ? this.saves.buildUi()
                         : item.unlock ? this.buildUnlock() : item.added ? this.buildAdded() : item.touch ? this.touch.buildSettings()
                         : this.buildControl(item));
      }
      if (t.tab === 'Video') page.appendChild(this.buildRenderer());
      pages.appendChild(page);
    });
    tabs.addEventListener('keydown', (e) => {
      const list = [...tabs.children], at = list.indexOf(document.activeElement);
      const next = e.key === 'ArrowRight' ? at + 1 : e.key === 'ArrowLeft' ? at - 1 : e.key === 'Home' ? 0 : e.key === 'End' ? list.length - 1 : null;
      if (next === null || at < 0) return;
      e.preventDefault();
      this.selectTab(list[(next + list.length) % list.length]);
    });
    document.getElementById('dev').appendChild(this.buildDeveloper());
    document.getElementById('dev').addEventListener('toggle', (e) => this.dashboard.run(e.target.open && this.dialog.open));
  }

  selectTab(tab) {
    for (const t of tab.parentElement.children) {
      const on = t === tab;
      t.setAttribute('aria-selected', String(on));
      t.tabIndex = on ? 0 : -1;
      document.getElementById(t.getAttribute('aria-controls')).hidden = !on;
    }
    tab.focus();
  }

  buildControl(item) {
    const id = `set-${item.key}${item.bit !== undefined ? '-' + item.bit : ''}`;
    const row = el('div', { class: 'row' });
    let input;
    if (item.type === 'select') {
      input = el('select', { id });
      for (const [v, text] of item.options) input.appendChild(el('option', { value: String(v) }, text));
      input.addEventListener('change', () => this.set(item.key, Number(input.value)));
      row.append(el('label', { for: id }, item.label), input);
    } else if (item.type === 'checkbox') {
      input = el('input', { id, type: 'checkbox' });
      input.addEventListener('change', () => {
        if (item.bit === undefined) { this.set(item.key, input.checked ? 1 : 0); return; }
        const off = this.get(item.key), mask = 1 << item.bit;
        this.set(item.key, input.checked ? off & ~mask : off | mask);
      });
      row.classList.add('check');
      row.append(input, el('label', { for: id }, item.label));
    } else {
      input = el('input', { id, type: 'range', min: String(item.min), max: String(item.max), step: '1' });
      const out = el('output', { for: id });
      input.addEventListener('input', () => { out.textContent = `${input.value}${item.unit}`; });
      input.addEventListener('change', () => this.set(item.key, Number(input.value)));
      row.append(el('label', { for: id }, item.label), input, out);
      if (item.reset !== undefined) {
        const reset = el('button', { type: 'button', class: 'secondary' }, `Reset to ${item.reset}${item.unit}`);
        reset.addEventListener('click', () => this.set(item.key, item.reset));
        row.appendChild(reset);
      }
      this.refreshers.push(() => { out.textContent = `${input.value}${item.unit}`; });
    }
    if (item.help) {
      const help = el('p', { class: 'help', id: `${id}-help` }, item.help);
      input.setAttribute('aria-describedby', help.id);
      row.appendChild(help);
    }
    this.refreshers.unshift(() => {
      const v = this.get(item.key);
      if (item.limit) {  // options above what the engine allows here are not offered (the select is off when none are)
        const max = item.limit(this);
        for (const o of input.options) o.hidden = o.disabled = Number(o.value) > max;
        input.disabled = max === 0;
      }
      if (item.type === 'checkbox') input.checked = item.bit === undefined ? v !== 0 : !((v >> item.bit) & 1);
      else input.value = String(v);
      row.hidden = item.when ? !item.when(this) : false;
    });
    return row;
  }

  // Which renderer: the page's ?gpu= (the game starts again).
  buildRenderer() {
    const row = el('div', { class: 'row' });
    const select = el('select', { id: 'set-renderer' });
    for (const [v, text] of RENDERERS) select.appendChild(el('option', { value: v }, text));
    select.value = this.renderer;
    const restart = el('button', { type: 'button', class: 'secondary' }, 'Restart with this renderer');
    restart.hidden = true;
    select.addEventListener('change', () => { restart.hidden = select.value === this.renderer; });
    restart.addEventListener('click', () => {
      const q = new URLSearchParams(this.query);
      q.set('gpu', select.value);
      location.search = q.toString();
    });
    row.append(el('label', { for: 'set-renderer' }, 'Renderer'), select, restart,
      el('p', { class: 'help' }, 'Both draw the same picture. Changing it restarts the game: progress since the last save is lost.'));
    return row;
  }

  buildBindings() {
    const box = el('div', { class: 'bindings' });
    const players = el('div', { class: 'segmented', role: 'radiogroup', 'aria-label': 'Player' });
    for (const p of [0, 1]) {
      const id = `bind-player-${p}`;
      const radio = el('input', { type: 'radio', name: 'bind-player', id, value: String(p) });
      radio.checked = p === 0;
      radio.addEventListener('change', () => { this.player = p; this.refresh(); });
      players.append(radio, el('label', { for: id }, `Player ${p + 1}`));
    }
    const slot = el('select', { id: 'bind-slot' });
    slot.addEventListener('change', () => { this.call('Bt3Ui_PadSlotSet', null, [this.player, Number(slot.value)]); this.persist(); });
    const reset = el('button', { type: 'button', class: 'secondary' }, 'Reset to defaults');
    reset.addEventListener('click', () => this.action('reset_controls', this.player));
    const top = el('div', { class: 'row inline who' }, players, el('label', { for: 'bind-slot' }, 'uses'), slot);
    const q = this.controllers.quick;
    const quick = el('div', { class: 'row inline quick' }, q.padSetup, q.keySetup, q.testButton, q.testKeys, reset);
    const all = el('details', { class: 'more' }, el('summary', {}, 'All bindings as a list'));
    const table = el('table');
    table.appendChild(el('thead', {}, el('tr', {}, el('th', { scope: 'col' }, 'Action'), el('th', { scope: 'col' }, 'Keyboard'),
      el('th', { scope: 'col' }, 'Controller'))));
    const body = el('tbody');
    table.appendChild(body);
    const picture = el('div', { class: 'gp-picture' });
    const chosen = el('div', { class: 'gp-chosen', 'aria-live': 'polite' });
    all.appendChild(table);
    // (the test lights the picture: the controllers part's own test cells are not shown here)
    box.append(q.status, top, this.controllers.warning, picture, chosen, quick, q.wizardBox, all);
    this.loadPicture(picture);
    this.refreshers.push(() => {
      const count = this.call('Bt3Ui_GamepadCount', 'number', []), current = this.call('Bt3Ui_PadSlot', 'number', [this.player]);
      slot.replaceChildren(el('option', { value: '-1' }, 'None'));
      for (let i = 0; i < Math.max(count, 4); i++) {
        const name = i < count ? this.call('Bt3Ui_GamepadName', 'string', [i]) : '';
        slot.appendChild(el('option', { value: String(i) }, name ? `${i + 1}: ${name}` : `Controller ${i + 1} (not connected)`));
      }
      slot.value = String(current);
      if (this.capturing) return;  // (the row being bound keeps its "press a key" until the engine answers)
      const actions = this.get('actions'), padButtons = this.get('pad_buttons');
      this.showChosen(chosen, actions, padButtons);
      this.showPicture(actions, padButtons);
      body.replaceChildren();
      for (const a of [actions, ...Array.from({ length: actions }, (_, i) => i)]) {  // the settings key first, then the pad
        const cells = [el('th', { scope: 'row' }, this.call('Bt3Ui_ActionLabel', 'string', [a]))];
        cells.push(el('td', {}, this.bindButton(CAPTURE_KEY, a)));
        cells.push(el('td', {}, a < padButtons || a === actions ? this.bindButton(CAPTURE_PAD, a)
                                                                 : el('span', { class: 'dim' }, a < padButtons + 4 ? 'Left stick' : 'Right stick')));
        const row = el('tr', { 'data-action': String(a) }, ...cells);
        row.classList.toggle('chosen', a === this.chosen);
        body.appendChild(row);
      }
    });
    return box;
  }

  bindingName(kind, action) {
    const v = this.call('Bt3Ui_Binding', 'number', [kind, this.player, action]);
    if (kind === CAPTURE_KEY) return v > 0 ? this.call('Bt3Ui_KeyName', 'string', [v]) : '—';
    if (v < 0) return action === this.get('actions') ? 'START + SELECT' : '—';
    return PAD_TRIGGERS[v] || PAD_LABELS[v] || `Button ${v}`;
  }

  // ---- the controller picture (gamepad.svg): a view of the same bindings. Choosing a control shows its two bindings
  // under the picture (the table's own buttons: the engine takes the next key or button); the picture marks what has
  // no binding, what is being bound, and, during the Test of the bindings, what the game receives.
  async loadPicture(box) {
    let text;
    try {
      const r = await fetch('gamepad.svg');
      if (!r.ok) throw new Error(`HTTP ${r.status}`);
      text = await r.text();
    } catch (e) {
      box.replaceChildren(el('p', { class: 'help' }, `The controller picture could not be loaded (${e.message}): the table below has every binding.`));
      this.log(`controller picture: ${e.message}`);
      return;
    }
    box.innerHTML = text;
    const svg = box.querySelector('svg');
    for (const g of svg.querySelectorAll('.gp-control')) {
      g.addEventListener('click', () => this.choose(g));
      g.addEventListener('keydown', (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); this.choose(g); } });
    }
    this.pad = { svg, controls: new Map() };
    if (this.engineReady) this.refresh();
  }

  choose(g) {
    if (!this.pad || this.capturing) return;
    for (const [a, c] of this.pad.controls) if (c === g) this.chosen = a;
    this.refresh();
    const first = this.dialog.querySelector('.gp-chosen button.bind');
    if (first) first.focus({ preventScroll: true });  // (the picture stays where it is)
  }

  // the engine's action labels name the picture's controls (data-action): an action index for each
  showPicture(actions, padButtons) {
    if (!this.pad) return;
    if (!this.pad.controls.size) {
      const byLabel = new Map([...this.pad.svg.querySelectorAll('.gp-control')].map((g) => [g.dataset.action, g]));
      for (let a = 0; a < actions; a++) {
        const g = byLabel.get(this.call('Bt3Ui_ActionLabel', 'string', [a]));
        if (g) this.pad.controls.set(a, g);
      }
    }
    for (const [a, g] of this.pad.controls) {
      const key = this.call('Bt3Ui_Binding', 'number', [CAPTURE_KEY, this.player, a]);
      const pad = a < padButtons ? this.call('Bt3Ui_Binding', 'number', [CAPTURE_PAD, this.player, a]) : -1;
      g.classList.toggle('is-unbound', key <= 0 && pad < 0);
      g.classList.toggle('is-selected', a === this.chosen);
      g.classList.toggle('is-capturing', !!this.capturing && this.capturing.action === a);
      g.setAttribute('aria-pressed', String(a === this.chosen));
    }
  }

  showChosen(box, actions, padButtons) {
    const a = this.chosen;
    if (a < 0 || a >= actions) {
      box.replaceChildren(el('p', { class: 'help' }, 'Click a button on the controller to change it. A dashed outline: nothing set yet.'));
      return;
    }
    const keyB = this.bindButton(CAPTURE_KEY, a, 'pick');
    const padB = a < padButtons ? this.bindButton(CAPTURE_PAD, a, 'pick') : el('span', { class: 'dim' }, a < padButtons + 4 ? 'Left stick' : 'Right stick');
    box.replaceChildren(el('strong', {}, this.call('Bt3Ui_ActionLabel', 'string', [a])),
      el('label', {}, el('span', { class: 'dim' }, 'Keyboard '), keyB), el('label', {}, el('span', { class: 'dim' }, 'Controller '), padB),
      el('p', { class: 'help' }, 'Click a binding, then press the new key or button. Esc cancels, Delete clears.'));
  }

  // what the game receives during the Test of the bindings (controllers.js) on the picture; held null: nothing shown
  showHeld(held, sticks) {
    if (!this.pad) return;
    const pushed = (v) => v < 0x80 - STICK_DEAD ? -1 : v > 0x80 + STICK_DEAD ? 1 : 0;
    const on = (label) => {
      if (held === null) return false;
      const bit = PICTURE_BITS[label];
      if (bit !== undefined) return ((held >> bit) & 1) === 1;
      const m = /^(Left|Right) stick (up|down|left|right)$/.exec(label);
      if (!m) return false;
      const shift = m[1] === 'Left' ? 16 : 0, x = pushed((sticks >>> (shift + 8)) & 0xFF), y = pushed((sticks >>> shift) & 0xFF);
      return { up: y < 0, down: y > 0, left: x < 0, right: x > 0 }[m[2]];
    };
    for (const g of this.pad.controls.values()) g.classList.toggle('is-pressed', on(g.dataset.action));
  }

  bindButton(kind, action, where = '') {
    const b = el('button', { type: 'button', class: `bind ${where}`.trim(), 'data-kind': String(kind), 'data-action': String(action) },
      this.bindingName(kind, action));
    b.addEventListener('click', () => {
      if (this.captureListener) { this.notice('Finish or cancel the guided set-up first.'); return; }  // (one capture at a time)
      this.capturing = { kind, action, where };
      this.showPicture(this.get('actions'), this.get('pad_buttons'));
      b.textContent = kind === CAPTURE_KEY ? 'Press a key…' : 'Press a button…';
      b.classList.add('waiting');
      this.call('Bt3Ui_CaptureBegin', null, [kind, this.player, action]);
      // the key goes to the engine, which hears the keyboard on the canvas (SDL_EMSCRIPTEN_KEYBOARD_ELEMENT)
      if (kind === CAPTURE_KEY) this.canvas.focus();
    });
    return b;
  }

  captureDone() {
    if (this.captureListener) { this.captureListener(); return; }
    const c = this.capturing;
    this.capturing = null;
    this.persist();
    this.refresh();
    if (c && this.dialog.open) {  // the same binding in the rebuilt table gets the focus back
      const sel = `button.bind[data-kind="${c.kind}"][data-action="${c.action}"]`;
      (this.dialog.querySelector(c.where ? `${sel}.${c.where}` : `${sel}:not(.pick)`) || this.dialog.querySelector(sel)).focus();
    }
  }

  buildUnlock() {
    const box = el('div', { class: 'row' });
    const confirm = document.getElementById('unlock-confirm');
    const button = el('button', { type: 'button', class: 'secondary' }, 'Unlock everything…');
    const done = el('span', { class: 'ok-text', role: 'status' });
    button.addEventListener('click', () => confirm.showModal());
    confirm.addEventListener('close', async () => {
      if (confirm.returnValue !== 'unlock') return;
      this.action('unlock_all');
      for (let waited = 0; waited < UNLOCK_WAIT_MS && !this.get('unlock_done'); waited += UNLOCK_POLL_MS) {
        await new Promise((r) => setTimeout(r, UNLOCK_POLL_MS));
      }
      if (!this.get('unlock_done')) this.notice('Nothing was unlocked: the game has no save loaded.');
      this.refresh();
    });
    box.append(el('h3', {}, 'Unlock everything'),
      el('p', { class: 'help' }, 'All characters, stages, music and items, and the most Zenni: a debug function the game\'s developers left in. ' +
        'It also empties your records list, and becomes permanent the next time the game saves. Use it once your save is loaded (from the main menu on).'),
      button, done);
    const why = el('p', { class: 'help' }, 'Available once the game has loaded a save (from the main menu on).');
    box.appendChild(why);
    this.refreshers.push(() => {
      const loaded = this.get('save_loaded') === 1;
      button.disabled = !loaded;
      why.hidden = loaded;
      done.textContent = this.get('unlock_done') ? 'Done.' : '';
    });
    return box;
  }

  // Content added from outside the disc (the engine's web_extras.c): what was found, or why nothing can be.
  buildAdded() {
    const box = el('div', { class: 'row' });
    const kinds = [
      { label: 'Stages', key: 'added_stage_count', one: 'added stage', many: 'added stages', folder: 'stages' },
      { label: 'Songs', key: 'added_song_count', one: 'added song', many: 'added songs', folder: 'songs' },
      { label: 'Texture packs', key: 'texture_pack_count', one: 'replacement texture', many: 'replacement textures', folder: 'textures' },
    ];
    const lines = kinds.map(() => el('p', { class: 'help' }));
    box.append(el('h3', {}, 'Added stages, songs and texture packs'), ...lines);
    const noListing = 'not available: the data server does not list its folders, so files added to the data folder cannot be found ' +
      '(port/web/devserver.py lists them).';
    this.refreshers.push(() => {
      const looked = this.get('added_checked') === ADDED_ALL_CHECKED, listable = this.get('data_listable') === 1;
      kinds.forEach((k, i) => {
        const n = this.get(k.key);
        lines[i].textContent = `${k.label}: ` + (!looked ? 'not looked for yet (the game looks while it starts).'
          : n > 0 ? `${n} ${n === 1 ? k.one : k.many}.`
          : !listable ? noListing
          : `none (the data folder has no ${k.folder} folder with any).`);
      });
    });
    return box;
  }

  buildDeveloper() {
    const shot = el('button', { type: 'button', class: 'secondary' }, `Save a screenshot (${GS_DISPLAY.w} × ${GS_DISPLAY.h} PNG)`);
    shot.addEventListener('click', () => this.action('screenshot'));
    const env = new Set((new URLSearchParams(this.query).get('env') || '').split(',').filter(Boolean).map((kv) => kv.split('=')[0]));
    const switches = DEBUG_SWITCHES.map((s, i) => {
      const id = `dev-switch-${i}`;
      const box = el('input', { type: 'checkbox', id });
      box.checked = s.env ? env.has(s.env) : new URLSearchParams(this.query).has(s.query);
      box.dataset.index = String(i);
      return el('div', { class: 'row check' }, box, el('label', { for: id }, s.label));
    });
    const restart = el('button', { type: 'button', class: 'secondary' }, 'Restart with these switches');
    restart.addEventListener('click', () => {
      const q = new URLSearchParams(this.query);
      const kept = (q.get('env') || '').split(',').filter((kv) => kv && !DEBUG_SWITCHES.some((s) => s.env && kv.startsWith(s.env + '=')));
      switches.forEach((row) => {
        const box = row.querySelector('input'), s = DEBUG_SWITCHES[Number(box.dataset.index)];
        if (s.env && box.checked) kept.push(`${s.env}=1`);
        if (s.query) { if (box.checked) q.set(s.query, '1'); else q.delete(s.query); }
      });
      if (kept.length) q.set('env', kept.join(',')); else q.delete('env');
      location.search = q.toString();
    });
    return el('div', {}, shot, el('h3', {}, 'Start-up switches'),
      el('p', { class: 'help' }, 'The engine reads these once, at start: applying them restarts the game.'), ...switches, restart);
  }

  // ---- opening, closing, keys ------------------------------------------------------------------------------------
  toggle() { if (this.dialog.open) this.close(); else this.open(); }

  open() {
    if (!this.mod || this.dialog.open) return;
    this.opener = document.activeElement;
    this.touch.releaseAll();  // (nothing held by a finger stays held while the panel is open)
    this.call('Bt3Ui_SetOpen', null, [1]);
    this.refresh();
    this.dialog.show();
    this.openButton.setAttribute('aria-expanded', 'true');
    const tab = this.dialog.querySelector('[role=tab][aria-selected=true]');
    tab.focus();
    if (document.getElementById('dev').open) this.dashboard.run(true);
  }

  close() {
    if (!this.dialog.open) return;
    if (this.capturing) { this.call('Bt3Ui_CaptureCancel', null, []); }
    this.call('Bt3Ui_SetOpen', null, [0]);
    this.dialog.close();
    this.dashboard.run(false);
    this.controllers.closed();
    this.openButton.setAttribute('aria-expanded', 'false');
    // back to the game (its keyboard is the canvas's), unless the panel was opened from another control of the page
    (this.opener && this.opener !== document.body && this.opener !== this.openButton ? this.opener : this.canvas).focus();
  }

  // Only while the panel is open: Escape closes it (unless the engine is waiting for a key: Esc is then its "keep").
  onKey(e) {
    if (!this.dialog.open || e.key !== 'Escape' || this.capturing) return;
    e.preventDefault();
    this.close();
  }

  refresh() {
    if (!this.mod) return;
    for (const r of this.refreshers) r();
    this.controllers.refresh();
    this.saves.render();
  }

  // The stage's shape follows the picture's (the engine sizes the canvas to the aspect ratio: GsDraw_WindowShape).
  fitStage() {
    if (!this.mod) return;
    this.stage.style.setProperty('--aspect', String(this.get('aspect_milli') / 1000));
  }

  fullscreen() {
    if (document.fullscreenElement) { document.exitFullscreen(); return; }
    this.stage.requestFullscreen().then(() => this.canvas.focus(), (e) => this.notice(`Full screen was refused: ${e.message}`));
  }

  notice(text) {
    this.log(`notice: ${text}`);
    this.noticeEl.textContent = text;
    this.noticeEl.hidden = false;
    clearTimeout(this.noticeTimer);
    this.noticeTimer = setTimeout(() => { this.noticeEl.hidden = true; }, NOTICE_MS);
  }

  saveScreenshot(rgba, w, h) {
    const c = document.createElement('canvas');
    c.width = w;
    c.height = h;
    c.getContext('2d').putImageData(new ImageData(new Uint8ClampedArray(rgba.buffer, rgba.byteOffset, rgba.byteLength), w, h), 0, 0);
    c.toBlob((blob) => {
      const a = el('a', { href: URL.createObjectURL(blob), download: `bt3_frame_${this.get('frame')}.png` });
      a.click();
      setTimeout(() => URL.revokeObjectURL(a.href), 0);
    }, 'image/png');
  }
}

// A DOM element with attributes and children (strings become text).
function el(tag, attrs = {}, ...children) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v);
  for (const c of children) e.append(c);
  return e;
}
