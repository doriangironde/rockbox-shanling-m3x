// SPDX-License-Identifier: GPL-2.0-or-later
import { unzipSync } from 'fflate';
import { RELEASE, requireCondition as check, sha256 } from './profile.js';
const decoder = new TextDecoder('utf-8', { fatal: true });
export function safePath(name) {
  return typeof name === 'string' && name.length > 0 && !name.includes('\\') &&
    !/[\x00-\x1f\x7f]/.test(name) && !name.startsWith('/') &&
    name.split('/').every(part => part !== '' && part !== '.' && part !== '..');
}
export async function installerBundle(bytes) {
  check(bytes.length <= 32 * 1024 * 1024 && await sha256(bytes) === RELEASE.installerSha256,
    'Installer does not match the pinned experimental release; download the attached installer ZIP');
  // Only unzip after authenticating the entire archive against pinned release.
  const files = unzipSync(bytes);
  check(Object.keys(files).every(safePath) && files.SHA256SUMS && files['manifest.json'], 'Invalid bundle paths/manifest');
  const expected = new Map();
  for (const line of decoder.decode(files.SHA256SUMS).trimEnd().split('\n')) {
    const match = line.match(/^([0-9a-f]{64})  (.+)$/);
    check(match && safePath(match[2]) && match[2] !== 'SHA256SUMS' && !expected.has(match[2]), 'Invalid checksum path');
    expected.set(match[2], match[1]);
  }
  check(Object.keys(files).length === expected.size + 1, 'Unexpected/missing bundle files');
  let total = 0;
  for (const [name, hash] of expected) {
    check(files[name] && await sha256(files[name]) === hash, 'Bundle checksum mismatch: ' + name);
    total += files[name].length;
  }
  check(total < 128 * 1024 * 1024, 'Bundle exceeds staging budget');
  const manifest = JSON.parse(decoder.decode(files['manifest.json']));
  check(manifest.release === RELEASE.id && manifest.runtime_layout === 'private-module-runtime-v1', 'Unsupported installer layout');
  check(decoder.decode(files['payload/release-id']).trim() === RELEASE.id, 'Release ID mismatch');
  check(files['install.sh'] && files['payload/rockbox'] && files['payload/module/service.sh'], 'Incomplete installer');
  return { files, manifest, total };
}
