// The Controls tab's controller part (port/web/settings.js mounts it): the controllers the browser shows the page, a
// live view of one, a warning for a player whose controller is not connected, guided set-up of a player's controller
// buttons or keys, keyboard presets, and a test of the bindings.
//
// The bindings are the engine's (gs_input.c: a key per action, a controller button per pad button, a controller slot
// per player, the n-th controller SDL lists); changes go through it: the engine itself takes the next key or button
// (Bt3Ui_CaptureBegin, as the table above does), presets and duplicate fixes set them (Bt3Ui_BindingSet), and the test
// shows what the game would receive (Bt3Ui_InputTest: the engine's own read, the game handed a released pad). What the
// browser tells a page about a controller is little: an id text, a "standard" layout or none, button and axis counts.
'use strict';

const CAPTURE = { key: 1, pad: 2 };
// Keyboard presets, by the engine's action labels (PortInput_ActionLabel) and SDL's key names: only what the engine's
// model holds (one key per action). "Defaults" is the engine's own (reset_keys).
const KEY_PRESETS = [
  { name: 'Defaults (arrows, K L J I, W A S D as the left stick)', engineDefaults: true },
  { name: 'Left hand on W A S D (d-pad), arrows as the left stick', keys: {
    'D-pad up': 'W', 'D-pad down': 'S', 'D-pad left': 'A', 'D-pad right': 'D', Cross: 'K', Circle: 'L', Square: 'J', Triangle: 'I',
    L1: 'U', R1: 'O', L2: 'Q', R2: 'E', L3: 'Z', R3: 'X', START: 'Return', SELECT: 'Right Shift',
    'Left stick up': 'Up', 'Left stick down': 'Down', 'Left stick left': 'Left', 'Left stick right': 'Right' } },
  { name: 'Numeric keypad (a second player beside the first)', keys: {
    'D-pad up': 'Keypad 8', 'D-pad down': 'Keypad 5', 'D-pad left': 'Keypad 4', 'D-pad right': 'Keypad 6',
    Cross: 'Keypad 2', Circle: 'Keypad 3', Square: 'Keypad 1', Triangle: 'Keypad 0', L1: 'Keypad 7', R1: 'Keypad 9',
    L2: 'Keypad /', R2: 'Keypad *', START: 'Keypad Enter', SELECT: 'Keypad +' } },
];
const CONNECT_SETTLE_MS = 300;     // the engine's list of controllers follows the browser's within this
const STICK_DEAD = 0x30;          // how far from the centre (0x80) a stick counts as pushed in the test
const PAD_BUTTON_BITS = 16;       // the game's pad bits shown in the test (include/sys/pad.h)
// Chrome: "... (STANDARD GAMEPAD Vendor: 054c Product: 0ce6)"; Firefox: "054c-0ce6-..."
const VENDOR_PRODUCT = [/Vendor:\s*([0-9a-f]{4})\s*Product:\s*([0-9a-f]{4})/i, /^([0-9a-f]{4})-([0-9a-f]{4})-/i];

class Bt3Controllers {
  constructor(settings) {
    this.s = settings;
    this.wizard = null;   // the guided set-up under way
    this.testing = false;
    this.loop = 0;
    this.viewIndex = -1;  // the browser controller shown live
    this.connected = new Map();  // browser index -> id, of the controllers the browser has said are there
    this.toasts = new Bt3Toasts(document.getElementById('toasts'));
    this.root = this.build();
    // each controller on its own: one leaving says so for that one only; one coming back is announced again
    window.addEventListener('gamepadconnected', (e) => {
      const { index, id } = e.gamepad;
      this.s.log(`controller connected: ${id} (browser index ${index})`);
      if (this.connected.get(index) === id) return;  // (the same one told twice)
      this.connected.set(index, id);
      this.toasts.show('ok', 'Controller connected', gamepadDisplayName(id));
      this.changed();
    });
    window.addEventListener('gamepaddisconnected', (e) => {
      const { index, id } = e.gamepad;
      this.s.log(`controller disconnected: ${id} (browser index ${index})`);
      this.connected.delete(index);
      this.toasts.show('warn', 'Controller disconnected', gamepadDisplayName(id));
      if (this.wizard && this.wizard.kind === CAPTURE.pad) this.wizardCancel('the controller was disconnected');
      this.changed();
    });
  }

  // the panel's lists follow at once, and again once the engine (SDL, at its next look at the browser) has the change
  changed() {
    const again = () => (this.s.engineReady ? this.s.refresh() : this.refresh());
    again();
    clearTimeout(this.changedTimer);
    this.changedTimer = setTimeout(again, CONNECT_SETTLE_MS);
  }

  call(name, ret, args) { return this.s.call(name, ret, args); }
  player() { return this.s.player; }

  // ---- what the browser and the engine see ------------------------------------------------------------------------
  browserPads() { return [...(navigator.getGamepads ? navigator.getGamepads() : [])].filter(Boolean); }

  static vendorProduct(id) {
    for (const re of VENDOR_PRODUCT) {
      const m = re.exec(id);
      if (m) return parseInt(m[1], 16) << 16 | parseInt(m[2], 16);
    }
    return 0;
  }

  // engine slots (SDL's list) with their browser controller where it can be told: same vendor and product, else the
  // same name, each browser controller used once
  enginePads() {
    const n = this.call('Bt3Ui_GamepadCount', 'number', []), pads = this.browserPads(), used = new Set(), out = [];
    for (let i = 0; i < n; i++) {
      const name = this.call('Bt3Ui_GamepadName', 'string', [i]), vp = this.call('Bt3Ui_GamepadVendorProduct', 'number', [i]) >>> 0;
      const match = pads.find((p) => !used.has(p.index) && vp && Bt3Controllers.vendorProduct(p.id) === vp) ||
                    pads.find((p) => !used.has(p.index) && name && p.id.startsWith(name.slice(0, 20)));
      if (match) used.add(match.index);
      out.push({ slot: i, name, browserIndex: match ? match.index : -1 });
    }
    return out;
  }

  // ---- the part of the tab ----------------------------------------------------------------------------------------
  build() {
    const el = (tag, attrs = {}, ...kids) => { const e = document.createElement(tag); for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v); e.append(...kids); return e; };
    this.el = el;
    this.status = el('p', { class: 'pad-status' });  // always there: which controllers are connected, or none
    this.warning = el('p', { class: 'warning', role: 'alert' });
    this.warning.hidden = true;
    this.list = el('ul', { class: 'pads', 'aria-live': 'polite' });
    this.viewSelect = el('select', { id: 'pad-view', 'aria-label': 'Controller to show' });
    this.viewSelect.addEventListener('change', () => { this.viewIndex = Number(this.viewSelect.value); this.buildView(); });
    this.view = el('div', { class: 'padview' });
    const padSetup = el('button', { type: 'button', class: 'secondary' }, 'Set up all controller buttons…');
    padSetup.addEventListener('click', () => this.wizardStart(CAPTURE.pad));
    const keySetup = el('button', { type: 'button', class: 'secondary' }, 'Set up all keys…');
    keySetup.addEventListener('click', () => this.wizardStart(CAPTURE.key));
    const padDefaults = el('button', { type: 'button', class: 'secondary' }, 'Controller buttons to defaults');
    padDefaults.addEventListener('click', () => this.s.action('reset_pads', this.player()));
    this.wizardBox = el('div', { class: 'wizard', role: 'group', 'aria-label': 'Guided set-up' });
    this.wizardBox.hidden = true;
    this.presetSelect = el('select', { id: 'key-preset' });
    KEY_PRESETS.forEach((p, i) => this.presetSelect.appendChild(el('option', { value: String(i) }, p.name)));
    const presetApply = el('button', { type: 'button', class: 'secondary' }, 'Use these keys');
    presetApply.addEventListener('click', () => this.applyPreset(KEY_PRESETS[Number(this.presetSelect.value)]));
    this.testButton = el('button', { type: 'button', class: 'secondary', 'aria-pressed': 'false' }, 'Test');
    this.testButton.addEventListener('click', () => this.setTest(!this.testing));
    this.testKeys = el('button', { type: 'button', class: 'secondary' }, 'Test keys too');
    this.testKeys.addEventListener('click', () => this.s.canvas.focus());
    this.testKeys.hidden = true;
    this.testView = el('div', { class: 'padtest', 'aria-live': 'off' });
    this.testView.hidden = true;
    this.testCells = [];  // (made with the engine's names on the first refresh: the engine is not there yet)
    this.testSticks = el('span', { class: 'dim' });
    // the Controls tab's main part takes these (settings.js buildBindings): the guided set-ups and the test
    this.quick = { status: this.status, padSetup, keySetup, wizardBox: this.wizardBox, testButton: this.testButton, testKeys: this.testKeys };
    // the rest, folded away
    return el('details', { class: 'more controllers' },
      el('summary', {}, 'Connected controllers and keyboard presets'),
      el('p', { class: 'help' }, 'Not listed? Press any button on the controller: browsers show a controller only after that.'),
      this.list,
      el('div', { class: 'row inline' }, el('label', { for: 'pad-view' }, 'Live view'), this.viewSelect), this.view,
      el('div', { class: 'row inline' }, el('label', { for: 'key-preset' }, 'Keyboard preset'), this.presetSelect, presetApply),
      el('div', { class: 'row inline' }, padDefaults));
  }

  // the lists, the warning (the panel's refresh, a connection event)
  refresh() {
    if (!this.s.mod) return;
    if (!this.testCells.length) {
      this.testCells = Array.from({ length: PAD_BUTTON_BITS }, (_, i) => this.el('span', { class: 'cell' }, this.call('Bt3Ui_PadBitName', 'string', [i])));
      this.testView.replaceChildren(...this.testCells, this.testSticks);
    }
    const pads = this.browserPads(), engine = this.enginePads();
    const items = pads.map((p) => {
      const slot = engine.find((e) => e.browserIndex === p.index);
      // whether the game can use it is the engine's list (SDL): the standard layout, or a controller SDL's own database knows
      // the engine's list (SDL) decides: in a browser SDL takes every controller, with the standard layout if the browser
      // reports it, else from its database of makes or, failing that, a default guess
      const usable = !slot ? 'not listed by the game yet'
        : `the game's controller ${slot.slot + 1}` + (p.mapping === 'standard' ? ''
          : ' — the browser reports no standard layout: its buttons may be placed wrongly; check them in the live view and set them up');
      return this.el('li', {}, `${p.id} — browser index ${p.index}, layout ${p.mapping || 'none'}, ${p.buttons.length} buttons, ${p.axes.length} axes; ${usable}`);
    });
    this.list.replaceChildren(...(items.length ? items : [this.el('li', { class: 'dim' }, 'No controller visible: connect one and press any of its buttons.')]));
    this.status.classList.toggle('none', !pads.length);
    this.status.textContent = pads.length
      ? `${pads.length === 1 ? 'Controller connected' : `${pads.length} controllers connected`}: ${pads.map((p) => gamepadDisplayName(p.id)).join(', ')}`
      : 'No controller connected. Plug one in and press any button on it. The keyboard works meanwhile.';
    const slot = this.call('Bt3Ui_PadSlot', 'number', [this.player()]);
    // (only when a controller is connected but not the chosen one: someone on the keyboard needs no warning)
    this.warning.hidden = !(engine.length && slot >= 0 && slot >= engine.length);
    this.warning.textContent = `Player ${this.player() + 1} uses controller ${slot + 1}, which is not connected: choose a connected one above.`;
    const keep = this.viewIndex;
    this.viewSelect.replaceChildren(...pads.map((p) => this.el('option', { value: String(p.index) }, `${p.index}: ${p.id.slice(0, 40)}`)));
    this.viewIndex = pads.some((p) => p.index === keep) ? keep : pads.length ? pads[0].index : -1;
    this.viewSelect.value = String(this.viewIndex);
    this.viewSelect.disabled = !pads.length;
    this.buildView();
    this.ensureLoop();
  }

  // the live view's cells, made once per controller shown (updated in place by the loop)
  buildView() {
    const p = this.browserPads().find((q) => q.index === this.viewIndex);
    if (!p) { this.view.replaceChildren(); this.viewCells = null; return; }
    this.viewCells = { buttons: p.buttons.map((_, i) => this.el('span', { class: 'cell' }, String(i))),
                       axes: p.axes.map((_, i) => this.el('meter', { min: '-1', max: '1', value: '0', title: `axis ${i}` })) };
    this.view.replaceChildren(this.el('div', { class: 'cells' }, ...this.viewCells.buttons), this.el('div', { class: 'axes' }, ...this.viewCells.axes));
  }

  // one update a display refresh while this part is visible (it stops itself when not)
  ensureLoop() {
    if (this.loop) return;
    const step = () => {
      if (!this.root.offsetParent || !this.s.dialog.open) { this.loop = 0; return; }
      const p = this.viewCells && this.browserPads().find((q) => q.index === this.viewIndex);
      if (p) {
        p.buttons.forEach((b, i) => this.viewCells.buttons[i] && this.viewCells.buttons[i].classList.toggle('on', b.pressed));
        p.axes.forEach((v, i) => { if (this.viewCells.axes[i]) this.viewCells.axes[i].value = v; });
      }
      if (this.testing) {
        const held = this.call('Bt3Ui_PadButtons', 'number', [1, this.player()]), sticks = this.call('Bt3Ui_PadSticks', 'number', [1, this.player()]) >>> 0;
        this.testCells.forEach((c, i) => c.classList.toggle('on', ((held >> i) & 1) === 1));
        this.s.showHeld(held, sticks);
        const dir = (x, y) => [y < 0x80 - STICK_DEAD ? 'up' : y > 0x80 + STICK_DEAD ? 'down' : '', x < 0x80 - STICK_DEAD ? 'left' : x > 0x80 + STICK_DEAD ? 'right' : ''].filter(Boolean).join('-') || 'centre';
        this.testSticks.textContent = `left stick ${dir(sticks >>> 24, (sticks >> 16) & 0xFF)}, right stick ${dir((sticks >> 8) & 0xFF, sticks & 0xFF)}`;
      }
      this.loop = requestAnimationFrame(step);
    };
    this.loop = requestAnimationFrame(step);
  }

  setTest(on) {
    this.testing = on;
    this.call('Bt3Ui_InputTest', null, [on ? 1 : 0]);
    this.testButton.setAttribute('aria-pressed', String(on));
    this.testButton.textContent = on ? 'Stop the test' : 'Test the bindings';
    this.testView.hidden = !on;
    if (!on) this.s.showHeld(null, 0);
    this.testKeys.hidden = !on;
    if (on) this.ensureLoop();
  }

  // the panel closed: the engine ends the test itself (Bt3Ui_SetOpen); the page follows
  closed() {
    if (this.testing) this.setTest(false);
    if (this.wizard) this.wizardCancel('the panel was closed');
  }

  // ---- presets ----------------------------------------------------------------------------------------------------
  applyPreset(preset) {
    const p = this.player();
    if (preset.engineDefaults) { this.s.action('reset_keys', p); return; }
    const actions = this.call('Bt3Ui_Get', 'number', ['actions']);
    const labels = Array.from({ length: actions }, (_, a) => this.call('Bt3Ui_ActionLabel', 'string', [a]));
    const unknown = [], codes = labels.map((label) => {
      const name = preset.keys[label];
      if (!name) return 0;
      const code = this.call('Bt3Ui_ScancodeFromName', 'number', [name]);
      if (!code) unknown.push(name);
      return code;
    });
    if (unknown.length) { this.s.notice(`The preset names keys SDL does not know (${unknown.join(', ')}): nothing was changed.`); return; }
    codes.forEach((code, a) => this.call('Bt3Ui_BindingSet', null, [CAPTURE.key, p, a, code]));
    const other = 1 - p, clash = codes.filter((c, a) => c && Array.from({ length: actions }, (_, b) => this.call('Bt3Ui_Binding', 'number', [CAPTURE.key, other, b])).includes(c));
    this.s.persist();
    this.s.refresh();
    this.s.notice(`Keys for player ${p + 1}: ${preset.name}.` + (clash.length ? ` ${clash.length} of them are also player ${other + 1}’s.` : ''));
  }

  // ---- guided set-up ----------------------------------------------------------------------------------------------
  // Each action in turn: the engine takes the next key or button. A value another action of the player already has is
  // offered to swap, keep or choose again. Esc (keys) or Skip keeps an action as it is; Back goes one back; Cancel puts
  // everything back as it was before the set-up.
  wizardStart(kind) {
    if (this.wizard) return;
    if (this.testing) this.setTest(false);
    const p = this.player(), count = kind === CAPTURE.pad ? this.call('Bt3Ui_Get', 'number', ['pad_buttons']) : this.call('Bt3Ui_Get', 'number', ['actions']);
    if (kind === CAPTURE.pad && !this.call('Bt3Ui_GamepadCount', 'number', [])) {
      this.s.notice('No controller the game can use is connected: connect one and press any of its buttons.');
      return;
    }
    const before = Array.from({ length: count }, (_, a) => this.call('Bt3Ui_Binding', 'number', [kind, p, a]));
    this.wizard = { kind, player: p, count, before, at: 0, changed: 0 };
    this.s.captureListener = () => this.wizardCaptured();
    this.wizardBox.hidden = false;
    this.wizardStep();
  }

  wizardButtons(...specs) {
    return specs.map(([label, fn]) => { const b = this.el('button', { type: 'button', class: 'secondary' }, label); b.addEventListener('click', fn); return b; });
  }

  wizardStep() {
    const w = this.wizard;
    if (w.at >= w.count) { this.wizardEnd(); return; }
    const label = this.call('Bt3Ui_ActionLabel', 'string', [w.at]), what = w.kind === CAPTURE.key ? 'key' : 'controller button';
    w.value = this.call('Bt3Ui_Binding', 'number', [w.kind, w.player, w.at]);
    const [skip, back, cancel] = this.wizardButtons(['Skip', () => this.wizardSkip()], ['Back', () => this.wizardBack()], ['Cancel', () => this.wizardCancel()]);
    back.disabled = w.at === 0;
    const onPicture = this.s.pad && this.s.pad.controls.has(w.at);  // (the settings key is not on the controller picture)
    this.wizardBox.replaceChildren(
      this.el('p', { role: 'status' }, this.el('span', { class: 'dim' }, `${w.at + 1} / ${w.count} · `),
              onPicture ? `Press the ${what} for the glowing button (` : `Press the ${what} for `, this.el('strong', {}, label), onPicture ? ')' : '',
              w.kind === CAPTURE.key ? this.el('span', { class: 'dim' }, ' · Esc keeps it') : ''),
      this.el('div', { class: 'row inline' }, skip, back, cancel));
    this.s.capturing = { kind: w.kind, action: w.at, wizard: true };
    this.s.showPicture(this.call('Bt3Ui_Get', 'number', ['actions']), this.call('Bt3Ui_Get', 'number', ['pad_buttons']));  // (it glows there)
    this.call('Bt3Ui_CaptureBegin', null, [w.kind, w.player, w.at]);
    if (w.kind === CAPTURE.key) this.s.canvas.focus(); else skip.focus();  // (keys reach the engine on the game area)
  }

  wizardCaptured() {
    const w = this.wizard;
    this.s.capturing = null;
    if (!w) return;
    this.s.showPicture(this.call('Bt3Ui_Get', 'number', ['actions']), this.call('Bt3Ui_Get', 'number', ['pad_buttons']));
    if (w.ignoreDone) { w.ignoreDone = false; return; }  // (the engine's answer to a Skip or Back: already handled)
    const now = this.call('Bt3Ui_Binding', 'number', [w.kind, w.player, w.at]);
    if (now === w.value) { w.at++; this.wizardStep(); return; }  // (kept: Esc)
    w.changed++;
    const none = w.kind === CAPTURE.key ? 0 : -1;
    const dup = now === none ? -1 : Array.from({ length: w.count }, (_, a) => a).find((a) => a !== w.at && this.call('Bt3Ui_Binding', 'number', [w.kind, w.player, a]) === now);
    if (dup === undefined || dup < 0) { w.at++; this.wizardStep(); return; }
    const other = this.call('Bt3Ui_ActionLabel', 'string', [dup]), mine = this.call('Bt3Ui_ActionLabel', 'string', [w.at]);
    const [swap, keep, again] = this.wizardButtons(
      [`Swap (${other} gets ${mine}’s old one)`, () => { this.call('Bt3Ui_BindingSet', null, [w.kind, w.player, dup, w.value]); w.at++; this.wizardStep(); }],
      ['Keep both', () => { w.at++; this.wizardStep(); }],
      ['Choose again', () => { this.call('Bt3Ui_BindingSet', null, [w.kind, w.player, w.at, w.value]); w.changed--; this.wizardStep(); }]);
    this.wizardBox.replaceChildren(this.el('p', { role: 'alert' }, `That is already ${other}’s.`), this.el('div', { class: 'row inline' }, swap, keep, again));
    swap.focus();
  }

  wizardSkip() {
    this.s.capturing = null;
    this.wizard.ignoreDone = true;
    this.call('Bt3Ui_CaptureCancel', null, []);  // (the engine answers with captureDone: ignored, the step moves on here)
    this.wizard.at++;
    this.wizardStep();
  }

  wizardBack() {
    this.s.capturing = null;
    this.wizard.ignoreDone = true;
    this.call('Bt3Ui_CaptureCancel', null, []);
    this.wizard.at = Math.max(0, this.wizard.at - 1);
    this.wizardStep();
  }

  wizardCancel(why) {
    const w = this.wizard;
    if (!w) return;
    this.wizard = null;  // (first: the captureDone the cancel brings is not a step)
    this.s.capturing = null;
    this.call('Bt3Ui_CaptureCancel', null, []);
    w.before.forEach((v, a) => this.call('Bt3Ui_BindingSet', null, [w.kind, w.player, a, v]));
    this.wizardClose(`Set-up cancelled${why ? ` (${why})` : ''}: the bindings are as they were.`);
  }

  wizardEnd() {
    const w = this.wizard;
    this.wizard = null;
    this.wizardClose(`Set-up done: ${w.changed} of ${w.count} changed.`);
  }

  wizardClose(message) {
    this.s.captureListener = null;
    this.s.persist();
    this.s.refresh();
    this.wizardBox.replaceChildren(this.el('p', { role: 'status' }, message));
    if (this.s.dialog.open) this.testButton.focus();
  }
}
