// Stands in for navigator.bluetooth and a TapUI device, speaking
// docs/protocol.md the way the firmware does: notifications cut into 20-byte
// pieces, every set echoed back as state. Runs in the page (addInitScript).
(() => {
  const SERVICE = 'c7170001-8a2d-43c1-a783-922a8834a1fe';
  const RX = 'c7170002-8a2d-43c1-a783-922a8834a1fe';
  const TX = 'c7170003-8a2d-43c1-a783-922a8834a1fe';

  const ui = {
    title: 'Test device',
    controls: [
      { type: 'toggle', id: 'light', label: 'Light' },
      { type: 'select', id: 'color', label: 'Colour', options: ['red', 'green', 'blue'] },
      { type: 'slider', id: 'bright', label: 'Brightness', min: 0, max: 100, step: 5, unit: '%' },
      { type: 'button', id: 'blink', label: 'Blink' },
      { type: 'readout', id: 'temp', label: 'Temperature', unit: '°C' },
      { type: 'dial', id: 'future', label: 'From the future' },
    ],
  };
  const state = { light: false, color: 'red', bright: 40, temp: 21.5 };

  const fake = { requests: [], received: [], ui, state, writes: 0 };
  window.__fake = fake;

  const enc = new TextEncoder();
  const dec = new TextDecoder();
  let rxBuf = '';
  let device;

  const tx = new EventTarget();
  tx.uuid = TX;
  tx.startNotifications = async () => tx;

  function notify(obj) {
    const bytes = enc.encode(`${JSON.stringify(obj)}\n`);
    for (let i = 0; i < bytes.length; i += 20) {
      const piece = bytes.slice(i, i + 20);
      tx.value = new DataView(piece.buffer, piece.byteOffset, piece.byteLength);
      tx.dispatchEvent(new Event('characteristicvaluechanged'));
    }
  }
  fake.push = (id, v) => { state[id] = v; notify({ t: 'state', id, v }); };

  function handle(line) {
    fake.received.push(line);
    const [op, id, ...rest] = line.split(' ');
    const value = rest.join(' ');
    if (op === 'hello') {
      notify({ t: 'ui', ui });
      for (const [k, v] of Object.entries(state)) notify({ t: 'state', id: k, v });
    } else if (op === 'set' && id in state) {
      state[id] = id === 'light' ? value === '1' : id === 'bright' ? Number(value) : value;
      notify({ t: 'state', id, v: state[id] });
    } else if (op === 'press' && id === 'blink') {
      // nothing to echo
    } else {
      notify({ t: 'err', msg: `unknown control: ${id}` });
    }
  }

  const rx = {
    uuid: RX,
    async writeValueWithResponse(chunk) {
      if (chunk.byteLength > 20) throw new Error('write over 20 bytes');
      fake.writes += 1;
      await new Promise((r) => setTimeout(r, 5));
      rxBuf += dec.decode(chunk);
      let nl;
      while ((nl = rxBuf.indexOf('\n')) >= 0) {
        const line = rxBuf.slice(0, nl);
        rxBuf = rxBuf.slice(nl + 1);
        setTimeout(() => handle(line), 0);
      }
    },
  };

  const service = {
    uuid: SERVICE,
    async getCharacteristic(uuid) {
      if (uuid === RX) return rx;
      if (uuid === TX) return tx;
      throw new DOMException('no such characteristic', 'NotFoundError');
    },
  };

  if (new URLSearchParams(location.search).has('nobt')) return;

  Object.defineProperty(navigator, 'bluetooth', {
    value: {
      async requestDevice(options) {
        fake.requests.push(options);
        if (fake.cancel) throw new DOMException('User cancelled the requestDevice() chooser.', 'NotFoundError');
        device = new EventTarget();
        device.name = options.filters[0].name ?? 'TapUI-ANY';
        device.gatt = {
          connected: false,
          async connect() { this.connected = true; return this; },
          async getPrimaryService(uuid) {
            if (uuid !== SERVICE) throw new DOMException('no such service', 'NotFoundError');
            return service;
          },
        };
        return device;
      },
    },
  });

  fake.disconnect = () => {
    device.gatt.connected = false;
    device.dispatchEvent(new Event('gattserverdisconnected'));
  };
})();
