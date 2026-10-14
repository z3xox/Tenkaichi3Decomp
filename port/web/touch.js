// On-screen touch controls (docs/BROWSER_TOUCH_PLAN.md): an overlay in the game's stage (#stage, which also goes full
// screen) with a d-pad, a left analog stick, the face buttons, the shoulder buttons, START, SELECT, L3 and R3. They hold
// the engine's actions as held keys do: the engine merges them where it reads the keyboard (gs_input.c Port_PadRead,
// PortInput_VirtualActions; web_ui.c Bt3Ui_TouchSet), through the same action table, as player 1. Actions are named by
// the engine's own labels (Bt3Ui_ActionLabel), as the keyboard presets do: no second table of actions.
//
// Input: pointer events with pointer capture, each pointer its own (a map from pointer id to what it holds); the held
// actions are the union of every live pointer's, sent to the engine when they change. Everything is let go on
// pointerup / pointercancel / lostpointercapture, when the window loses the focus, the page is hidden or left, the
// controls are hidden or edited, and when the settings panel opens.
//
// Preferences (shown, size, opacity, hand, the layout) are the page's own, kept in localStorage 'bt3.touch', apart
// from the engine's settings file ('bt3.settings') and from the game's saves (IndexedDB).
'use strict';

const TOUCH_STORE = 'bt3.touch';
const TOUCH_VERSION = 1;
const TOUCH_MODES = [['auto', 'When this device has a touch screen'], ['on', 'Always'], ['off', 'Never']];
const TOUCH_SIZE = { min: 60, max: 160, step: 5, def: 100 };       // % of the default sizes
const TOUCH_OPACITY = { min: 15, max: 100, step: 5, def: 55 };     // % opacity of the controls (pressed: full)
const TOUCH_SCALE = { min: 0.5, max: 2, step: 0.1 };               // one control's own size factor (the editor)
const TOUCH_STICK_RANGE = 0x7F;           // the stick's reach either side of the centre (0x80): 0x01..0xFF
const TOUCH_STICK_CENTRE = 0x80;
const TOUCH_STICK_DEAD = 0.15;            // of the stick's radius: no deflection under this
const TOUCH_KNOB_TRAVEL = 58.7;           // % of the knob's width its centre moves at full deflection: (50% - 23%) / 46% of the stick
const TOUCH_DPAD_DEAD = 0.25;             // of the d-pad's radius: no direction under this
const TOUCH_DPAD_DIAGONAL = Math.PI / 8;  // half the angle of a direction's sector beyond which the neighbour joins (8-way)

// The controls. x, y: the centre, as fractions of the controls' box (right-handed; mirrored for the left hand);
// size: as a fraction of the box's shorter side. actions: engine action labels.
const TOUCH_CONTROLS = [
  { id: 'stick', kind: 'stick', label: 'Left stick', x: 0.14, y: 0.70, size: 0.30 },
  { id: 'dpad', kind: 'dpad', label: 'D-pad', x: 0.14, y: 0.34, size: 0.26,
    dirs: { up: 'D-pad up', down: 'D-pad down', left: 'D-pad left', right: 'D-pad right' } },
  { id: 'triangle', kind: 'button', label: '△', name: 'Triangle', action: 'Triangle', x: 0.86, y: 0.52, size: 0.12, tone: 'triangle' },
  { id: 'square', kind: 'button', label: '□', name: 'Square', action: 'Square', x: 0.77, y: 0.66, size: 0.12, tone: 'square' },
  { id: 'circle', kind: 'button', label: '○', name: 'Circle', action: 'Circle', x: 0.95, y: 0.66, size: 0.12, tone: 'circle' },
  { id: 'cross', kind: 'button', label: '✕', name: 'Cross', action: 'Cross', x: 0.86, y: 0.80, size: 0.12, tone: 'cross' },
  { id: 'l1', kind: 'shoulder', label: 'L1', action: 'L1', x: 0.07, y: 0.08, size: 0.11 },
  { id: 'l2', kind: 'shoulder', label: 'L2', action: 'L2', x: 0.19, y: 0.08, size: 0.11 },
  { id: 'r2', kind: 'shoulder', label: 'R2', action: 'R2', x: 0.81, y: 0.08, size: 0.11 },
  { id: 'r1', kind: 'shoulder', label: 'R1', action: 'R1', x: 0.93, y: 0.08, size: 0.11 },
  { id: 'select', kind: 'pill', label: 'SELECT', action: 'SELECT', x: 0.42, y: 0.93, size: 0.09 },
  { id: 'start', kind: 'pill', label: 'START', action: 'START', x: 0.58, y: 0.93, size: 0.09 },
  { id: 'l3', kind: 'small', label: 'L3', action: 'L3', x: 0.29, y: 0.86, size: 0.08 },
  { id: 'r3', kind: 'small', label: 'R3', action: 'R3', x: 0.71, y: 0.86, size: 0.08 },
];
// a control's box as a multiple of its size (width, height): round ones are square, the others wider
const TOUCH_SHAPE = { stick: [1, 1], dpad: [1, 1], button: [1, 1], small: [1, 1], shoulder: [1.4, 0.7], pill: [1.6, 0.55] };

class Bt3Touch {
  constructor({ stage, settings, log }) {
    this.stage = stage;
    this.settings = settings;   // the engine (settings.call) and the panel
    this.log = log;
    this.prefs = Bt3Touch.load();
    this.pointers = new Map();  // pointer id -> { control, actions (bits), stick: {x, y} | null }
    this.sent = { actions: 0, lx: -1, ly: -1 };
    this.actionBit = null;      // engine action label -> its bit (from the engine, once it runs)
    this.editing = null;        // the editor: { selected: control id }
    this.coarse = window.matchMedia('(any-pointer: coarse)');
    this.landscape = window.matchMedia('(orientation: landscape)');
    this.build();
    const releaseAll = () => this.releaseAll();
    window.addEventListener('blur', releaseAll);
    window.addEventListener('pagehide', releaseAll);
    document.addEventListener('visibilitychange', () => { if (document.hidden) this.releaseAll(); });
    window.addEventListener('resize', () => this.layout());
    this.coarse.addEventListener('change', () => this.apply());
    this.landscape.addEventListener('change', () => this.apply());
    this.apply();
  }

  // ---- preferences ---------------------------------------------------------------------------------------------
  static defaults() {
    return { version: TOUCH_VERSION, mode: 'auto', size: TOUCH_SIZE.def, opacity: TOUCH_OPACITY.def, hand: 'right', layout: {} };
  }
  static load() {
    const d = Bt3Touch.defaults();
    try {
      const p = JSON.parse(localStorage.getItem(TOUCH_STORE) || 'null');
      if (p && p.version === TOUCH_VERSION) return { ...d, ...p, layout: { ...(p.layout || {}) } };
    } catch (e) { /* (nothing kept, or not readable: the defaults) */ }
    return d;
  }
  save() {
    try { localStorage.setItem(TOUCH_STORE, JSON.stringify(this.prefs)); } catch (e) { this.log(`touch: preferences not kept (${e.message})`); }
  }
  set(changes) { Object.assign(this.prefs, changes); this.save(); this.apply(); }
  resetLayout() { this.prefs.layout = {}; this.save(); this.apply(); }

  // shown: on, or auto on a device with a touch screen
  shown() { return this.prefs.mode === 'on' || (this.prefs.mode === 'auto' && this.coarse.matches); }

  // ---- the overlay -----------------------------------------------------------------------------------------------
  build() {
    this.root = el('div', { id: 'touch', 'aria-hidden': 'true' });
    this.box = el('div', { class: 'touch-box' });
    this.root.append(this.box);
    this.portrait = el('p', { class: 'touch-portrait' }, 'Turn the device sideways: the controls are laid out for landscape.');
    this.root.append(this.portrait);
    this.els = new Map();
    for (const c of TOUCH_CONTROLS) {
      const e = el('div', { class: `touch-ctl touch-${c.kind}`, 'data-id': c.id, 'data-tone': c.tone || '' });
      if (c.kind === 'stick') {
        e.append(el('div', { class: 'touch-knob' }));
      } else if (c.kind === 'dpad') {
        for (const d of ['up', 'right', 'down', 'left']) e.append(el('span', { class: `touch-arm touch-${d}` }));
      } else {
        e.append(el('span', { class: 'touch-label' }, c.label));
      }
      e.addEventListener('pointerdown', (ev) => this.down(ev, c));
      e.addEventListener('pointermove', (ev) => this.move(ev, c));
      for (const t of ['pointerup', 'pointercancel', 'lostpointercapture']) e.addEventListener(t, (ev) => this.up(ev));
      e.addEventListener('contextmenu', (ev) => ev.preventDefault());
      this.els.set(c.id, e);
      this.box.append(e);
    }
    this.toolbar = this.buildToolbar();
    this.root.append(this.toolbar);
    this.stage.append(this.root);
  }

  // shows or hides the overlay, and lays it out
  apply() {
    const shown = this.shown() || this.editing !== null;
    if (!shown) this.releaseAll();
    this.root.hidden = !shown;
    this.root.style.setProperty('--touch-opacity', String(this.prefs.opacity / 100));
    this.root.classList.toggle('editing', this.editing !== null);
    // the play layout: on a touch screen, the stage fills the window while the controls are shown
    document.body.classList.toggle('touch-play', shown && this.coarse.matches);
    this.portrait.hidden = !shown || this.landscape.matches;
    this.toolbar.hidden = this.editing === null;
    this.layout();
    if (this.settings.engineReady) this.settings.fitStage();
  }

  // where control `c` is: its own layout, else the default, mirrored for the left hand
  place(c) {
    const own = this.prefs.layout[c.id] || {};
    const x = own.x !== undefined ? own.x : (this.prefs.hand === 'left' ? 1 - c.x : c.x);
    return { x, y: own.y !== undefined ? own.y : c.y, scale: own.scale || 1 };
  }

  // size of control `c` in pixels (width, height) in a box of the given size
  extent(c, boxW, boxH, scale) {
    const unit = Math.min(boxW, boxH) * c.size * (this.prefs.size / 100) * scale;
    const [sw, sh] = TOUCH_SHAPE[c.kind];
    return [unit * sw, unit * sh];
  }

  layout() {
    if (this.root.hidden) return;
    const r = this.box.getBoundingClientRect();
    for (const c of TOUCH_CONTROLS) {
      const p = this.place(c), [w, h] = this.extent(c, r.width, r.height, p.scale), e = this.els.get(c.id);
      // kept whole inside the box (the safe area is the box's padding: index.html)
      const cx = Math.min(Math.max(p.x * r.width, w / 2), r.width - w / 2), cy = Math.min(Math.max(p.y * r.height, h / 2), r.height - h / 2);
      Object.assign(e.style, { width: `${w}px`, height: `${h}px`, left: `${cx - w / 2}px`, top: `${cy - h / 2}px` });
      e.classList.toggle('selected', this.editing !== null && this.editing.selected === c.id);
    }
  }

  // ---- input -----------------------------------------------------------------------------------------------------
  // the engine's action bits by label (from the engine itself)
  bits() {
    if (!this.actionBit && this.settings.engineReady) {
      const n = this.settings.get('actions');
      this.actionBit = new Map(Array.from({ length: n }, (_, a) => [this.settings.call('Bt3Ui_ActionLabel', 'string', [a]), 1 << a]));
    }
    return this.actionBit;
  }
  bit(label) {
    const b = this.bits();
    if (!b) return 0;
    if (!b.has(label)) throw new Error(`touch: the engine has no action "${label}"`);
    return b.get(label);
  }

  down(ev, c) {
    ev.preventDefault();
    if (this.editing !== null) { this.editDown(ev, c); return; }
    if (c.kind === 'stick' && [...this.pointers.values()].some((p) => p.control === c)) return;  // one finger per stick
    try { ev.currentTarget.setPointerCapture(ev.pointerId); } catch (e) { /* (a pointer the browser no longer has) */ }
    this.pointers.set(ev.pointerId, { control: c, actions: 0, stick: null });
    this.track(ev, c);
  }
  move(ev, c) {
    if (this.editing !== null) { this.editMove(ev, c); return; }
    if (this.pointers.has(ev.pointerId)) this.track(ev, c);
  }
  up(ev) {
    if (this.editing !== null) { this.editUp(ev); return; }
    if (!this.pointers.delete(ev.pointerId)) return;
    this.send();
  }

  // what pointer `ev` holds on control `c` now
  track(ev, c) {
    const p = this.pointers.get(ev.pointerId), r = this.els.get(c.id).getBoundingClientRect();
    const dx = (ev.clientX - (r.left + r.width / 2)) / (r.width / 2), dy = (ev.clientY - (r.top + r.height / 2)) / (r.height / 2);
    if (c.kind === 'dpad') {
      p.actions = 0;
      if (Math.hypot(dx, dy) >= TOUCH_DPAD_DEAD) {
        const a = Math.atan2(dy, dx);  // 0: right, pi/2: down
        const near = (target) => Math.abs(Math.atan2(Math.sin(a - target), Math.cos(a - target))) <= Math.PI / 4 + TOUCH_DPAD_DIAGONAL;
        if (near(0)) p.actions |= this.bit(c.dirs.right);
        if (near(Math.PI / 2)) p.actions |= this.bit(c.dirs.down);
        if (near(Math.PI) || near(-Math.PI)) p.actions |= this.bit(c.dirs.left);
        if (near(-Math.PI / 2)) p.actions |= this.bit(c.dirs.up);
      }
    } else if (c.kind === 'stick') {
      const len = Math.hypot(dx, dy), k = len > 1 ? 1 / len : 1;
      const x = dx * k, y = dy * k, live = Math.hypot(x, y) >= TOUCH_STICK_DEAD;
      p.stick = { x: live ? x : 0, y: live ? y : 0 };
    } else {
      p.actions = this.bit(c.action);
    }
    this.send();
  }

  // the union of every live pointer's, to the engine when it changed; the pressed looks
  send() {
    let actions = 0, stick = null;
    const pressed = new Set();
    for (const p of this.pointers.values()) {
      actions |= p.actions;
      if (p.stick) stick = p.stick;
      if (p.actions || p.stick) pressed.add(p.control.id);
    }
    const lx = stick ? Math.round(TOUCH_STICK_CENTRE + stick.x * TOUCH_STICK_RANGE) : -1, ly = stick ? Math.round(TOUCH_STICK_CENTRE + stick.y * TOUCH_STICK_RANGE) : -1;
    for (const [id, e] of this.els) e.classList.toggle('pressed', pressed.has(id));
    const knob = this.els.get('stick').querySelector('.touch-knob');
    knob.style.transform = stick ? `translate(${stick.x * TOUCH_KNOB_TRAVEL}%, ${stick.y * TOUCH_KNOB_TRAVEL}%)` : '';
    const dpad = this.els.get('dpad'), dirs = TOUCH_CONTROLS.find((c) => c.id === 'dpad').dirs;
    for (const d of Object.keys(dirs)) dpad.querySelector(`.touch-${d}`).classList.toggle('on', !!(actions & this.bit(dirs[d])));
    if (actions === this.sent.actions && lx === this.sent.lx && ly === this.sent.ly) return;
    this.sent = { actions, lx, ly };
    if (this.settings.engineReady) this.settings.call('Bt3Ui_TouchSet', null, [actions >>> 0, lx, ly]);
  }

  // every pointer let go (interruptions; the settings panel opening)
  releaseAll() {
    for (const id of this.pointers.keys()) {
      for (const e of this.els.values()) { try { if (e.hasPointerCapture(id)) e.releasePointerCapture(id); } catch (err) { /* (gone) */ } }
    }
    this.pointers.clear();
    this.send();
  }

  // ---- the layout editor -----------------------------------------------------------------------------------------
  buildToolbar() {
    const bar = el('div', { class: 'touch-toolbar', role: 'toolbar', 'aria-label': 'Touch layout editor' });
    this.selName = el('span', { class: 'touch-selname' }, 'Drag a control to move it.');
    const smaller = el('button', { type: 'button' }, 'Smaller'), bigger = el('button', { type: 'button' }, 'Bigger');
    const opacity = el('input', { type: 'range', min: String(TOUCH_OPACITY.min), max: String(TOUCH_OPACITY.max), step: String(TOUCH_OPACITY.step), 'aria-label': 'Opacity' });
    const reset = el('button', { type: 'button' }, 'Reset layout'), done = el('button', { type: 'button', class: 'primary' }, 'Done');
    smaller.addEventListener('click', () => this.scaleSelected(-TOUCH_SCALE.step));
    bigger.addEventListener('click', () => this.scaleSelected(TOUCH_SCALE.step));
    opacity.addEventListener('input', () => this.set({ opacity: Number(opacity.value) }));
    reset.addEventListener('click', () => this.resetLayout());
    done.addEventListener('click', () => this.stopEdit());
    this.opacityInput = opacity;
    bar.append(this.selName, smaller, bigger, el('label', { class: 'touch-opacity' }, 'Opacity ', opacity), reset, done);
    return bar;
  }

  startEdit() {
    this.releaseAll();
    this.editing = { selected: null, drag: null };
    this.opacityInput.value = String(this.prefs.opacity);
    this.selName.textContent = 'Drag a control to move it.';
    this.apply();
  }
  stopEdit() {
    this.editing = null;
    this.save();
    this.apply();
  }
  editDown(ev, c) {
    try { ev.currentTarget.setPointerCapture(ev.pointerId); } catch (e) { /* (gone) */ }
    const r = this.box.getBoundingClientRect(), p = this.place(c);
    this.editing.selected = c.id;
    this.editing.drag = { id: ev.pointerId, control: c, dx: p.x * r.width - (ev.clientX - r.left), dy: p.y * r.height - (ev.clientY - r.top) };
    this.selName.textContent = c.name || c.label;
    this.layout();
  }
  editMove(ev, c) {
    const d = this.editing.drag;
    if (!d || d.id !== ev.pointerId) return;
    const r = this.box.getBoundingClientRect(), p = this.place(c);
    const [w, h] = this.extent(c, r.width, r.height, p.scale);
    const x = Math.min(Math.max(ev.clientX - r.left + d.dx, w / 2), r.width - w / 2), y = Math.min(Math.max(ev.clientY - r.top + d.dy, h / 2), r.height - h / 2);
    this.prefs.layout[c.id] = { ...(this.prefs.layout[c.id] || {}), x: x / r.width, y: y / r.height };
    this.layout();
  }
  editUp(ev) {
    if (this.editing.drag && this.editing.drag.id === ev.pointerId) { this.editing.drag = null; this.save(); }
  }
  scaleSelected(delta) {
    const id = this.editing && this.editing.selected;
    if (!id) { this.selName.textContent = 'Select a control first.'; return; }
    const c = TOUCH_CONTROLS.find((k) => k.id === id), p = this.place(c);
    const scale = Math.round(Math.min(Math.max(p.scale + delta, TOUCH_SCALE.min), TOUCH_SCALE.max) * 10) / 10;
    this.prefs.layout[id] = { ...(this.prefs.layout[id] || {}), x: p.x, y: p.y, scale };
    this.save();
    this.layout();
  }

  // ---- the settings panel's Touch tab ------------------------------------------------------------------------------
  buildSettings() {
    const box = el('div', { class: 'touch-settings' });
    const id = (k) => `touch-${k}`;
    const mode = el('select', { id: id('mode') });
    for (const [v, text] of TOUCH_MODES) mode.append(el('option', { value: v }, text));
    const range = (k, lim) => el('input', { id: id(k), type: 'range', min: String(lim.min), max: String(lim.max), step: String(lim.step) });
    const size = range('size', TOUCH_SIZE), opacity = range('opacity', TOUCH_OPACITY);
    const sizeOut = el('output', { for: id('size') }), opacityOut = el('output', { for: id('opacity') });
    const hand = el('select', { id: id('hand') }, el('option', { value: 'right' }, 'Right-handed (movement on the left)'), el('option', { value: 'left' }, 'Left-handed (movement on the right)'));
    const edit = el('button', { type: 'button' }, 'Edit layout…'), reset = el('button', { type: 'button', class: 'secondary' }, 'Reset layout');
    const status = el('p', { class: 'help' });
    mode.addEventListener('change', () => this.set({ mode: mode.value }));
    size.addEventListener('input', () => { sizeOut.textContent = `${size.value}%`; });
    size.addEventListener('change', () => this.set({ size: Number(size.value) }));
    opacity.addEventListener('input', () => { opacityOut.textContent = `${opacity.value}%`; this.set({ opacity: Number(opacity.value) }); });
    hand.addEventListener('change', () => this.set({ hand: hand.value }));
    edit.addEventListener('click', () => { this.settings.close(); this.startEdit(); });
    reset.addEventListener('click', () => this.resetLayout());
    const row = (label, input, out, help) => el('div', { class: 'row' }, el('label', { for: input.id }, label), out || input, ...(out ? [input] : []), ...(help ? [el('p', { class: 'help' }, help)] : []));
    box.append(
      row('Show touch controls', mode, null, 'On-screen controls over the game, played as player 1. They press the same buttons as the keyboard and controllers do.'),
      row('Size', size, sizeOut), row('Opacity', opacity, opacityOut), row('Layout', hand),
      el('div', { class: 'row inline' }, edit, reset),
      status);
    this.settings.refreshers.push(() => {
      mode.value = this.prefs.mode; size.value = String(this.prefs.size); opacity.value = String(this.prefs.opacity); hand.value = this.prefs.hand;
      sizeOut.textContent = `${this.prefs.size}%`; opacityOut.textContent = `${this.prefs.opacity}%`;
      status.textContent = `This device: ${this.coarse.matches ? 'a touch screen' : 'no touch screen found'}. Touch controls ${this.shown() ? 'shown' : 'hidden'}.` +
        (Object.keys(this.prefs.layout).length ? ' Your own layout is in use.' : '');
    });
    return box;
  }
}
