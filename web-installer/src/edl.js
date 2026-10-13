// SPDX-License-Identifier: GPL-2.0-or-later
// Wire format/reference: linux-msm/qdl (BSD-3-Clause), see THIRD_PARTY.md.
import { PROFILE, requireCondition as check, sha256 } from './profile.js';

const encoder = new TextEncoder(), decoder = new TextDecoder('utf-8', { fatal: true });
export function packet(command, ...words) {
  const bytes = new Uint8Array(8 + words.length * 4), v = new DataView(bytes.buffer);
  [command, bytes.length, ...words].forEach((n, i) => v.setUint32(i * 4, n, true));
  return bytes;
}
export class ByteStream {
  constructor(transport) { this.transport = transport; this.buffer = new Uint8Array(); }
  async fill() {
    const chunk = await this.transport.read();
    check(chunk instanceof Uint8Array, 'Invalid USB transfer');
    check(this.buffer.length + chunk.length <= 2 * 1024 * 1024, 'USB framing buffer limit exceeded');
    const out = new Uint8Array(this.buffer.length + chunk.length);
    out.set(this.buffer); out.set(chunk, this.buffer.length); this.buffer = out;
  }
  async exact(size) {
    check(Number.isSafeInteger(size) && size >= 0 && size <= 1024 * 1024, 'Invalid read length');
    while (this.buffer.length < size) await this.fill();
    const out = this.buffer.slice(0, size); this.buffer = this.buffer.subarray(size); return out;
  }
  async saharaPacket() {
    const header = await this.exact(8), v = new DataView(header.buffer), size = v.getUint32(4, true);
    check(size >= 8 && size <= 256 && size % 4 === 0, 'Invalid Sahara packet length');
    const out = new Uint8Array(size); out.set(header); out.set(await this.exact(size - 8), 8); return out;
  }
  async xml() {
    const marker = encoder.encode('</data>');
    for (;;) {
      let end = -1;
      for (let i = 0; i <= this.buffer.length - marker.length; i++) {
        if (marker.every((n, j) => this.buffer[i + j] === n)) { end = i + marker.length; break; }
      }
      if (end >= 0) {
        const xml = decoder.decode(this.buffer.subarray(0, end));
        this.buffer = this.buffer.subarray(end);
        // Preserve every byte after the closing tag. Trimming whitespace here
        // can silently corrupt a raw payload beginning with CR/LF. A different
        // programmer's XML/raw separator needs explicit validated framing.
        return parseResponse(xml);
      }
      check(this.buffer.length <= 65536, 'Firehose XML message too large'); await this.fill();
    }
  }
}
function unescapeXml(value) {
  check(!/&(?!(amp|lt|gt|quot|apos);)/.test(value), 'Unsupported XML entity');
  return value.replace(/&(amp|lt|gt|quot|apos);/g, (_, name) =>
    ({ amp: '&', lt: '<', gt: '>', quot: '"', apos: "'" })[name]);
}
export function parseResponse(xml) {
  check(!/<!|[\x00-\x08\x0b\x0c\x0e-\x1f]/.test(xml), 'Unsafe Firehose XML');
  const body = xml.trim().replace(/^<\?xml\s+[^?]*\?>\s*/, '').match(/^<data>\s*([\s\S]*?)\s*<\/data>$/);
  check(body, 'Invalid Firehose envelope');
  let remaining = body[1]; const result = { logs: [], response: undefined };
  while (remaining.trim()) {
    const node = remaining.match(/^\s*<(log|response)\s+([^<>]*?)\s*\/>/);
    check(node, 'Invalid Firehose element'); remaining = remaining.slice(node[0].length);
    let attrs = node[2]; const values = Object.create(null);
    while (attrs.trim()) {
      const attr = attrs.match(/^\s*([A-Za-z_][A-Za-z_0-9]*)\s*=\s*(?:"([^"<>]*)"|'([^'<>]*)')/);
      check(attr && !Object.hasOwn(values, attr[1]), 'Invalid/duplicate Firehose attribute');
      values[attr[1]] = unescapeXml(attr[2] ?? attr[3]); attrs = attrs.slice(attr[0].length);
    }
    if (node[1] === 'log') result.logs.push(values.value ?? '');
    else { check(!result.response, 'Multiple Firehose responses'); result.response = values; }
  }
  check(result.logs.length > 0 || result.response, 'Empty Firehose response'); return result;
}

export async function loadProgrammer(stream, programmer, expectedSerial) {
  check(programmer.length === PROFILE.programmerSize && await sha256(programmer) === PROFILE.programmerSha256,
    'Requires the exact verified MSM8937.mbn programmer');
  return saharaUpload(stream, programmer, expectedSerial);
}
// Low-level wire state machine; callers must authenticate the programmer first.
// Production controller uses loadProgrammer, not this replay-test entry point.
export async function saharaUpload(stream, programmer, expectedSerial) {
  check(/^[0-9a-f]{8}$/.test(expectedSerial), 'Invalid expected chip serial');
  const read = async (command, size) => {
    const p = await stream.saharaPacket(), v = new DataView(p.buffer);
    check(v.getUint32(0, true) === command && p.length === size, 'Unexpected Sahara state'); return v;
  };
  const hello = await read(1, 48);
  check(hello.getUint32(8, true) >= 2 && hello.getUint32(12, true) <= 2 && hello.getUint32(20, true) === 0,
    'Unsupported Sahara version/mode; reconnect in fresh EDL');
  // Read chip identity before executing a programmer. Only the same device which
  // passed ADB model/firmware checks is eligible; 05c6:9008 alone is not identity.
  await stream.transport.write(packet(2, 2, 1, 0, 3, 0, 0, 0, 0, 0, 0));
  await read(11, 8); await stream.transport.write(packet(13, 1));
  const reply = await read(14, 16);
  check(reply.getUint32(8, true) === 1 && reply.getUint32(12, true) === 4, 'Invalid Sahara serial response');
  await stream.transport.write(packet(15, 1));
  const serialBytes = await stream.exact(4);
  const serial = new DataView(serialBytes.buffer).getUint32(0, true).toString(16).padStart(8, '0');
  check(serial === expectedSerial, 'EDL device differs from checked ADB device; reconnect the same M3X');
  await stream.transport.write(packet(12, 0)); await read(1, 48);
  await stream.transport.write(packet(2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0));
  let finished = false;
  for (let requests = 0; requests < 4096; requests++) {
    const p = await stream.saharaPacket(), v = new DataView(p.buffer), command = v.getUint32(0, true);
    if (command === 3 || command === 18) {
      check(!finished && p.length === (command === 3 ? 20 : 32), 'Invalid programmer request');
      const get = offset => {
        const n = command === 3 ? BigInt(v.getUint32(offset, true)) : v.getBigUint64(offset, true);
        check(n <= BigInt(Number.MAX_SAFE_INTEGER), 'Sahara range outside safe bounds'); return Number(n);
      };
      const image = get(8), offset = get(command === 3 ? 12 : 16), length = get(command === 3 ? 16 : 24);
      check(image === 13 && length > 0 && offset <= programmer.length && length <= programmer.length - offset,
        'Unexpected Sahara image/range');
      await stream.transport.write(programmer.subarray(offset, offset + length));
    } else if (command === 4) {
      check(!finished && p.length === 16 && v.getUint32(8, true) === 13 && v.getUint32(12, true) === 0,
        'Programmer authentication/upload failed');
      finished = true; await stream.transport.write(packet(5));
    } else if (command === 6) {
      check(finished && p.length === 12 && v.getUint32(8, true) <= 1, 'Invalid Sahara completion');
      // Single EHOSTDL image quirk: some MSM89xx loaders return pending (0).
      return serial;
    } else throw new Error('Unsupported Sahara packet/state');
  }
  throw new Error('Sahara request limit exceeded');
}

export class Firehose {
  constructor(stream, log = () => {}) { this.stream = stream; this.log = log; this.payloadSize = 16384; this.evidence = ''; }
  async response(raw = false, allowNak = false) {
    for (let i = 0; i < 256; i++) {
      const message = await this.stream.xml();
      for (const line of message.logs) {
        this.log(line); this.evidence = (this.evidence + '\n' + line).slice(-65536);
      }
      if (!message.response) continue;
      const r = message.response;
      check(r.value === 'ACK' || (allowNak && r.value === 'NAK'), 'Firehose rejected command');
      check((r.rawmode === undefined || r.rawmode === 'true' || r.rawmode === 'false') &&
        (r.rawmode === 'true') === raw, 'Unexpected Firehose raw mode'); return r;
    }
    throw new Error('Firehose response limit exceeded');
  }
  async send(tag, attrs) {
    check(['configure', 'read', 'program'].includes(tag), 'Unsupported Firehose operation');
    const fields = Object.entries(attrs).map(([key, value]) => {
      check(/^[A-Za-z_][A-Za-z_0-9]*$/.test(key) && /^[A-Za-z0-9._-]+$/.test(String(value)), 'Unsafe command attribute');
      return `${key}="${value}"`;
    }).join(' ');
    // Use the same newline-terminated XML as the hardware-tested native QDL client.
    await this.stream.transport.write(encoder.encode(`<?xml version="1.0"?>\n<data><${tag} ${fields}/></data>\n`));
  }
  async configure() {
    for (let attempt = 0; attempt < 3; attempt++) {
      await this.send('configure', { MemoryName: 'eMMC', MaxPayloadSizeToTargetInBytes: this.payloadSize,
        Verbose: 0, ZlpAwareHost: 1, SkipStorageInit: 0 });
      const r = await this.response(false, true);
      const size = Number(r.MaxPayloadSizeToTargetInBytes ?? r.MaxPayloadSizeToTargetInBytesSupported);
      check(Number.isInteger(size) && size >= 512 && size <= 1024 * 1024 && size % 512 === 0,
        'Invalid Firehose payload negotiation');
      if (r.value === 'NAK' || size !== this.payloadSize) { this.payloadSize = size; continue; }
      check((r.TargetName === '8937' || /TargetName\s*[=:]\s*"?8937\b/.test(this.evidence)) &&
        (r.MemoryName?.toLowerCase() === 'emmc' || /MemoryName\s*[=:]\s*"?emmc\b/i.test(this.evidence)),
        'Programmer target/storage identity not confirmed');
      return;
    }
    throw new Error('Firehose configure negotiation failed');
  }
  async read(start, sectors, progress = () => {}) {
    check(Number.isSafeInteger(start) && start >= 0 && Number.isSafeInteger(sectors) && sectors > 0 &&
      sectors <= PROFILE.bootSectors && start + sectors <= 2 ** 32, 'Unsafe read range');
    await this.send('read', { SECTOR_SIZE_IN_BYTES: 512, num_partition_sectors: sectors,
      physical_partition_number: 0, start_sector: start });
    await this.response(true);
    const out = new Uint8Array(sectors * 512);
    for (let p = 0; p < out.length;) {
      const size = Math.min(this.payloadSize, out.length - p);
      out.set(await this.stream.exact(size), p); p += size; progress(p / out.length);
    }
    await this.response(false); return out;
  }
  async writeBoot(bytes, progress = () => {}) {
    check(bytes.length === PROFILE.bootSectors * 512, 'Only full M3X boot-partition writes allowed');
    await this.send('program', { SECTOR_SIZE_IN_BYTES: 512, num_partition_sectors: PROFILE.bootSectors,
      physical_partition_number: 0, start_sector: PROFILE.bootStart });
    await this.response(true);
    for (let p = 0; p < bytes.length; p += this.payloadSize) {
      await this.stream.transport.write(bytes.subarray(p, p + this.payloadSize));
      progress(Math.min(p + this.payloadSize, bytes.length) / bytes.length);
    }
    await this.response(false);
  }
}

export class EdlUsbTransport {
  static async request() {
    check(navigator.usb, 'WebUSB requires a desktop Chromium browser');
    const device = await navigator.usb.requestDevice({ filters: [{ vendorId: PROFILE.edlVendor, productId: PROFILE.edlProduct }] });
    const transport = new EdlUsbTransport(device);
    try { await transport.open(); } catch (error) { await transport.close(); throw error; }
    return transport;
  }
  constructor(device) { this.device = device; this.closed = true; }
  async open() {
    check(this.device.vendorId === PROFILE.edlVendor && this.device.productId === PROFILE.edlProduct, 'Not Qualcomm EDL');
    await this.device.open();
    if (!this.device.configuration) await this.device.selectConfiguration(1);
    const choices = this.device.configuration.interfaces.flatMap(iface => iface.alternates.map(alt => ({ iface, alt })))
      .filter(({ alt }) => alt.interfaceClass === 0xff &&
        alt.endpoints.some(e => e.direction === 'in' && e.type === 'bulk') &&
        alt.endpoints.some(e => e.direction === 'out' && e.type === 'bulk'));
    check(choices.length === 1, 'Ambiguous EDL USB interface');
    const { iface, alt } = choices[0]; this.interface = iface.interfaceNumber;
    this.input = alt.endpoints.find(e => e.direction === 'in' && e.type === 'bulk');
    this.output = alt.endpoints.find(e => e.direction === 'out' && e.type === 'bulk');
    check(Number.isInteger(this.input.packetSize) && this.input.packetSize > 0 && this.input.packetSize <= 1024 &&
      Number.isInteger(this.output.packetSize) && this.output.packetSize > 0 && this.output.packetSize <= 1024,
      'Invalid EDL endpoint packet size');
    await this.device.claimInterface(this.interface);
    if (iface.alternate.alternateSetting !== alt.alternateSetting)
      await this.device.selectAlternateInterface(this.interface, alt.alternateSetting);
    this.closed = false;
  }
  async read() {
    check(!this.closed, 'EDL disconnected');
    let timer;
    try {
      return await Promise.race([
        (async () => {
          // Do not leave a Sahara receive pending across the programmer handoff.
          while (!this.closed) {
            const result = await this.device.transferIn(this.input.endpointNumber, this.input.packetSize);
            check(result.status === 'ok' && result.data, 'EDL bulk-in transfer failed');
            const bytes = new Uint8Array(result.data.buffer, result.data.byteOffset, result.data.byteLength);
            if (bytes.length) return bytes; // USB ZLP, not EOF.
          }
          throw new Error('EDL disconnected');
        })(),
        new Promise((_, reject) => { timer = setTimeout(() => {
          reject(new Error('EDL receive timeout; no automatic retry/reboot'));
        }, 30000); }),
      ]);
    } catch (error) { await this.close(); throw error; }
    finally { clearTimeout(timer); }
  }
  async write(bytes) {
    check(!this.closed, 'EDL disconnected');
    // Do not retry partial/destructive transfers. Never leak programmer chunks
    // across Sahara requests; Firehose caller already chunks at negotiated size.
    let timer;
    try {
      const result = await Promise.race([
        (async () => {
          const result = await this.device.transferOut(this.output.endpointNumber, bytes);
          check(result.status === 'ok' && result.bytesWritten === bytes.length, 'Short/failed EDL bulk-out transfer');
          if (bytes.length && bytes.length % this.output.packetSize === 0) {
            const zlp = await this.device.transferOut(this.output.endpointNumber, new Uint8Array());
            check(zlp.status === 'ok' && zlp.bytesWritten === 0, 'Failed EDL bulk-out ZLP');
          }
          return result;
        })(),
        new Promise((_, reject) => { timer = setTimeout(() => {
          this.close(); reject(new Error('EDL write timeout; partial write possible, do not reboot'));
        }, 30000); }),
      ]);
      return result;
    } finally { clearTimeout(timer); }
  }
  async close() {
    this.closed = true;
    if (this.device.opened) { try { await this.device.close(); } catch { /* physical disconnect */ } }
  }
}
