// SPDX-License-Identifier: GPL-2.0-or-later
import { gunzipSync, Unzip, UnzipInflate, UnzipPassThrough } from 'fflate';
import { PROFILE, requireCondition as check, sha256 } from './profile.js';

const text = new TextDecoder('utf-8', { fatal: true });
const align = n => (n + 3) & ~3;
export function parseBoot(bytes) {
  check(bytes instanceof Uint8Array && bytes.length >= 2048, 'Truncated boot image');
  check(bytes.length <= PROFILE.bootSectors * PROFILE.sectorSize, 'Boot image exceeds partition');
  check(text.decode(bytes.subarray(0, 8)) === 'ANDROID!', 'Not an Android boot image');
  const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const page = v.getUint32(36, true), kernelSize = v.getUint32(8, true);
  const ramdiskSize = v.getUint32(16, true);
  check(page === 2048 && kernelSize > 0 && ramdiskSize > 0, 'Unsupported boot header/layout');
  check(v.getUint32(24, true) === 0 && v.getUint32(40, true) === 0 && v.getUint32(44, true) === PROFILE.bootOsVersion,
    'Only the tested M3X v0 boot layout is supported');
  const ramdiskOffset = page + Math.ceil(kernelSize / page) * page;
  check(ramdiskOffset + ramdiskSize <= bytes.length, 'Truncated kernel/ramdisk');
  return { page, kernelSize, ramdiskSize,
    kernel: bytes.subarray(page, page + kernelSize),
    ramdisk: bytes.subarray(ramdiskOffset, ramdiskOffset + ramdiskSize) };
}
export async function validateStock(bytes) {
  check(bytes.length === PROFILE.stockSize && await sha256(bytes) === PROFILE.stockSha256,
    'Requires the exact stock M3X 1.75 boot.img; do not use another firmware or a pre-patched image');
  return parseBoot(bytes);
}

export async function stockBootFile(file) {
  const signature = new Uint8Array(await file.slice(0, 4).arrayBuffer());
  if (!equal(signature, new Uint8Array([0x50, 0x4b, 0x03, 0x04]))) {
    check(file.size === PROFILE.stockSize, 'Requires the exact stock M3X 1.75 boot.img or official firmware ZIP');
    const bytes = new Uint8Array(await file.arrayBuffer());
    await validateStock(bytes);
    return bytes;
  }
  let bytes, length = 0, complete = false;
  const unzip = new Unzip(entry => {
    if (entry.name !== 'boot.img') {
      // fflate retains unstarted entries; discard compressed bytes without inflating them.
      unzip.register(class extends UnzipPassThrough { static compression = entry.compression; });
      entry.ondata = () => {};
      entry.start();
      return;
    }
    check(!bytes, 'Duplicate root boot.img in firmware ZIP');
    check(entry.originalSize === undefined || entry.originalSize === PROFILE.stockSize,
      'Unexpected stock boot.img size in firmware ZIP');
    check(entry.compression === 0 || entry.compression === 8, 'Unsupported stock boot.img ZIP compression');
    unzip.register(UnzipPassThrough);
    unzip.register(UnzipInflate);
    bytes = new Uint8Array(PROFILE.stockSize);
    entry.ondata = (error, chunk, final) => {
      if (error) throw error;
      check(length + chunk.length <= bytes.length, 'Stock boot.img in firmware ZIP exceeds pinned size');
      bytes.set(chunk, length);
      length += chunk.length;
      if (final) {
        check(length === bytes.length, 'Truncated stock boot.img in firmware ZIP');
        complete = true;
      }
    };
    entry.start();
  });
  const reader = file.stream().getReader();
  try {
    while (!complete) {
      const { value, done } = await reader.read();
      if (done) {
        unzip.push(new Uint8Array(), true);
        check(complete, 'Firmware ZIP is missing root boot.img');
        break;
      }
      // Bound each inflate call even when a stream supplies large chunks.
      for (let offset = 0; offset < value.length && !complete; offset += 16384)
        unzip.push(value.subarray(offset, offset + 16384));
    }
    await validateStock(bytes);
    return bytes;
  } finally {
    try { await reader.cancel(); } finally { reader.releaseLock(); }
  }
}

function cstring(bytes, start, end) {
  const stop = bytes.indexOf(0, start);
  check(stop >= start && stop < end, 'Unterminated FDT string');
  return text.decode(bytes.subarray(start, stop));
}
export function fdtProperties(bytes) {
  check(bytes.length >= 40, 'Truncated FDT');
  const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  check(v.getUint32(0) === 0xd00dfeed, 'Bad FDT magic');
  const size = v.getUint32(4), structure = v.getUint32(8), strings = v.getUint32(12);
  const stringsSize = v.getUint32(32), structureSize = v.getUint32(36);
  check(size <= bytes.length && structure >= 40 && strings >= 40 &&
    structure + structureSize <= size && strings + stringsSize <= size,
    'FDT block outside image');
  const end = structure + structureSize, stack = [], props = new Map();
  let p = structure, ended = false;
  while (p + 4 <= end) {
    const token = v.getUint32(p); p += 4;
    if (token === 1) {
      const name = cstring(bytes, p, end); stack.push(name); p = align(p + name.length + 1);
    } else if (token === 2) {
      check(stack.length > 0, 'Unbalanced FDT nodes'); stack.pop();
    } else if (token === 3) {
      check(stack.length > 0 && p + 8 <= end, 'Invalid FDT property');
      const lengthOffset = p, length = v.getUint32(p), nameOffset = v.getUint32(p + 4); p += 8;
      check(nameOffset < stringsSize && p + length <= end, 'FDT property outside block');
      const name = cstring(bytes, strings + nameOffset, strings + stringsSize);
      const key = `${stack.join('/')}:${name}`;
      check(!props.has(key), 'Duplicate FDT property');
      props.set(key, { value: bytes.subarray(p, p + length), offset: p, lengthOffset,
        paddedLength: align(length) });
      p += align(length);
    } else if (token === 9) {
      check(stack.length === 0, 'Unclosed FDT nodes'); ended = true; break;
    } else check(token === 4, 'Unknown FDT token');
  }
  check(ended, 'Missing FDT end');
  return props;
}
function dtbBank(kernel) {
  const v = new DataView(kernel.buffer, kernel.byteOffset, kernel.byteLength), trees = [];
  for (let p = 0; p + 40 <= kernel.length; p++) {
    if (kernel[p] !== 0xd0 || v.getUint32(p) !== 0xd00dfeed) continue;
    const size = v.getUint32(p + 4);
    if (size <= 4096 || size >= 0x400000 || p + size > kernel.length) continue;
    const bytes = kernel.subarray(p, p + size);
    let props;
    try { props = fdtProperties(bytes); } catch { continue; }
    trees.push({ offset: p, bytes, props }); p += size - 1;
  }
  check(trees.length === 68, 'Unexpected device-tree bank');
  return trees;
}

// Remove strings in place, filling freed structure words with FDT_NOP. Keep all
// DTB/header/kernel-bank offsets and every other property byte-for-byte intact.
export function patchPins(dtb) {
  const out = dtb.slice(), props = fdtProperties(out), v = new DataView(out.buffer);
  let changed = 0;
  for (const state of ['gpio_key_active', 'gpio_key_suspend']) {
    for (const group of ['mux', 'config']) {
      const matches = [...props].filter(([path]) =>
        path.endsWith(`/tlmm_gpio_key/${state}/${group}:pins`));
      check(matches.length === 1, 'Missing/ambiguous M3X pin group');
      const prop = matches[0][1], names = text.decode(prop.value).split('\0');
      check(names.pop() === '', 'Invalid pin string list');
      check(names.length === 9 && names.filter(n => n === 'gpio18').length === 1 &&
        names.filter(n => n === 'gpio19').length === 1 && names.includes('gpio13') && names.includes('gpio17'),
        'Unexpected original pin list');
      const value = new TextEncoder().encode(names.filter(n => !['gpio18', 'gpio19'].includes(n)).join('\0') + '\0');
      check(align(value.length) <= prop.paddedLength, 'Pin patch exceeds property');
      v.setUint32(prop.lengthOffset, value.length);
      out.fill(0, prop.offset, prop.offset + prop.paddedLength); out.set(value, prop.offset);
      for (let p = prop.offset + align(value.length); p < prop.offset + prop.paddedLength; p += 4)
        v.setUint32(p, 4);
      changed++;
    }
  }
  check(changed === 4, 'Expected four pin properties');
  const after = fdtProperties(out);
  check(after.size === props.size, 'FDT property inventory changed');
  for (const [path, prop] of props) {
    const replacement = after.get(path);
    check(replacement, 'FDT property lost');
    if (!/\/tlmm_gpio_key\/(gpio_key_active|gpio_key_suspend)\/(mux|config):pins$/.test(path)) {
      check(equal(prop.value, replacement.value), 'Unrelated device-tree property changed');
    }
  }
  return out;
}
export async function applyAudioFix(stock, magisk) {
  const base = await validateStock(stock), candidate = parseBoot(magisk);
  check(equal(base.kernel, candidate.kernel), 'Magisk changed the kernel; refusing to guess');
  for (let i = 0; i < base.page; i++) {
    if ((i >= 16 && i < 20) || (i >= 576 && i < 608)) continue;
    check(stock[i] === magisk[i], 'Magisk changed an unexpected header field');
  }
  check(!equal(base.ramdisk, candidate.ramdisk), 'Boot image was not patched by Magisk');
  checkMagiskRamdisk(candidate.ramdisk);
  const trees = dtbBank(candidate.kernel);
  const matches = trees.filter(t => {
    const model = t.props.get(':model');
    return model && text.decode(model.value).replace(/\0$/, '') ===
      'Qualcomm Technologies, Inc. MSM8937-PMI8950 MTP';
  });
  check(matches.length === 1 && matches[0] === trees[26], 'M3X DTB identity differs');
  const out = magisk.slice();
  out.set(patchPins(matches[0].bytes), candidate.page + matches[0].offset);
  check(equal(parseBoot(out).ramdisk, candidate.ramdisk), 'Audio fix changed Magisk ramdisk');
  return out;
}
export function equal(a, b) {
  return a.length === b.length && a.every((n, i) => n === b[i]);
}
function checkMagiskRamdisk(compressed) {
  check(compressed[0] === 0x1f && compressed[1] === 0x8b, 'Unsupported ramdisk compression');
  // Bound gzip output by ISIZE before allocation; trusted APK still validated.
  const v = new DataView(compressed.buffer, compressed.byteOffset, compressed.byteLength);
  check(compressed.length >= 18 && v.getUint32(compressed.length - 4, true) <= 32 * 1024 * 1024,
    'Ramdisk decompression limit exceeded');
  const bytes = gunzipSync(compressed), files = new Set(); let p = 0;
  while (p + 110 <= bytes.length) {
    check(text.decode(bytes.subarray(p, p + 6)) === '070701', 'Unsupported ramdisk CPIO');
    const field = offset => {
      const value = text.decode(bytes.subarray(p + offset, p + offset + 8));
      check(/^[0-9a-f]{8}$/i.test(value), 'Invalid CPIO field'); return parseInt(value, 16);
    };
    const size = field(54), nameSize = field(94);
    check(nameSize > 0 && p + 110 + nameSize <= bytes.length, 'Truncated CPIO name');
    const name = cstring(bytes, p + 110, p + 110 + nameSize); p = align(p + 110 + nameSize);
    check(p + size <= bytes.length && !files.has(name), 'Invalid CPIO entry'); files.add(name);
    p = align(p + size); if (name === 'TRAILER!!!') break;
  }
  for (const name of ['init', '.backup/.magisk', 'overlay.d/sbin/magisk.xz', 'TRAILER!!!'])
    check(files.has(name), 'Missing Magisk ramdisk entry: ' + name);
}
export function paddedBoot(bytes) {
  parseBoot(bytes);
  const out = new Uint8Array(PROFILE.bootSectors * PROFILE.sectorSize); out.set(bytes); return out;
}
export async function validateStockPartition(bytes, stock) {
  await validateStock(stock);
  check(bytes.length === PROFILE.bootSectors * PROFILE.sectorSize, 'Incomplete boot-partition backup');
  check(equal(bytes.subarray(0, stock.length), stock), 'Device boot is not exact stock 1.75; rooting refused');
  check(bytes.subarray(stock.length).every(b => b === 0), 'Unexpected nonzero boot-partition tail');
}
