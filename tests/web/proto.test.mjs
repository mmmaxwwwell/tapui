import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  LineDecoder, MAX_LINE, SERVICE, WRITE_CHUNK, command, encodeLine, launchParams, parseMessage, requestOptions,
} from '../../web/proto.js';

test('launch link names the device', () => {
  assert.deepEqual(launchParams('?n=TapUI-1A2B'), { name: 'TapUI-1A2B' });
  assert.deepEqual(launchParams(''), { name: null });
  assert.deepEqual(launchParams('?n=%20'), { name: null });
});

test('chooser filters by name when the link has one, else by service', () => {
  assert.deepEqual(requestOptions({ name: 'TapUI-1A2B' }), { filters: [{ name: 'TapUI-1A2B' }], optionalServices: [SERVICE] });
  assert.deepEqual(requestOptions({ name: null }), { filters: [{ services: [SERVICE] }] });
});

test('lines reassemble across notifications, multi-byte characters included', () => {
  const d = new LineDecoder();
  const bytes = new TextEncoder().encode('{"t":"state","id":"temp","v":"21 °C"}\n{"t":"ui"');
  const cut = bytes.indexOf(0xc2) + 1; // inside the two-byte '°'
  assert.deepEqual(d.push(bytes.slice(0, cut)), []);
  assert.deepEqual(d.push(bytes.slice(cut)), ['{"t":"state","id":"temp","v":"21 °C"}']);
  assert.deepEqual(d.push(new TextEncoder().encode(',"ui":{}}\n\n')), ['{"t":"ui","ui":{}}']);
});

test('messages', () => {
  assert.deepEqual(parseMessage('{"t":"state","id":"x","v":3}'), { t: 'state', id: 'x', v: 3 });
  assert.equal(parseMessage('nope').t, 'bad');
  assert.equal(parseMessage('{"v":1}').t, 'bad');
});

test('commands', () => {
  assert.equal(command('hello'), 'hello\n');
  assert.equal(command('press', 'blink'), 'press blink\n');
  assert.equal(command('set', 'light', true), 'set light 1\n');
  assert.equal(command('set', 'light', false), 'set light 0\n');
  assert.equal(command('set', 'color', 'warm white'), 'set color warm white\n');
  assert.throws(() => command('set', 'a b', 1));
  assert.throws(() => command('set', 'a', 'x\ny'));
});

test('commands go out in 20-byte writes, capped at the device line limit', () => {
  const chunks = encodeLine(command('set', 'color', 'x'.repeat(40)));
  assert.ok(chunks.every((c) => c.length <= WRITE_CHUNK));
  assert.equal(new TextDecoder().decode(Buffer.concat(chunks)), `set color ${'x'.repeat(40)}\n`);
  assert.throws(() => encodeLine(`${'x'.repeat(MAX_LINE + 1)}\n`));
});
