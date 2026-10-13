// SPDX-License-Identifier: GPL-2.0-or-later
import { Adb, AdbDaemonTransport, LinuxFileType } from '@yume-chan/adb';
import { AdbDaemonWebUsbDeviceManager } from '@yume-chan/adb-daemon-webusb';
import { ReadableStream } from '@yume-chan/stream-extra';
import { unzipSync } from 'fflate';
import { PROFILE, checkIdentity, requireCondition as check, sha256 } from './profile.js';
import { validateStock } from './images.js';
import { safePath } from './bundle.js';

export const quote = value => "'" + String(value).replaceAll("'", "'\\''") + "'";
export class EphemeralCredentials {
  #key;
  async generateKey() {
    const pair = await crypto.subtle.generateKey({ name: 'RSASSA-PKCS1-v1_5', modulusLength: 2048,
      publicExponent: new Uint8Array([1, 0, 1]), hash: 'SHA-1' }, true, ['sign', 'verify']);
    this.#key = { buffer: new Uint8Array(await crypto.subtle.exportKey('pkcs8', pair.privateKey)),
      name: 'Rockbox-M3X-Web' };
    return this.#key;
  }
  async *iterateKeys() { if (this.#key) yield this.#key; }
  forget() { this.#key?.buffer.fill(0); this.#key = undefined; }
}
export class BrowserAdb {
  static async request(log = () => {}) {
    const manager = AdbDaemonWebUsbDeviceManager.BROWSER;
    check(manager, 'WebUSB requires Chrome/Edge desktop over HTTPS or localhost');
    const device = await manager.requestDevice(); check(device, 'No ADB device selected');
    const credentials = new EphemeralCredentials(); let timer;
    try {
      const connection = await device.connect();
      const transport = await Promise.race([
        AdbDaemonTransport.authenticate({ serial: device.serial, connection, credentialStore: credentials }),
        new Promise((_, reject) => { timer = setTimeout(() => {
          device.raw.close().catch(() => {}); reject(new Error('ADB authorization timed out'));
        }, 120000); }),
      ]);
      return new BrowserAdb(new Adb(transport), credentials, log);
    } catch (error) { credentials.forget(); if (device.raw.opened) await device.raw.close(); throw error; }
    finally { clearTimeout(timer); }
  }
  constructor(adb, credentials, log) { this.adb = adb; this.credentials = credentials; this.log = log; }
  async shell(command, { allowFailure = false, timeout = 120000 } = {}) {
    const socket = await this.adb.createSocket(`shell:${command}; __m3x_status=$?; printf '\\nM3X_WEB_EXIT:%s\\n' "$__m3x_status"`);
    const reader = socket.readable.getReader(), decoder = new TextDecoder(); let output = '', timer;
    try {
      await Promise.race([
        (async () => {
          for (;;) {
            const { value, done } = await reader.read(); if (done) break;
            const chunk = decoder.decode(value, { stream: true }); output += chunk;
            check(output.length < 2 * 1024 * 1024, 'Shell output limit exceeded');
            this.log(chunk.replace(/M3X_WEB_EXIT:[0-9]+/g, ''));
          }
          output += decoder.decode();
        })(),
        new Promise((_, reject) => { timer = setTimeout(() => {
          socket.close(); reject(new Error('ADB command timed out; check player state before retry'));
        }, timeout); }),
      ]);
      const status = output.match(/\r?\nM3X_WEB_EXIT:([0-9]+)\r?\n\s*$/);
      check(status, 'ADB command disconnected before reporting exit status');
      const result = { code: Number(status[1]), output: output.slice(0, status.index).replaceAll('\r\n', '\n') };
      check(allowFailure || result.code === 0, `Device command failed (${result.code}): ${result.output}`);
      return result;
    } finally {
      clearTimeout(timer);
      try { await socket.close(); } catch { /* already disconnected */ }
      try { await reader.cancel(); } catch { /* already closed */ }
      reader.releaseLock();
    }
  }
  async preflight() {
    const prop = async name => (await this.shell(`getprop ${name}`)).output.trim();
    // Android's USB serial is not the Qualcomm chip serial reported by Sahara.
    const serialText = (await this.shell('cat /sys/devices/soc0/serial_number')).output.trim();
    const serial = Number(serialText);
    check(/^[0-9]+$/.test(serialText) && serial > 0 && serial <= 0xffffffff, 'Cannot read a valid Qualcomm chip serial');
    const info = {
      manufacturer: await prop('ro.product.manufacturer'), model: await prop('ro.product.model'),
      firmware: await prop('ro.build.display.id'), api: await prop('ro.build.version.sdk'),
      arch: (await this.shell('uname -m')).output.trim(), serial: serial.toString(16).padStart(8, '0'),
      androidActive: await prop('init.svc.zygote') === 'running' && await prop('init.svc.surfaceflinger') === 'running',
      rockboxRunning: (await this.shell('pidof rockbox rockbox-dbg', { allowFailure: true })).code === 0,
    };
    const battery = (await this.shell('dumpsys battery')).output.match(/^\s*level:\s*(\d+)\s*$/m);
    info.battery = battery ? Number(battery[1]) : NaN;
    // Identify the selected device before asking Magisk for any root permission.
    checkIdentity(info);
    const root = await this.shell("su -c 'id -u'", { allowFailure: true });
    info.rooted = root.code === 0 && root.output.trim() === '0';
    this.context = checkIdentity(info); return this.context;
  }
  async pushFiles(files, stage) {
    check(/^\/data\/local\/tmp\/m3x-web\.[A-Za-z0-9]+$/.test(stage), 'Unsafe staging path');
    check(Object.keys(files).every(safePath), 'Unsafe upload path');
    const parents = [...new Set(Object.keys(files).filter(n => n.includes('/')).map(n => n.slice(0, n.lastIndexOf('/'))))];
    if (parents.length) await this.shell(`mkdir -p ${parents.map(n => quote(stage + '/' + n)).join(' ')}`);
    const sync = await this.adb.sync();
    try {
      for (const [name, data] of Object.entries(files)) {
        check(!name.startsWith('/') && name.split('/').every(p => p && p !== '..' && p !== '.') && !name.includes('\\'), 'Unsafe upload path');
        let p = 0;
        await sync.write({ filename: stage + '/' + name, permission: 0o644,
          file: new ReadableStream({ pull(controller) {
            if (p === data.length) { controller.close(); return; }
            controller.enqueue(data.subarray(p, p + 65536)); p = Math.min(p + 65536, data.length);
          } }) });
      }
    } finally { await sync.dispose(); }
  }
  async stage() {
    const result = (await this.shell('mktemp -d /data/local/tmp/m3x-web.XXXXXX')).output.trim();
    check(/^\/data\/local\/tmp\/m3x-web\.[A-Za-z0-9]+$/.test(result), 'Unexpected staging directory'); return result;
  }
  async readFile(path, limit) {
    const sync = await this.adb.sync(); let reader;
    try {
      const stat = await sync.lstat(path);
      check(stat.type === LinuxFileType.File && Number(stat.size) > 0 && Number(stat.size) <= limit, 'Missing/unsafe staged output');
      const out = new Uint8Array(Number(stat.size)); let p = 0;
      reader = sync.read(path).getReader();
      for (;;) {
        const { value, done } = await reader.read(); if (done) break;
        check(p + value.length <= out.length, 'Staged output grew during read'); out.set(value, p); p += value.length;
      }
      check(p === out.length, 'Staged output truncated'); return out;
    } finally { reader?.releaseLock(); await sync.dispose(); }
  }
  async prepareMagisk(stock, apk) {
    const context = await this.preflight(); check(context.rooted === false, 'Already rooted: skip rooting');
    await validateStock(stock);
    check(apk.length === PROFILE.magiskSize && await sha256(apk) === PROFILE.magiskSha256,
      'Select the official release Magisk-v30.7.apk, not debug/repacked/another version');
    const names = {
      'assets/boot_patch.sh': 'boot_patch.sh', 'assets/util_functions.sh': 'util_functions.sh',
      'assets/stub.apk': 'stub.apk', 'lib/arm64-v8a/libbusybox.so': 'busybox',
      'lib/arm64-v8a/libmagiskboot.so': 'magiskboot', 'lib/arm64-v8a/libmagiskinit.so': 'magiskinit',
      'lib/arm64-v8a/libmagisk.so': 'magisk', 'lib/arm64-v8a/libinit-ld.so': 'init-ld',
    };
    const extracted = unzipSync(apk, { filter: file => Object.hasOwn(names, file.name) });
    const files = { 'boot.img': stock, 'Magisk.apk': apk };
    for (const [source, name] of Object.entries(names)) {
      check(extracted[source], 'Missing Magisk component: ' + source); files[name] = extracted[source];
    }
    const stage = await this.stage();
    try {
      await this.pushFiles(files, stage);
      // Non-root file patch only. Official script repacks new-boot.img; no
      // partition is opened. Preserve the recorded legacy profile's flags.
      await this.shell(`cd ${quote(stage)} && chmod 755 busybox magiskboot magiskinit magisk init-ld && ` +
        `TMPDIR=${quote(stage)} BOOTMODE=true ASH_STANDALONE=1 KEEPVERITY=false KEEPFORCEENCRYPT=false ` +
        `PREINITDEVICE=userdata ./busybox sh boot_patch.sh boot.img`);
      const magisk = await this.readFile(stage + '/new-boot.img', PROFILE.bootSectors * 512);
      // Install the hash-checked official manager, not a pre-patched third-party APK.
      const installed = await this.shell(`pm install -r ${quote(stage + '/Magisk.apk')}`);
      check(/^Success\s*$/m.test(installed.output), 'Magisk manager installation did not report Success');
      return { context, magisk };
    } finally { await this.shell(`rm -rf ${quote(stage)}`).catch(() => {}); }
  }
  async runInstaller(bundle, action) {
    check(['check', 'install'].includes(action), 'Invalid installer action');
    const info = await this.preflight(); check(info.rooted, 'Grant Magisk root for ADB first');
    const stage = await this.stage(); let completed = false;
    try {
      await this.pushFiles(bundle.files, stage);
      const result = await this.shell(`su -c ${quote('sh ' + quote(stage + '/install.sh') + ' ' + action)}`,
        { timeout: 600000, allowFailure: true });
      completed = true;
      check(result.code === 0, 'Installer failed; inspect saved snapshot and restore log'); return result;
    } finally {
      if (completed) await this.shell(`rm -rf ${quote(stage)}`).catch(() => {});
      else this.log('Staging retained after incomplete/disconnected operation: ' + stage);
    }
  }
  async rollback(name) {
    check(/^[A-Za-z0-9_-][A-Za-z0-9._-]*$/.test(name) && !name.includes('..'), 'Invalid backup snapshot name');
    const info = await this.preflight(); check(info.rooted, 'Grant Magisk root for ADB');
    const path = '/data/adb/rockbox-m3x-backups/' + name;
    return this.shell(`su -c ${quote('sh ' + quote(path + '/restore.sh') + ' rollback ' + quote(path))}`, { timeout: 600000 });
  }
  async enterEdl(confirmed) {
    check(confirmed === true && this.context, 'Explicit EDL reboot confirmation required');
    // Android reboot API, not a shell interpolation. Reboot is never automatic.
    await this.adb.power.reboot('edl');
  }
  async close() { try { await this.adb.close(); } finally { this.credentials.forget(); } }
}
