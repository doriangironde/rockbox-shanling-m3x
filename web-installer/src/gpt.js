// SPDX-License-Identifier: GPL-2.0-or-later
import { PROFILE, requireCondition as check } from './profile.js';
export function crc32(bytes) {
  let crc = 0xffffffff;
  for (const b of bytes) {
    crc ^= b;
    for (let i = 0; i < 8; i++) crc = (crc >>> 1) ^ (0xedb88320 & -(crc & 1));
  }
  return (crc ^ 0xffffffff) >>> 0;
}
export function parseGpt(bytes) {
  check(bytes.length >= 34 * 512, 'Incomplete primary GPT');
  const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength), h = 512;
  check(new TextDecoder().decode(bytes.subarray(h, h + 8)) === 'EFI PART', 'Missing GPT header');
  check(v.getUint32(h + 8, true) === 0x10000, 'Unsupported GPT revision');
  const headerSize = v.getUint32(h + 12, true);
  check(headerSize >= 92 && headerSize <= 512, 'Invalid GPT header size');
  const header = bytes.slice(h, h + headerSize); header.fill(0, 16, 20);
  check(crc32(header) === v.getUint32(h + 16, true), 'GPT header CRC mismatch');
  const integer = offset => {
    const value = v.getBigUint64(offset, true);
    check(value <= BigInt(Number.MAX_SAFE_INTEGER), 'GPT LBA outside safe range'); return Number(value);
  };
  const backupLba = integer(h + 32), firstUsable = integer(h + 40), lastUsable = integer(h + 48);
  check(integer(h + 24) === 1 && integer(h + 72) === 2 && firstUsable >= 34 &&
    firstUsable <= lastUsable && backupLba > lastUsable && backupLba < 2 ** 32, 'Invalid GPT bounds');
  const count = v.getUint32(h + 80, true), entrySize = v.getUint32(h + 84, true);
  check(count === 48 && entrySize === 128, 'Requires tested M3X partition table');
  const entries = bytes.subarray(1024, 1024 + count * entrySize);
  check(crc32(entries) === v.getUint32(h + 88, true), 'GPT entries CRC mismatch');
  const partitions = [], decoder = new TextDecoder('utf-16le', { fatal: true });
  for (let i = 0; i < count; i++) {
    const p = 1024 + i * entrySize;
    if (bytes.subarray(p, p + 16).every(b => b === 0)) continue;
    const name = decoder.decode(bytes.subarray(p + 56, p + 128)).replace(/\0+$/, '');
    const start = integer(p + 32), end = integer(p + 40);
    check(name && !partitions.some(other => other.name === name) && start >= firstUsable &&
      end >= start && end <= lastUsable, 'Invalid GPT partition');
    check(!partitions.some(other => start <= other.end && end >= other.start), 'Overlapping GPT partitions');
    partitions.push({ name, start, end, sectors: end - start + 1 });
  }
  const boot = partitions.find(p => p.name === 'boot');
  check(boot && boot.start === PROFILE.bootStart && boot.sectors === PROFILE.bootSectors,
    'Boot partition does not match tested M3X layout');
  const recovery = partitions.find(p => p.name === 'recovery');
  check(recovery?.start === 921600 && recovery.sectors === 131072 &&
    partitions.find(p => p.name === 'system')?.start === 1054720 &&
    partitions.find(p => p.name === 'userdata')?.start === 9176080, 'Unexpected M3X partition map');
  const diskGuid = Array.from(bytes.subarray(h + 56, h + 72), b => b.toString(16).padStart(2, '0')).join('');
  return { boot, partitions, backupLba, diskGuid };
}
