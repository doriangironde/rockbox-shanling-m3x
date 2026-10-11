// SPDX-License-Identifier: GPL-2.0-or-later
import { BrowserAdb } from './adb.js';
import { installerBundle } from './bundle.js';
import { ByteStream, EdlUsbTransport, Firehose, loadProgrammer } from './edl.js';
import { RootWorkflow } from './root-workflow.js';
import { PROFILE, RELEASE, confirmationFor, requireCondition as check, sha256 } from './profile.js';
import { parseGpt } from './gpt.js';

const $ = id => document.getElementById(id);
const writesEnabled = import.meta.env.VITE_M3X_ENABLE_EDL_WRITES === '1';
let adb, root, edl, context, programmer, diagnosticContext, diagnosticBackup, busy = false;
const file = id => $(id).files[0];
const log = value => { $('log').textContent = ($('log').textContent + value + '\n').slice(-131072); $('log').scrollTop = $('log').scrollHeight; };
const progress = value => { $('progress').value = value; };
$('release-link').href = RELEASE.page;
const supported = !!navigator.usb && window.isSecureContext;
$('compatibility').textContent = supported ? 'WebUSB available. USB selection and authorization are always explicit.' :
  'Requires desktop Chrome/Edge over HTTPS or localhost. No browser USB support here; use the CLI installer.';
if (writesEnabled) $('write-policy').textContent =
  'EXPERIMENTAL DEVELOPER BUILD: partition writes enabled by build-time opt-in. Browser rooting is NOT hardware-validated.';

function update() {
  const prepared = root?.phase === 'prepared', inspected = root?.phase === 'inspected';
  const backedUp = root?.phase === 'backed-up';
  const enable = (id, condition) => { $(id).disabled = busy || !condition; };
  enable('connect', supported && !adb && !edl);
  enable('disconnect', adb || edl);
  enable('check-install', adb && context?.rooted && file('installer-file'));
  enable('install', adb && context?.rooted && file('installer-file') && $('install-consent').checked);
  enable('rollback', adb && context?.rooted && $('rollback-name').value.trim());
  enable('prepare-root', adb && context?.rooted === false && !root && file('stock-file') &&
    file('magisk-file') && file('programmer-file') && $('prepare-consent').checked);
  enable('save-image', root?.metadata.imageSha256);
  enable('enter-edl', adb && prepared && $('edl-consent').checked);
  enable('connect-edl', supported && prepared && !edl && $('edl-consent').checked);
  enable('diagnostic-reboot', adb && !root && file('programmer-file') && $('edl-consent').checked);
  enable('diagnostic-read', supported && !root && !edl && (diagnosticContext || context) &&
    file('programmer-file') && $('edl-consent').checked);
  enable('save-backup', root?.metadata.backupSha256 || diagnosticBackup);
  enable('verify-backup', (inspected || backedUp) && file('backup-file'));
  enable('flash', writesEnabled && backedUp && edl && $('flash-risk').checked &&
    $('flash-confirmation').value === confirmationFor(root.metadata.imageSha256));
  enable('restore-boot', writesEnabled && edl && root?.metadata.backupSha256 &&
    ['backed-up', 'verified', 'failed'].includes(root.phase) && $('restore-risk').checked &&
    $('restore-confirmation').value === confirmationFor(root.metadata.backupSha256, true));
  for (const input of document.querySelectorAll('input')) input.disabled = busy;
  for (const id of ['stock-file', 'magisk-file', 'programmer-file']) $(id).disabled = busy || !!root;
}
async function run(label, fn) {
  if (busy) return;
  busy = true; update(); $('operation-status').textContent = label; progress(0);
  try { await fn(); $('operation-status').textContent = label + ' — completed.'; }
  catch (error) {
    log('ERROR: ' + error.message);
    $('operation-status').textContent = 'Stopped: ' + error.message;
    if (root?.phase === 'failed') log('No automatic reboot. Preserve your recovery files; stay in EDL after a write failure.');
  } finally { busy = false; update(); }
}
function bind(id, label, fn) { $(id).addEventListener('click', () => run(label, fn)); }
async function bytes(id, max) {
  const selected = file(id); check(selected && selected.size <= max, 'Missing/oversized file: ' + id);
  return new Uint8Array(await selected.arrayBuffer());
}
function download(name, data, type = 'application/octet-stream') {
  const url = URL.createObjectURL(new Blob([data], { type })), link = document.createElement('a');
  link.href = url; link.download = name; link.click(); setTimeout(() => URL.revokeObjectURL(url), 60000);
}
function showRootMetadata() {
  if (root.metadata.imageSha256) $('flash-phrase').textContent = 'Flash phrase: ' + confirmationFor(root.metadata.imageSha256);
  if (root.metadata.backupSha256) $('restore-phrase').textContent = 'Restore phrase: ' + confirmationFor(root.metadata.backupSha256, true);
}

bind('connect', 'Connecting and checking M3X', async () => {
  root = undefined; programmer = undefined; context = undefined; diagnosticContext = undefined; diagnosticBackup = undefined;
  for (const checkbox of document.querySelectorAll('input[type=checkbox]')) checkbox.checked = false;
  log('Authorize ADB on the player. Browser key stays in memory, not localStorage/IndexedDB.');
  adb = await BrowserAdb.request(log);
  try { context = await adb.preflight(); }
  catch (error) { await adb.close(); adb = undefined; context = undefined; throw error; }
  $('device-status').textContent = `${context.model}, firmware ${context.firmware}, battery ${context.battery}%. ` +
    (context.rooted ? 'Root available. Use Rockbox installation path.' : 'Root unavailable/denied. Root preparation requires an actually unrooted player.');
  const connected = adb;
  connected.adb.disconnected.then(() => {
    if (adb === connected) { connected.credentials.forget(); adb = undefined; $('device-status').textContent = 'ADB disconnected; reconnect explicitly.'; update(); }
  }).catch(error => log('ADB disconnect: ' + error.message));
});
bind('disconnect', 'Disconnecting USB', async () => {
  if (adb) { const closing = adb; adb = undefined; await closing.close(); }
  if (edl) { await edl.close(); edl = undefined; }
  context = undefined;
  // A root session belongs to one live USB connection. Never re-use write
  // authorization after disconnect/reconnect; native recovery remains available.
  root = undefined; programmer = undefined; diagnosticContext = undefined; diagnosticBackup = undefined;
  for (const checkbox of document.querySelectorAll('input[type=checkbox]')) checkbox.checked = false;
  $('device-status').textContent = 'Disconnected. Browser ADB key forgotten.';
});
bind('check-install', 'Verifying installer and checking installation', async () => {
  const bundle = await installerBundle(await bytes('installer-file', 32 * 1024 * 1024));
  await adb.runInstaller(bundle, 'check');
});
bind('install', 'Installing Rockbox; keep tab/cable connected', async () => {
  check($('install-consent').checked, 'Installation consent required');
  const bundle = await installerBundle(await bytes('installer-file', 32 * 1024 * 1024));
  await adb.runInstaller(bundle, 'install');
  log('Keep printed Backup path. No boot autostart enabled. Open Rockbox in Android; begin at low volume.');
});
bind('rollback', 'Restoring Rockbox installation snapshot', async () => {
  const name = $('rollback-name').value.trim();
  check(window.confirm(`Restore Rockbox snapshot ${name}? Settings revert to that snapshot; older loader/security limits may return.`), 'Rollback cancelled');
  await adb.rollback(name);
});
bind('prepare-root', 'Preparing Magisk + audio-fix image; no partition write', async () => {
  check($('prepare-consent').checked, 'Preparation consent required');
  const stock = await bytes('stock-file', PROFILE.stockSize);
  programmer = await bytes('programmer-file', PROFILE.programmerSize);
  check(programmer.length === PROFILE.programmerSize && await sha256(programmer) === PROFILE.programmerSha256,
    'Incorrect Firehose programmer');
  const apk = await bytes('magisk-file', PROFILE.magiskSize);
  const prepared = await adb.prepareMagisk(stock, apk);
  const workflow = new RootWorkflow({ context: prepared.context, writesEnabled, log });
  await workflow.prepare(stock, prepared.magisk); root = workflow; showRootMetadata();
  log('Prepared candidate, not a proven bootable image. SHA-256: ' + root.metadata.imageSha256);
});
bind('save-image', 'Saving prepared boot image', async () => {
  download('m3x-1.75-magisk-gpio18-boot-full.bin', root.imageBytes());
});
bind('enter-edl', 'Explicit reboot into Qualcomm EDL', async () => {
  check($('edl-consent').checked && root?.phase === 'prepared', 'EDL consent/prepared image required');
  await adb.enterEdl(true);
  log('Select Connect EDL next. Device must enumerate as 05c6:9008. ADB authorization alone does not grant EDL permission.');
});
bind('connect-edl', 'Checking EDL identity, GPT and full stock boot', async () => {
  check($('edl-consent').checked && root?.phase === 'prepared', 'EDL consent/prepared image required');
  if (adb) { const closing = adb; adb = undefined; await closing.close(); }
  edl = await EdlUsbTransport.request();
  const stream = new ByteStream(edl);
  await loadProgrammer(stream, programmer, root.context.serial);
  root.firehose = new Firehose(stream, log);
  await root.inspect(progress); showRootMetadata();
  log('Full stock boot verified. Save backup and reopen it before any flash.');
});
bind('diagnostic-reboot', 'Explicit reboot for read-only EDL diagnostics', async () => {
  check(!root && $('edl-consent').checked, 'Separate read-only EDL consent required');
  const candidate = await bytes('programmer-file', PROFILE.programmerSize);
  check(candidate.length === PROFILE.programmerSize && await sha256(candidate) === PROFILE.programmerSha256,
    'Incorrect Firehose programmer; reboot refused');
  diagnosticContext = await adb.preflight();
  await adb.enterEdl(true);
});
bind('diagnostic-read', 'Read-only EDL identity / GPT / complete boot backup', async () => {
  check(!root && $('edl-consent').checked && (diagnosticContext || context), 'Checked ADB identity required');
  diagnosticContext ??= context;
  const candidate = await bytes('programmer-file', PROFILE.programmerSize);
  check(candidate.length === PROFILE.programmerSize && await sha256(candidate) === PROFILE.programmerSha256,
    'Incorrect Firehose programmer');
  if (adb) { const closing = adb; adb = undefined; await closing.close(); }
  edl = await EdlUsbTransport.request(); const stream = new ByteStream(edl);
  await loadProgrammer(stream, candidate, diagnosticContext.serial);
  const hose = new Firehose(stream, log); await hose.configure();
  const table = parseGpt(await hose.read(0, 34));
  const backup = await hose.read(PROFILE.bootStart, PROFILE.bootSectors, progress);
  diagnosticBackup = { bytes: backup, metadata: { phase: 'read-only-diagnostic', model: PROFILE.model,
    firmware: PROFILE.firmware, serial: diagnosticContext.serial, diskGuid: table.diskGuid,
    bootStart: PROFILE.bootStart, bootSectors: PROFILE.bootSectors, backupSha256: await sha256(backup) } };
  log('Read-only boot backup SHA-256: ' + diagnosticBackup.metadata.backupSha256);
  log('No root candidate or write authorization created. Save backup/recovery record, disconnect and power-cycle explicitly.');
});
bind('save-backup', 'Saving full boot recovery backup', async () => {
  const metadata = root?.metadata.backupSha256 ? root.metadata : diagnosticBackup.metadata;
  download(root?.metadata.backupSha256 ? 'm3x-stock-boot-backup.bin' : 'm3x-current-boot-backup.bin',
    root?.metadata.backupSha256 ? root.backupBytes() : diagnosticBackup.bytes);
  download('m3x-boot-recovery.json', JSON.stringify({ version: 1, ...metadata }, null, 2), 'application/json');
  log('Save BOTH downloads. Recovery record contains device identity; keep it private. Reopen saved boot file below.');
});
bind('verify-backup', 'Verifying saved/reopened boot backup', async () => {
  await root.verifySavedBackup(await bytes('backup-file', PROFILE.bootSectors * 512)); showRootMetadata();
});
bind('flash', 'Writing boot only, then verifying full readback', async () => {
  await root.flash({ phrase: $('flash-confirmation').value, riskAccepted: $('flash-risk').checked }, progress);
  log('BOOT READBACK VERIFIED. This does not yet prove Android boots, root works or audio fix works. No automatic reboot.');
  log('Disconnect explicitly, power-cycle into Android, open Magisk/finish setup, reconnect and verify root.');
});
bind('restore-boot', 'Restoring original boot only, then verifying full readback', async () => {
  await root.restore({ phrase: $('restore-confirmation').value, riskAccepted: $('restore-risk').checked }, progress);
  log('ORIGINAL BOOT READBACK VERIFIED. No automatic reboot; disconnect and power-cycle explicitly.');
});
for (const input of document.querySelectorAll('input')) input.addEventListener('input', update);
window.addEventListener('beforeunload', event => {
  if (busy || edl) { event.preventDefault(); event.returnValue = ''; }
});
update();
