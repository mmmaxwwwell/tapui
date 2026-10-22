// The wire format of docs/protocol.md. No DOM or Bluetooth here, so tests
// run it under plain node.

export const SERVICE = 'c7170001-8a2d-43c1-a783-922a8834a1fe';
export const RX = 'c7170002-8a2d-43c1-a783-922a8834a1fe';
export const TX = 'c7170003-8a2d-43c1-a783-922a8834a1fe';

export const MAX_LINE = 127;
// The ATT payload every link supports before any MTU exchange.
export const WRITE_CHUNK = 20;

export function launchParams(search) {
  const q = new URLSearchParams(search);
  const name = q.get('n')?.trim();
  return { name: name || null };
}

export function requestOptions({ name }) {
  return name
    ? { filters: [{ name }], optionalServices: [SERVICE] }
    : { filters: [{ services: [SERVICE] }] };
}

// Notifications split lines anywhere, including inside a multi-byte
// character, so bytes are decoded in stream mode.
export class LineDecoder {
  #decoder = new TextDecoder();
  #pending = '';

  push(bytes) {
    this.#pending += this.#decoder.decode(bytes, { stream: true });
    const lines = this.#pending.split('\n');
    this.#pending = lines.pop();
    return lines.filter((l) => l.length > 0);
  }
}

export function parseMessage(line) {
  let msg;
  try {
    msg = JSON.parse(line);
  } catch {
    return { t: 'bad', line };
  }
  if (!msg || typeof msg !== 'object' || typeof msg.t !== 'string') return { t: 'bad', line };
  return msg;
}

export function command(op, id, value) {
  if (op === 'hello') return 'hello\n';
  if (/\s/.test(id)) throw new Error(`control id has whitespace: ${id}`);
  if (op === 'press') return `press ${id}\n`;
  const v = typeof value === 'boolean' ? (value ? '1' : '0') : String(value);
  if (v.includes('\n')) throw new Error('value has a newline');
  return `set ${id} ${v}\n`;
}

export function encodeLine(line) {
  const bytes = new TextEncoder().encode(line);
  if (bytes.length > MAX_LINE + 1) throw new Error(`command longer than ${MAX_LINE} bytes`);
  const chunks = [];
  for (let i = 0; i < bytes.length; i += WRITE_CHUNK) chunks.push(bytes.slice(i, i + WRITE_CHUNK));
  return chunks;
}

export function stepOf(control) {
  return control.step > 0 ? control.step : 1;
}
