// A simulated Gamepad API for the page's tests (port/tests/web/controllers.js): installed before the page's scripts, it
// replaces navigator.getGamepads and sends the browser's own connection events, so the engine (SDL's Emscripten
// joystick code, through Emscripten's gamepad sampling) and the page see these devices exactly as they would see real
// ones. Only the devices are simulated: nothing here is physical-hardware testing.
//
//   __pads.connect({ id, mapping = 'standard', buttons = 17, axes = 4 })  -> its index
//   __pads.button(index, button, pressed [, value])     __pads.axis(index, axis, value -1..1)
//   __pads.disconnect(index)
// Indices are reused the browser's way: the lowest free one.
(() => {
  const slots = [];
  const now = () => performance.now();
  function make(index, { id, mapping = 'standard', buttons = 17, axes = 4 }) {
    return {
      id, index, mapping, connected: true, timestamp: now(),
      buttons: Array.from({ length: buttons }, () => ({ pressed: false, touched: false, value: 0 })),
      axes: Array.from({ length: axes }, () => 0),
      vibrationActuator: null,
    };
  }
  function announce(type, pad) {
    const ev = new Event(type);
    Object.defineProperty(ev, 'gamepad', { value: pad });
    window.dispatchEvent(ev);
  }
  window.__pads = {
    connect(spec) {
      let index = slots.findIndex((p) => !p);
      if (index < 0) index = slots.length;
      slots[index] = make(index, spec);
      announce('gamepadconnected', slots[index]);
      return index;
    },
    disconnect(index) {
      const pad = slots[index];
      if (!pad) return;
      pad.connected = false;
      slots[index] = null;
      announce('gamepaddisconnected', pad);
    },
    button(index, b, pressed, value) {
      const pad = slots[index];
      pad.buttons[b] = { pressed, touched: pressed, value: value === undefined ? (pressed ? 1 : 0) : value };
      pad.timestamp = now();
    },
    axis(index, a, value) {
      const pad = slots[index];
      pad.axes[a] = value;
      pad.timestamp = now();
    },
  };
  Object.defineProperty(Navigator.prototype, 'getGamepads', {
    configurable: true,
    value: function getGamepads() { return slots.map((p) => p || null); },
  });
})();
