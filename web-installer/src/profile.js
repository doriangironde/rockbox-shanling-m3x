// SPDX-License-Identifier: GPL-2.0-or-later
// Deliberately one firmware/profile; no generic Qualcomm flasher.
export const PROFILE = Object.freeze({
  manufacturer: 'Shanling', model: 'Shanling M3X', firmware: '1.75', api: '25',
  stockSize: 26576168, bootOsVersion: 0x0e040924,
  stockSha256: '8c77cb231ea30776036035ecd088fb1b204ceccb2bff0903766eea5413183342',
  stockKernelSha256: 'ce0c0a0024f18fdfa8333150947fba863093cba4bead3130e10770b552d18514',
  programmerSha256: 'e74962eb179e0c864c30d93193fcb9b4eb1c441652891f1008877422d9f43d2a',
  programmerSize: 380380,
  magiskSha256: 'e0d32d2123532860f97123d927b1bb86c4e08e6fd8a48bfc6b5bee0afae9ebd5',
  magiskSize: 11613864,
  sectorSize: 512, bootStart: 790528, bootSectors: 131072,
  edlVendor: 0x05c6, edlProduct: 0x9008,
});
export const RELEASE = Object.freeze({
  id: 'dev-bf6af2444b56',
  installerSha256: '9441e62078f0f5d658fc9ae5039823336feb66492f7599d200ba4da3b626bd14',
  page: 'https://github.com/doriangironde/rockbox-shanling-m3x/releases/tag/dev-bf6af2444b56',
});
export function requireCondition(condition, message) {
  if (!condition) throw new Error(message);
}
export async function sha256(data) {
  const result = await crypto.subtle.digest('SHA-256', data);
  return Array.from(new Uint8Array(result), b => b.toString(16).padStart(2, '0')).join('');
}
export function checkIdentity(info) {
  for (const key of ['manufacturer', 'model', 'firmware', 'api']) {
    requireCondition(info[key] === PROFILE[key], `Unsupported ${key}: ${info[key]}`);
  }
  requireCondition(info.arch === 'aarch64', 'Requires ARM64 M3X');
  requireCondition(info.androidActive && !info.rockboxRunning, 'Return to Android and stop Rockbox first');
  requireCondition(Number.isInteger(info.battery) && info.battery >= 50, 'Charge battery to at least 50%');
  requireCondition(/^[0-9a-f]{8}$/i.test(info.serial), 'Cannot safely bind ADB identity to Sahara chip serial');
  return Object.freeze({ ...info, serial: info.serial.toLowerCase() });
}
export function confirmationFor(digest, restore = false) {
  requireCondition(/^[0-9a-f]{64}$/.test(digest), 'Invalid image digest');
  return `${restore ? 'RESTORE' : 'FLASH'} M3X 1.75 ${digest.slice(0, 12)}`;
}
