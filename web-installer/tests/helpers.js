import { crc32 } from '../src/gpt.js';
export function context(overrides = {}) {
  return { manufacturer: 'Shanling', model: 'Shanling M3X', firmware: '1.75', api: '25',
    arch: 'aarch64', serial: '00112233', battery: 80, androidActive: true, rockboxRunning: false,
    rooted: false, ...overrides };
}
export function gpt() {
  const bytes = new Uint8Array(34 * 512), v = new DataView(bytes.buffer), h = 512;
  bytes.set(new TextEncoder().encode('EFI PART'), h);
  v.setUint32(h + 8, 0x10000, true); v.setUint32(h + 12, 92, true);
  for (const [offset, n] of [[24, 1], [32, 60000000], [40, 34], [48, 59999966], [72, 2]])
    v.setBigUint64(h + offset, BigInt(n), true);
  bytes.fill(0x12, h + 56, h + 72);
  v.setUint32(h + 80, 48, true); v.setUint32(h + 84, 128, true);
  const entries = [['boot', 790528, 131072], ['recovery', 921600, 131072],
    ['system', 1054720, 6291456], ['userdata', 9176080, 40000000]];
  entries.forEach(([name, start, sectors], i) => {
    const p = 1024 + i * 128; bytes.fill(i + 1, p, p + 32);
    v.setBigUint64(p + 32, BigInt(start), true); v.setBigUint64(p + 40, BigInt(start + sectors - 1), true);
    [...name].forEach((c, j) => v.setUint16(p + 56 + j * 2, c.charCodeAt(0), true));
  });
  fixGpt(bytes); return bytes;
}
export function fixGpt(bytes) {
  const v = new DataView(bytes.buffer);
  v.setUint32(512 + 88, crc32(bytes.subarray(1024, 1024 + 48 * 128)), true);
  v.setUint32(512 + 16, 0, true);
  v.setUint32(512 + 16, crc32(bytes.subarray(512, 512 + 92)), true);
}
export function syntheticFdt() {
  const words = [], strings = new TextEncoder().encode('pins\0function\0model\0'), names = { pins: 0, function: 5, model: 14 };
  const pushWord = n => words.push(n >>> 24, n >>> 16 & 255, n >>> 8 & 255, n & 255);
  const str = s => { words.push(...new TextEncoder().encode(s), 0); while (words.length % 4) words.push(0); };
  const node = s => { pushWord(1); str(s); };
  const prop = (key, value) => {
    const bytes = new TextEncoder().encode(value + '\0');
    pushWord(3); pushWord(bytes.length); pushWord(names[key]); words.push(...bytes);
    while (words.length % 4) words.push(0);
  };
  node(''); prop('model', 'test'); node('soc'); node('pinctrl@1000000'); node('tlmm_gpio_key');
  for (const state of ['gpio_key_active', 'gpio_key_suspend']) {
    node(state);
    for (const group of ['mux', 'config']) {
      node(group); prop('pins', ['gpio13', 'gpio17', 'gpio91', 'gpio1', 'gpio29', 'gpio30', 'gpio18', 'gpio19', 'gpio65'].join('\0'));
      prop('function', 'gpio'); pushWord(2);
    }
    pushWord(2);
  }
  pushWord(2); node('i2c_5'); prop('pins', 'gpio18\0gpio19'); prop('function', 'blsp_i2c5'); pushWord(2);
  pushWord(2); pushWord(2); pushWord(2); pushWord(9);
  const out = new Uint8Array(56 + words.length + strings.length), v = new DataView(out.buffer);
  [0xd00dfeed, out.length, 56, 56 + words.length, 40, 17, 16, 0, strings.length, words.length]
    .forEach((n, i) => v.setUint32(i * 4, n));
  out.set(words, 56); out.set(strings, 56 + words.length); return out;
}
export function transport(chunks) {
  return { writes: [], async read() {
    if (!chunks.length) throw new Error('Unexpected extra read'); return chunks.shift();
  }, async write(bytes) { this.writes.push(bytes.slice()); } };
}
export const encode = value => new TextEncoder().encode(value);
