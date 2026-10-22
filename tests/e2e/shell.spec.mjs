import { test, expect } from '@playwright/test';
import { fileURLToPath } from 'node:url';

const fakeBluetooth = fileURLToPath(new URL('./fake-bluetooth.js', import.meta.url));

test.beforeEach(async ({ page }) => {
  await page.addInitScript({ path: fakeBluetooth });
});

const control = (page, id) => page.locator(`[data-id="${id}"]`);

async function connect(page, query = '?n=TapUI-TEST') {
  await page.goto(query);
  await page.getByRole('button', { name: 'Connect' }).click();
  await expect(page.getByRole('heading')).toHaveText('Test device');
}

test('the link names the device the chooser looks for', async ({ page }) => {
  await page.goto('?n=TapUI-TEST');
  await expect(page.getByRole('heading')).toHaveText('TapUI-TEST');
  await expect(page.locator('#status')).toHaveText('Tap Connect to reach TapUI-TEST.');
  await page.getByRole('button', { name: 'Connect' }).click();
  const requests = await page.evaluate(() => window.__fake.requests);
  expect(requests[0].filters).toEqual([{ name: 'TapUI-TEST' }]);
});

test('draws the controls the device describes, with its values', async ({ page }) => {
  await connect(page);
  await expect(page.locator('#status')).toHaveText('Connected to TapUI-TEST');
  await expect(page.getByRole('switch', { name: 'Light' })).not.toBeChecked();
  await expect(page.getByRole('combobox', { name: 'Colour' })).toHaveValue('red');
  await expect(control(page, 'bright').locator('output')).toHaveText('40 %');
  await expect(page.getByRole('button', { name: 'Blink' })).toBeVisible();
  await expect(control(page, 'temp').locator('output')).toHaveText('21.5 °C');
  await expect(control(page, 'future')).toContainText('unsupported control (dial)');
});

test('controls send commands and show what the device echoes', async ({ page }) => {
  await connect(page);
  await page.getByRole('switch', { name: 'Light' }).check();
  await page.getByRole('combobox', { name: 'Colour' }).selectOption('blue');
  await page.getByRole('button', { name: 'Blink' }).click();
  await expect.poll(() => page.evaluate(() => window.__fake.received))
    .toEqual(['hello', 'set light 1', 'set color blue', 'press blink']);
  await expect.poll(() => page.evaluate(() => window.__fake.state.light)).toBe(true);
});

test('a dragged slider sends its last value, not every step', async ({ page }) => {
  await connect(page);
  // One burst of input events, faster than the link drains writes.
  await page.getByRole('slider', { name: 'Brightness' }).evaluate((el) => {
    for (const v of ['45', '50', '55', '60', '65', '70']) {
      el.value = v;
      el.dispatchEvent(new Event('input', { bubbles: true }));
    }
  });
  await expect.poll(() => page.evaluate(() => window.__fake.state.bright)).toBe(70);
  const sets = await page.evaluate(() => window.__fake.received.filter((l) => l.startsWith('set bright')));
  expect(sets.length).toBeLessThan(6);
  await expect(control(page, 'bright').locator('output')).toHaveText('70 %');
});

test('device-pushed values update the page', async ({ page }) => {
  await connect(page);
  await page.evaluate(() => window.__fake.push('temp', 30.25));
  await expect(control(page, 'temp').locator('output')).toHaveText('30.25 °C');
});

test('a device error shows in the status line', async ({ page }) => {
  await connect(page);
  await page.evaluate(() => window.__fake.ui.controls.push({ type: 'button', id: 'ghost', label: 'Ghost' }));
  await page.evaluate(() => window.__fake.disconnect());
  await page.getByRole('button', { name: 'Reconnect' }).click();
  await page.getByRole('button', { name: 'Ghost' }).click();
  await expect(page.locator('#status')).toHaveText('unknown control: ghost');
});

test('losing the link disables the controls and offers Reconnect', async ({ page }) => {
  await connect(page);
  await page.evaluate(() => window.__fake.disconnect());
  await expect(page.locator('#status')).toHaveText('Disconnected.');
  await expect(page.getByRole('switch', { name: 'Light' })).toBeDisabled();
  await page.getByRole('button', { name: 'Reconnect' }).click();
  await expect(page.getByRole('switch', { name: 'Light' })).toBeEnabled();
  expect(await page.evaluate(() => window.__fake.requests.length)).toBe(1);
});

test('cancelling the chooser is not an error', async ({ page }) => {
  await page.goto('?n=TapUI-TEST');
  await page.evaluate(() => { window.__fake.cancel = true; });
  await page.getByRole('button', { name: 'Connect' }).click();
  await expect(page.locator('#status')).toHaveText('No device chosen.');
  await expect(page.getByRole('button', { name: 'Connect' })).toBeEnabled();
});

test('a browser without Web Bluetooth says what to use instead', async ({ page }) => {
  await page.goto('?n=TapUI-TEST&nobt');
  await page.getByRole('button', { name: 'Connect' }).click();
  await expect(page.locator('#status')).toContainText('Bluefy');
});

test('after one visit the page opens with no network, for any device link', async ({ page, context }) => {
  await page.goto('?n=TapUI-TEST');
  await page.evaluate(() => navigator.serviceWorker.ready);
  await context.setOffline(true);
  await page.goto('?n=TapUI-OTHER');
  await expect(page.getByRole('heading')).toHaveText('TapUI-OTHER');
  await page.getByRole('button', { name: 'Connect' }).click();
  await expect(page.locator('#status')).toHaveText('Connected to TapUI-OTHER');
  await expect(page.getByRole('heading')).toHaveText('Test device');
});
