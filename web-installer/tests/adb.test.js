import test from 'node:test';
import assert from 'node:assert/strict';
import { LinuxFileType } from '@yume-chan/adb';
import { BrowserAdb } from '../src/adb.js';
import { context } from './helpers.js';

function socket(output) {
  return { closed: false, readable: new ReadableStream({ start(controller) {
    controller.enqueue(new TextEncoder().encode(output)); controller.close();
  } }), close() { this.closed = true; } };
}
test('legacy Android shell exit marker handles CRLF, failure and abrupt disconnect', async () => {
  for (const [output, success] of [['result\r\nM3X_WEB_EXIT:0\r\n', true],
    ['failure\r\nM3X_WEB_EXIT:17\r\n', false], ['incomplete result', false]]) {
    const sock = socket(output); const adb = new BrowserAdb({ createSocket: async () => sock }, {}, () => {});
    if (success) assert.deepEqual(await adb.shell('id'), { code: 0, output: 'result' });
    else await assert.rejects(adb.shell('id'));
    assert(sock.closed);
  }
});
test('ADB shell timeout closes socket and retains original error', async () => {
  let controller;
  const sock = { readable: new ReadableStream({ start(c) { controller = c; } }), close() { controller.close(); } };
  const adb = new BrowserAdb({ createSocket: async () => sock }, {}, () => {});
  await assert.rejects(adb.shell('id', { timeout: 1 }), /timed out/);
});
test('wrong device is refused before requesting any Magisk root permission', async () => {
  const calls = [], adb = new BrowserAdb({ serial: '00112233' }, {}, () => {});
  const props = { 'ro.product.manufacturer': 'Other', 'ro.product.model': 'Phone', 'ro.build.display.id': '1.75',
    'ro.build.version.sdk': '25', 'init.svc.zygote': 'running', 'init.svc.surfaceflinger': 'running' };
  adb.shell = async command => {
    calls.push(command);
    return { code: command.startsWith('pidof') ? 1 : 0,
      output: command.startsWith('getprop ') ? props[command.slice(8)] : command === 'uname -m' ? 'aarch64' : 'level: 80' };
  };
  await assert.rejects(adb.preflight(), /Unsupported manufacturer/);
  assert(!calls.some(command => command.startsWith('su ')));
});
test('ADB sync File enum (8, not mode bits) accepted; links/directories refused', async () => {
  for (const type of [LinuxFileType.File, LinuxFileType.Link, LinuxFileType.Directory]) {
    let disposed = false;
    const sync = { async lstat() { return { type, size: 3 }; },
      read() { return new ReadableStream({ start(c) { c.enqueue(new Uint8Array([1, 2, 3])); c.close(); } }); },
      async dispose() { disposed = true; } };
    const adb = new BrowserAdb({ sync: async () => sync }, {}, () => {});
    if (type === LinuxFileType.File) assert.deepEqual(await adb.readFile('/tmp/a', 10), new Uint8Array([1, 2, 3]));
    else await assert.rejects(adb.readFile('/tmp/a', 10), /unsafe/);
    assert(disposed);
  }
});
test('installer staging retained if command disconnects, never deleted under running root process', async () => {
  const commands = [], adb = new BrowserAdb({}, {}, () => {});
  adb.preflight = async () => context({ rooted: true });
  adb.stage = async () => '/data/local/tmp/m3x-web.A12345'; adb.pushFiles = async () => {};
  adb.shell = async command => { commands.push(command); throw new Error('connection lost'); };
  await assert.rejects(adb.runInstaller({ files: {} }, 'install'), /connection lost/);
  assert.equal(commands.length, 1); assert(!commands.some(command => command.startsWith('rm')));
});
test('installer staging cleaned only after confirmed exit, including failed script exit', async () => {
  for (const code of [0, 1]) {
    const commands = [], adb = new BrowserAdb({}, {}, () => {});
    adb.preflight = async () => context({ rooted: true });
    adb.stage = async () => '/data/local/tmp/m3x-web.A12345'; adb.pushFiles = async () => {};
    adb.shell = async command => { commands.push(command); return { code, output: '' }; };
    if (code === 0) await adb.runInstaller({ files: {} }, 'check');
    else await assert.rejects(adb.runInstaller({ files: {} }, 'check'), /Installer failed/);
    assert.equal(commands.length, 2); assert.match(commands[1], /^rm -rf/);
  }
});
