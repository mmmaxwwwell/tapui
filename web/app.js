import {
  LineDecoder, RX, SERVICE, TX, command, encodeLine, launchParams, parseMessage, requestOptions, stepOf,
} from './proto.js';

const $ = (sel) => document.querySelector(sel);
const params = launchParams(location.search);

const view = {
  title: $('#title'),
  status: $('#status'),
  connect: $('#connect'),
  controls: $('#controls'),
};

let device = null;
let rx = null;
let widgets = new Map();

// Writes go out one at a time. A slider dragged faster than the link keeps
// only its latest value queued, so the device ends on where the thumb stopped.
const queue = [];
let writing = false;

function send(op, id, value) {
  const line = command(op, id, value);
  const key = op === 'set' ? `set ${id}` : null;
  const queued = key && queue.find((q) => q.key === key);
  if (queued) queued.line = line;
  else queue.push({ key, line });
  pump();
}

async function pump() {
  if (writing) return;
  writing = true;
  try {
    while (queue.length && rx) {
      const { line } = queue.shift();
      for (const chunk of encodeLine(line)) await rx.writeValueWithResponse(chunk);
    }
  } catch (err) {
    setStatus(`Send failed: ${err.message}`, 'error');
  } finally {
    writing = false;
  }
}

function setStatus(text, kind = '') {
  view.status.textContent = text;
  view.status.dataset.kind = kind;
}

function field(control, input) {
  const row = document.createElement('label');
  row.className = `control control-${control.type}`;
  const name = document.createElement('span');
  name.className = 'label';
  name.textContent = control.label ?? control.id;
  row.append(name, input);
  return row;
}

const renderers = {
  button(c) {
    const b = document.createElement('button');
    b.type = 'button';
    b.className = 'control control-button';
    b.textContent = c.label ?? c.id;
    b.addEventListener('click', () => send('press', c.id));
    return { el: b, set() {} };
  },

  toggle(c) {
    const input = document.createElement('input');
    input.type = 'checkbox';
    input.setAttribute('role', 'switch');
    input.addEventListener('change', () => send('set', c.id, input.checked));
    return { el: field(c, input), set: (v) => { input.checked = v === true || v === 1 || v === '1'; } };
  },

  slider(c) {
    const wrap = document.createElement('span');
    wrap.className = 'slider';
    const input = document.createElement('input');
    input.type = 'range';
    input.min = c.min ?? 0;
    input.max = c.max ?? 100;
    input.step = stepOf(c);
    const out = document.createElement('output');
    const show = () => { out.textContent = `${input.value}${c.unit ? ` ${c.unit}` : ''}`; };
    let dragging = false;
    input.addEventListener('pointerdown', () => { dragging = true; });
    input.addEventListener('pointerup', () => { dragging = false; });
    input.addEventListener('input', () => { show(); send('set', c.id, input.value); });
    wrap.append(input, out);
    show();
    return {
      el: field(c, wrap),
      // The device echoes every set; don't yank the thumb back mid-drag.
      set: (v) => { if (!dragging) { input.value = v; show(); } },
    };
  },

  select(c) {
    const sel = document.createElement('select');
    for (const opt of c.options ?? []) sel.append(new Option(opt, opt));
    sel.addEventListener('change', () => send('set', c.id, sel.value));
    return { el: field(c, sel), set: (v) => { sel.value = v; } };
  },

  readout(c) {
    const out = document.createElement('output');
    out.textContent = '–';
    return {
      el: field(c, out),
      set: (v) => { out.textContent = `${v}${c.unit ? ` ${c.unit}` : ''}`; },
    };
  },
};

function unsupported(c) {
  const span = document.createElement('span');
  span.className = 'unsupported';
  span.textContent = `unsupported control (${c.type})`;
  return { el: field(c, span), set() {} };
}

function renderUi(ui) {
  view.title.textContent = ui.title ?? device?.name ?? 'TapUI';
  document.title = view.title.textContent;
  widgets = new Map();
  view.controls.replaceChildren();
  for (const c of ui.controls ?? []) {
    const w = (renderers[c.type] ?? unsupported)(c);
    w.el.dataset.id = c.id;
    widgets.set(c.id, w);
    view.controls.append(w.el);
  }
}

function onMessage(msg) {
  if (msg.t === 'ui') renderUi(msg.ui ?? {});
  else if (msg.t === 'state') widgets.get(msg.id)?.set(msg.v);
  else if (msg.t === 'err') setStatus(msg.msg ?? 'Device reported an error', 'error');
  else if (msg.t === 'bad') console.warn('unparsed line from device:', msg.line);
}

async function connect() {
  if (!navigator.bluetooth) {
    setStatus(window.isSecureContext
      ? 'This browser has no Web Bluetooth. On Android use Chrome; on iPhone use the Bluefy browser.'
      : 'Bluetooth needs this page over HTTPS.', 'error');
    return;
  }
  view.connect.disabled = true;
  try {
    if (!device) {
      device = await navigator.bluetooth.requestDevice(requestOptions(params));
      device.addEventListener('gattserverdisconnected', onDisconnect);
    }
    setStatus(`Connecting to ${device.name ?? 'device'}…`);
    const server = await device.gatt.connect();
    const service = await server.getPrimaryService(SERVICE);
    rx = await service.getCharacteristic(RX);
    const tx = await service.getCharacteristic(TX);
    const decoder = new LineDecoder();
    tx.addEventListener('characteristicvaluechanged', (e) => {
      const v = e.target.value;
      for (const line of decoder.push(new Uint8Array(v.buffer, v.byteOffset, v.byteLength))) {
        onMessage(parseMessage(line));
      }
    });
    await tx.startNotifications();
    setStatus(`Connected to ${device.name ?? 'device'}`, 'ok');
    view.connect.hidden = true;
    send('hello');
  } catch (err) {
    // Cancelling the chooser lands here too; it's not an error worth red text.
    setStatus(err.name === 'NotFoundError' ? 'No device chosen.' : `Couldn't connect: ${err.message}`,
      err.name === 'NotFoundError' ? '' : 'error');
    if (err.name === 'NotFoundError') device = null;
  } finally {
    view.connect.disabled = false;
  }
}

function onDisconnect() {
  rx = null;
  queue.length = 0;
  view.controls.querySelectorAll('input, select, button').forEach((el) => { el.disabled = true; });
  view.connect.textContent = 'Reconnect';
  view.connect.hidden = false;
  setStatus('Disconnected.', 'error');
}

view.connect.addEventListener('click', connect);
if (params.name) view.title.textContent = params.name;
setStatus(params.name ? `Tap Connect to reach ${params.name}.` : 'Tap Connect to pick a device.');

if ('serviceWorker' in navigator) {
  navigator.serviceWorker.register('sw.js').catch((err) => console.warn('offline copy unavailable:', err));
}
