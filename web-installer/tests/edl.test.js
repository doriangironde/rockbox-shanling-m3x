import test from 'node:test';
import assert from 'node:assert/strict';
import { ByteStream, EdlUsbTransport, Firehose, loadProgrammer, packet, parseResponse, saharaUpload } from '../src/edl.js';
import { encode, transport } from './helpers.js';
const ack = raw => encode(`<?xml version="1.0"?><data><response value="ACK" rawmode="${raw}"/></data>`);
function transcript(serial = 0x00112233, request = packet(3, 13, 1, 3)) {
  const bytes = new Uint8Array(4); new DataView(bytes.buffer).setUint32(0, serial, true);
  return [packet(1, 2, 1, 4096, 0, 0, 0, 0, 0, 0, 0), packet(11), packet(14, 1, 4), bytes,
    packet(1, 2, 1, 4096, 0, 0, 0, 0, 0, 0, 0), request, packet(4, 13, 0), packet(6, 1)];
}
test('Sahara replay binds serial and exact requested programmer range', async () => {
  const usb = transport(transcript()), stream = new ByteStream(usb);
  assert.equal(await saharaUpload(stream, new Uint8Array([1, 2, 3, 4, 5]), '00112233'), '00112233');
  assert.deepEqual([...usb.writes[5]], [2, 3, 4]);
  assert.equal(new DataView(usb.writes[0].buffer).getUint32(20, true), 3); // command mode, before programmer
});
test('Sahara mismatched chip, image ID, bounds and failed auth refuse upload completion', async () => {
  await assert.rejects(saharaUpload(new ByteStream(transport(transcript(0xaabbccdd))), new Uint8Array(8), '00112233'), /differs/);
  for (const request of [packet(3, 12, 0, 1), packet(3, 13, 99, 1), packet(3, 13, 0, 999), packet(3, 13, 0, 0)])
    await assert.rejects(saharaUpload(new ByteStream(transport(transcript(0x00112233, request))), new Uint8Array(8), '00112233'), /image\/range/);
  const failed = transcript(); failed[6] = packet(4, 13, 2);
  await assert.rejects(saharaUpload(new ByteStream(transport(failed)), new Uint8Array(8), '00112233'), /authentication/);
  await assert.rejects(loadProgrammer(new ByteStream(transport([])), new Uint8Array(8), '00112233'), /exact verified/);
});
test('Sahara 64-bit request and single-image pending completion supported', async () => {
  const request = new Uint8Array(32), v = new DataView(request.buffer);
  v.setUint32(0, 18, true); v.setUint32(4, 32, true);
  v.setBigUint64(8, 13n, true); v.setBigUint64(16, 0n, true); v.setBigUint64(24, 4n, true);
  const chunks = transcript(0x00112233, request); chunks[7] = packet(6, 0);
  await saharaUpload(new ByteStream(transport(chunks)), new Uint8Array(4), '00112233');
  v.setBigUint64(16, 2n ** 63n, true);
  await assert.rejects(saharaUpload(new ByteStream(transport(transcript(0x00112233, request))), new Uint8Array(4), '00112233'), /safe bounds/);
});
test('fragmented/coalesced XML preserves binary bytes after raw ACK', async () => {
  const binary = new Uint8Array(512).fill(0x81), message = ack(true);
  const all = new Uint8Array(message.length + 512); all.set(message); all.set(binary, message.length);
  const usb = transport([all.subarray(0, 11), all.subarray(11), ack(false)]), hose = new Firehose(new ByteStream(usb));
  assert.deepEqual(await hose.read(790528, 1), binary);
  const command = new TextDecoder().decode(usb.writes[0]); assert.match(command, /start_sector="790528"/);
});
test('raw payload beginning with CR/LF is never trimmed as XML whitespace', async () => {
  for (const first of [10, 13]) {
    const bytes = new Uint8Array(512).fill(0x81); bytes[0] = first;
    const header = ack(true), joined = new Uint8Array(header.length + bytes.length);
    joined.set(header); joined.set(bytes, header.length);
    const hose = new Firehose(new ByteStream(transport([joined, ack(false)])));
    assert.deepEqual(await hose.read(790528, 1), bytes);
  }
});
test('Firehose logs and negotiated eMMC target accepted', async () => {
  const usb = transport([encode('<data><log value="startup"/></data>'),
    encode('<data><response value="ACK" TargetName="8937" MemoryName="eMMC" MaxPayloadSizeToTargetInBytes="16384"/></data>')]);
  const logs = [], hose = new Firehose(new ByteStream(usb), line => logs.push(line));
  await hose.configure(); assert.equal(hose.payloadSize, 16384); assert.deepEqual(logs, ['startup']);
});
test('Firehose rejects wrong target, NAK, rawmode mismatch and malformed XML', async () => {
  await assert.rejects(new Firehose(new ByteStream(transport([
    encode('<data><response value="ACK" TargetName="8998" MemoryName="eMMC" MaxPayloadSizeToTargetInBytes="16384"/></data>')
  ]))).configure(), /identity/);
  await assert.rejects(new Firehose(new ByteStream(transport([ack(false)]))).read(0, 1), /raw mode/);
  await assert.rejects(new Firehose(new ByteStream(transport([encode('<data><response value="NAK"/></data>')]))).read(0, 1), /rejected/);
  for (const xml of ['<!DOCTYPE x><data><log value="x"/></data>', '<data><response value="ACK" value="NAK"/></data>',
    '<data><response value="&evil;"/></data>', '<data><response value="ACK"/><response value="ACK"/></data>', '<data><unknown/></data>'])
    assert.throws(() => parseResponse(xml));
});
test('transport framing rejects impossible lengths and empty/oversized reads', async () => {
  const wrong = packet(1); new DataView(wrong.buffer).setUint32(4, 0xffffffff, true);
  await assert.rejects(new ByteStream(transport([wrong])).saharaPacket(), /packet length/);
  await assert.rejects(new ByteStream(transport([])).exact(1024 * 1024 + 1), /Invalid read/);
  await assert.rejects(new Firehose(new ByteStream(transport([]))).read(0, 0), /Unsafe read/);
  await assert.rejects(new Firehose(new ByteStream(transport([]))).writeBoot(new Uint8Array(512)), /Only full/);
});
test('Firehose payload changes require stable renegotiation before raw transfers', async () => {
  const usb = transport([
    encode('<data><response value="NAK" MaxPayloadSizeToTargetInBytes="65536"/></data>'),
    encode('<data><response value="ACK" TargetName="8937" MemoryName="eMMC" MaxPayloadSizeToTargetInBytes="65536"/></data>'),
  ]);
  const hose = new Firehose(new ByteStream(usb)); await hose.configure();
  assert.equal(hose.payloadSize, 65536); assert.equal(usb.writes.length, 2);
  assert.match(new TextDecoder().decode(usb.writes[1]), /InBytes="65536"/);
});
test('EDL bulk-out sends explicit ZLP only for exact packet-size multiples', async () => {
  const lengths = [], usb = new EdlUsbTransport({ transferOut: async (_, bytes) => {
    lengths.push(bytes.length); return { status: 'ok', bytesWritten: bytes.length };
  } });
  usb.closed = false; usb.output = { endpointNumber: 1, packetSize: 512 };
  await usb.write(new Uint8Array(512)); await usb.write(new Uint8Array(13));
  assert.deepEqual(lengths, [512, 0, 13]);
});
test('USB short writes fail without retry', async () => {
  let calls = 0;
  const usb = new EdlUsbTransport({ transferOut: async () => { calls++; return { status: 'ok', bytesWritten: 3 }; } });
  usb.closed = false; usb.output = { endpointNumber: 1 };
  await assert.rejects(usb.write(new Uint8Array(4)), /Short\/failed/); assert.equal(calls, 1);
});
