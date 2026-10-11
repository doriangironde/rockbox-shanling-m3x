import test from 'node:test';
import assert from 'node:assert/strict';
import { checkIdentity, confirmationFor } from '../src/profile.js';
import { installerBundle, safePath } from '../src/bundle.js';
import { crc32, parseGpt } from '../src/gpt.js';
import { equal, fdtProperties, parseBoot, patchPins, validateStock } from '../src/images.js';
import { BrowserAdb, EphemeralCredentials, quote } from '../src/adb.js';
import { context, fixGpt, gpt, syntheticFdt } from './helpers.js';

test('model, firmware, API, architecture, state, battery and serial fail closed', () => {
  assert.equal(checkIdentity(context()).serial, '00112233');
  for (const change of [{ manufacturer: 'Other' }, { model: 'M3X' }, { firmware: '1.74' }, { api: '26' },
    { arch: 'armv7l' }, { androidActive: false }, { rockboxRunning: true }, { battery: 49 },
    { battery: NaN }, { serial: 'unknown' }]) assert.throws(() => checkIdentity(context(change)));
});
test('confirmation binds exact action and image digest', () => {
  assert.equal(confirmationFor('a'.repeat(64)), 'FLASH M3X 1.75 aaaaaaaaaaaa');
  assert.equal(confirmationFor('a'.repeat(64), true), 'RESTORE M3X 1.75 aaaaaaaaaaaa');
  assert.throws(() => confirmationFor('evil')); assert.throws(() => confirmationFor(undefined));
});
test('bundle/upload paths reject traversal, empty components and controls', () => {
  for (const value of ['/etc/passwd', '../boot', 'a/../b', './a', 'a//b', 'a\\b', 'a\0b', 'a\nb', ''])
    assert.equal(safePath(value), false, value);
  assert.equal(safePath('payload/assets/.rockbox/fonts/a.fnt'), true);
});
test('untrusted installer and stock images rejected before parsing', async () => {
  await assert.rejects(installerBundle(new Uint8Array([1, 2])), /pinned/);
  await assert.rejects(validateStock(new Uint8Array(2048)), /exact stock/);
  assert.throws(() => parseBoot(new Uint8Array(8)), /Truncated/);
});
test('GPT CRCs and exact geometry protect boot address', () => {
  assert.equal(crc32(new TextEncoder().encode('123456789')), 0xcbf43926);
  const valid = gpt(); assert.equal(parseGpt(valid).boot.start, 790528);
  const brokenHeader = valid.slice(); brokenHeader[528] ^= 1; assert.throws(() => parseGpt(brokenHeader), /CRC/);
  const brokenEntries = valid.slice(); brokenEntries[1024] ^= 1; assert.throws(() => parseGpt(brokenEntries), /CRC/);
  const moved = valid.slice(); new DataView(moved.buffer).setBigUint64(1024 + 32, 790529n, true); fixGpt(moved);
  assert.throws(() => parseGpt(moved), /Boot partition/);
  const overlaps = valid.slice(); new DataView(overlaps.buffer).setBigUint64(1024 + 128 + 32, 790528n, true); fixGpt(overlaps);
  assert.throws(() => parseGpt(overlaps), /Overlapping/);
  assert.throws(() => parseGpt(valid.subarray(0, 1024)), /Incomplete/);
});
test('in-place FDT patch removes only four conflicting pin lists and preserves I2C', () => {
  const original = syntheticFdt(), before = fdtProperties(original), fixed = patchPins(original), after = fdtProperties(fixed);
  assert.equal(original.length, fixed.length); assert.equal(before.size, after.size); let changes = 0;
  for (const [path, property] of before) {
    const value = after.get(path).value;
    if (!equal(property.value, value)) {
      changes++; assert.match(path, /\/tlmm_gpio_key\/.+:pins$/);
      assert(!new TextDecoder().decode(value).includes('gpio18')); assert(!new TextDecoder().decode(value).includes('gpio19'));
      assert(new TextDecoder().decode(value).includes('gpio13')); assert(new TextDecoder().decode(value).includes('gpio17'));
    }
  }
  assert.equal(changes, 4); assert.throws(() => patchPins(fixed), /Unexpected original/);
  assert(equal(original, syntheticFdt()), 'input was mutated');
});
test('malformed FDT pointers and tokens refused', () => {
  const tree = syntheticFdt(); new DataView(tree.buffer).setUint32(12, tree.length + 8);
  assert.throws(() => fdtProperties(tree), /outside/);
  const bad = syntheticFdt(); new DataView(bad.buffer).setUint32(56, 55);
  assert.throws(() => fdtProperties(bad), /Unknown/);
});
test('ADB shell quoting and rollback names cannot inject commands', async () => {
  assert.equal(quote("x'; reboot; '"), "'x'\\''; reboot; '\\'''" );
  const adb = new BrowserAdb({}, {}, () => {});
  for (const name of ['../stock', 'x;reboot', '/tmp/a', 'x..y', '']) await assert.rejects(adb.rollback(name), /Invalid backup/);
  await assert.rejects(adb.pushFiles({ '../boot': new Uint8Array() }, '/data/local/tmp/m3x-web.A12345'), /Unsafe upload/);
  await assert.rejects(adb.enterEdl(false), /Explicit EDL/);
});
test('ADB keys stay ephemeral and are forgotten', async () => {
  const store = new EphemeralCredentials();
  assert.deepEqual(await Array.fromAsync(store.iterateKeys()), []);
  const key = await store.generateKey(); assert(key.buffer.length > 1000);
  assert.equal((await Array.fromAsync(store.iterateKeys())).length, 1);
  store.forget(); assert(key.buffer.every(b => b === 0)); assert.deepEqual(await Array.fromAsync(store.iterateKeys()), []);
});
