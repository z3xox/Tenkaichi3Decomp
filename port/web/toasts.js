// Short notifications over the game area (port/web/index.html #toasts, inside the stage so they show in full screen
// too): controllers connecting and disconnecting. They never take the focus or the game's input: the strip lets clicks
// through, only a toast's own close button takes one. Each disappears by itself after a few seconds.
'use strict';

const TOAST_MS = 4500;     // how long a toast stays
const TOAST_MAX = 3;       // at most this many at once (the oldest goes first)
const TOAST_EDGE_PX = 12;  // from the top of the game's picture

class Bt3Toasts {
  constructor(root) {
    this.root = root;  // the strip: a polite live region (a warning is an alert inside it)
  }

  // kind: 'ok' or 'warn'; title in bold, then the detail
  show(kind, title, detail) {
    const t = document.createElement('div');
    t.className = `toast toast-${kind}`;
    if (kind === 'warn') t.setAttribute('role', 'alert');
    const text = document.createElement('span');
    const b = document.createElement('strong');
    b.textContent = title;
    text.append(b, detail ? ` — ${detail}` : '');
    const close = document.createElement('button');
    close.type = 'button';
    close.className = 'toast-close';
    close.setAttribute('aria-label', 'Dismiss');
    close.tabIndex = -1;  // (never in the way of the keyboard: the mouse or a finger closes it)
    close.textContent = '×';
    close.addEventListener('pointerdown', (e) => e.preventDefault());  // (a click does not take the focus from the game)
    close.addEventListener('click', () => this.remove(t));
    t.append(text, close);
    // at the top of the game's picture (the strip is fixed, to stay above the settings overlay: it follows the stage)
    const stage = this.root.parentElement.getBoundingClientRect();
    this.root.style.top = `${Math.max(TOAST_EDGE_PX, stage.top + TOAST_EDGE_PX)}px`;
    this.root.style.left = `${stage.left + stage.width / 2}px`;
    this.root.appendChild(t);
    while (this.root.children.length > TOAST_MAX) this.root.firstElementChild.remove();
    t.timer = setTimeout(() => this.remove(t), TOAST_MS);
    return t;
  }

  remove(t) {
    clearTimeout(t.timer);
    if (!t.isConnected) return;
    t.classList.add('leaving');
    setTimeout(() => t.remove(), 200);
  }
}

// A controller's name as people know it, from the browser's id: Chrome "Xbox Wireless Controller (STANDARD GAMEPAD
// Vendor: 045e Product: 0b13)", Firefox "045e-0b13-Xbox Wireless Controller".
function gamepadDisplayName(id) {
  const name = String(id).replace(/\s*\((?:STANDARD GAMEPAD\s*)?Vendor:[^)]*\)\s*$/i, '').replace(/^[0-9a-f]{1,4}-[0-9a-f]{1,4}-/i, '').trim();
  return name || 'Controller';
}
