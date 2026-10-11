import test from 'node:test';
import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { applyAudioFix, equal, fdtProperties, parseBoot, validateStock, validateStockPartition } from '../src/images.js';
import { installerBundle } from '../src/bundle.js';
import { PROFILE, sha256 } from '../src/profile.js';
const base = fileURLToPath(new URL('../../', import.meta.url));
const stockPath = base + 'local/firmware/m3x_v1_75/boot.img';
const magiskPath = base + 'local/backups/magisk_patched-30700_25tcf.img';
const referencePath = base + 'local/firmware/m3x_v1_75/boot-magisk-dtb.img';
const bundlePath = base + 'build-m3x/release/Rockbox-M3X-dev-bf6af2444b56-installer.zip';
const read = path => new Uint8Array(readFileSync(path));

test('private stock/Magisk fixtures: same GPIO property fix as hardware-tested dtc image',
  { skip: ![stockPath, magiskPath, referencePath].every(existsSync) && 'Private boot images are not distributed' }, async () => {
    const stock = read(stockPath), magisk = read(magiskPath), reference = parseBoot(read(referencePath));
    await validateStock(stock); const fixedBytes = await applyAudioFix(stock, magisk), fixed = parseBoot(fixedBytes);
    assert.equal(fixedBytes.length, magisk.length); assert(equal(fixed.ramdisk, parseBoot(magisk).ramdisk));
    assert(equal(fixedBytes.subarray(0, 2048), magisk.subarray(0, 2048)));
    // Parse the target via the known stock bank offset, independently discovered.
    function target(kernel) {
      const v = new DataView(kernel.buffer, kernel.byteOffset, kernel.byteLength);
      for (let p = 0; p + 40 <= kernel.length; p++) {
        if (kernel[p] !== 0xd0 || v.getUint32(p) !== 0xd00dfeed) continue;
        const n = v.getUint32(p + 4); if (n < 4096 || p + n > kernel.length) continue;
        try {
          const props = fdtProperties(kernel.subarray(p, p + n));
          if (new TextDecoder().decode(props.get(':model')?.value).includes('MSM8937-PMI8950 MTP')) return { p, n, props };
        } catch {}
      }
      throw new Error('Missing M3X tree');
    }
    const original = target(parseBoot(stock).kernel), browser = target(fixed.kernel), tested = target(reference.kernel);
    assert.equal(original.p, browser.p); assert.equal(original.n, browser.n);
    assert(equal(fixed.kernel.subarray(0, browser.p), parseBoot(magisk).kernel.subarray(0, browser.p)));
    assert(equal(fixed.kernel.subarray(browser.p + browser.n), parseBoot(magisk).kernel.subarray(browser.p + browser.n)));
    let changed = 0;
    for (const [name, prop] of original.props) {
      const value = browser.props.get(name).value;
      if (!equal(prop.value, value)) { changed++; assert.match(name, /\/tlmm_gpio_key\/.+:pins$/); }
      assert(equal(value, tested.props.get(name).value), 'Differs semantically from tested DTB: ' + name);
    }
    assert.equal(changed, 4);
    const mutated = magisk.slice(); mutated[2048] ^= 1;
    await assert.rejects(applyAudioFix(stock, mutated), /changed the kernel/);
    const padded = new Uint8Array(PROFILE.bootSectors * 512); padded.set(stock);
    await validateStockPartition(padded, stock); padded[padded.length - 1] = 1;
    await assert.rejects(validateStockPartition(padded, stock), /nonzero/);
  });
test('private pinned release fixture verifies all files; corrupted package rejected',
  { skip: !existsSync(bundlePath) && 'Published installer ZIP not available locally' }, async () => {
    const bytes = read(bundlePath), bundle = await installerBundle(bytes);
    assert.equal(bundle.manifest.release, 'dev-bf6af2444b56'); assert(Object.keys(bundle.files).length > 100);
    assert.equal(await sha256(bundle.files['payload/rockbox']), 'd4c54f16521e13f3d321f92e0fc36cb3d252a0dd637fe0a76d421d2911a88608');
    bytes[500] ^= 1; await assert.rejects(installerBundle(bytes), /pinned/);
  });
