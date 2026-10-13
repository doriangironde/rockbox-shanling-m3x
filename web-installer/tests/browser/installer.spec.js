import { test, expect } from '@playwright/test';

async function mockAdb(page, rooted = true) {
  await page.evaluate(async rooted => {
    const { BrowserAdb } = await import('/src/adb.js');
    window.__testCalls = [];
    BrowserAdb.request = async () => ({
      adb: { disconnected: new Promise(() => {}) }, credentials: { forget() {} },
      async preflight() { return { model: 'Shanling M3X', firmware: '1.75', battery: 80, rooted }; },
      async runInstaller(bundle, action) { window.__testCalls.push(action); },
      async close() { window.__testCalls.push('close'); },
    });
  }, rooted);
  await page.getByRole('button', { name: 'Connect M3X', exact: true }).click();
}
test('initial UI is fail-closed; no auto connect, flash, remote requests or persistent key', async ({ page }) => {
  const exceptions = [], requests = [];
  page.on('pageerror', error => exceptions.push(error.message)); page.on('request', req => requests.push(req.url()));
  await page.goto('/');
  for (const id of ['install', 'prepare-root', 'enter-edl', 'connect-edl', 'flash', 'restore-boot'])
    await expect(page.locator('#' + id)).toBeDisabled();
  expect(exceptions).toEqual([]); expect(requests.every(url => url.startsWith('http://127.0.0.1:5173/'))).toBeTruthy();
  expect(await page.evaluate(() => localStorage.length)).toBe(0);
});
test('unsupported browser offers CLI fallback and cannot connect', async ({ page }) => {
  await page.addInitScript(() => Object.defineProperty(navigator, 'usb', { value: undefined }));
  await page.goto('/');
  await expect(page.locator('#connect')).toBeDisabled();
});
test('rooted player cannot enter rooting branch; install needs explicit consent', async ({ page }) => {
  await page.goto('/'); await mockAdb(page);
  await expect(page.locator('#prepare-root')).toBeDisabled(); await expect(page.locator('#install')).toBeDisabled();
  await page.locator('#installer-file').setInputFiles({ name: 'installer.zip', mimeType: 'application/zip', buffer: Buffer.from('fake') });
  await expect(page.locator('#check-install')).toBeEnabled(); await expect(page.locator('#install')).toBeDisabled();
  await page.locator('#install-consent').check(); await expect(page.locator('#install')).toBeEnabled();
  await page.locator('#install').click(); await expect(page.locator('#install')).toBeEnabled();
  expect(await page.evaluate(() => window.__testCalls)).toEqual([]);
});
test('unrooted player cannot install or reboot/flash before preparation', async ({ page }) => {
  await page.goto('/'); await mockAdb(page, false);
  await page.locator('#edl-consent').check(); await page.locator('#flash-risk').check();
  await page.locator('#flash-confirmation').fill('FLASH M3X 1.75 aaaaaaaaaaaa');
  for (const id of ['install', 'enter-edl', 'connect-edl', 'flash']) await expect(page.locator('#' + id)).toBeDisabled();
});
test('read-only diagnostics require consent and authenticate programmer before any reboot', async ({ page }) => {
  await page.goto('/'); await mockAdb(page);
  await page.locator('#programmer-file').setInputFiles({ name: 'MSM8937.mbn', mimeType: 'application/octet-stream', buffer: Buffer.from('fake') });
  await expect(page.locator('#diagnostic-reboot')).toBeDisabled();
  await page.locator('#edl-consent').check(); await expect(page.locator('#diagnostic-reboot')).toBeEnabled();
  await page.locator('#diagnostic-reboot').click();
  await expect(page.locator('#diagnostic-reboot')).toBeEnabled();
  expect(await page.evaluate(() => window.__testCalls)).toEqual([]);
  await expect(page.locator('#flash')).toBeDisabled();
});
