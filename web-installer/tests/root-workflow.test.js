import { test, mock } from 'node:test';
import assert from 'node:assert/strict';
import { PROFILE, confirmationFor, sha256 } from '../src/profile.js';
import { context, gpt } from './helpers.js';

// Isolate orchestration from authentic-image validation. Separate tests validate
// real private image fixtures when available. No production trust bypass added.
const imageStubs = {
  applyAudioFix: async (stock, image) => image,
  paddedBoot: image => image,
  validateStockPartition: async (backup, stock) => {
    assert.equal(backup.length, PROFILE.bootSectors * 512);
    assert.equal(await sha256(backup), await sha256(stock));
  },
};
mock.module('../src/images.js', Number(process.versions.node.split('.')[0]) >= 26
  ? { exports: imageStubs } : { namedExports: imageStubs });
const { RootWorkflow } = await import('../src/root-workflow.js');
const size = PROFILE.bootSectors * 512;
const stock = new Uint8Array(size), image = new Uint8Array(size); image[0] = 1;
async function fixture(writesEnabled = true) {
  const hose = {
    boot: stock, reads: [], writes: [], failWrite: false, corruptReadback: false, table: gpt(),
    async configure() {},
    async read(start, sectors) {
      this.reads.push([start, sectors]);
      return start === 0 ? this.table.slice() : this.boot;
    },
    async writeBoot(bytes) {
      this.writes.push(bytes);
      if (this.failWrite) throw new Error('Cable lost during write');
      this.boot = bytes;
      if (this.corruptReadback) { this.boot = bytes.slice(); this.boot[0] ^= 1; }
    },
  };
  const root = new RootWorkflow({ context: context(), firehose: hose, writesEnabled });
  await root.prepare(stock, image); await root.inspect(); return { root, hose };
}
const flashArgs = root => ({ phrase: confirmationFor(root.metadata.imageSha256), riskAccepted: true });
const restoreArgs = root => ({ phrase: confirmationFor(root.metadata.backupSha256, true), riskAccepted: true });

test('read-only default and already-rooted/unknown devices cannot flash', async () => {
  for (const rooted of [true, undefined]) assert.throws(() => new RootWorkflow({ context: context({ rooted }) }), /skip rooting/);
  const { root, hose } = await fixture(false);
  await root.verifySavedBackup(stock);
  await assert.rejects(root.flash(flashArgs(root)), /writes disabled/); assert.equal(hose.writes.length, 0);
});
test('prepare/inspect/backup cause zero writes; backup must be saved and reopened', async () => {
  const { root, hose } = await fixture(); assert.equal(root.phase, 'inspected'); assert.equal(hose.writes.length, 0);
  await assert.rejects(root.flash(flashArgs(root)), /current root state/);
  assert.equal(hose.writes.length, 0);
  const wrong = stock.slice(); wrong[0] = 99;
  await assert.rejects(root.verifySavedBackup(wrong), /does not match/); assert.equal(root.phase, 'inspected');
  await root.verifySavedBackup(stock); assert.equal(root.phase, 'backed-up');
});
test('wrong/stale/action-confused confirmations perform no write and retain session', async () => {
  const { root, hose } = await fixture(); await root.verifySavedBackup(stock);
  for (const args of [{ phrase: 'FLASH', riskAccepted: true }, { phrase: confirmationFor('b'.repeat(64)), riskAccepted: true },
    { phrase: confirmationFor(root.metadata.imageSha256), riskAccepted: false }, restoreArgs(root)]) {
    await assert.rejects(root.flash(args), /confirmation/);
  }
  assert.equal(hose.writes.length, 0); assert.equal(root.phase, 'backed-up');
});
test('successful flash rechecks device/boot, writes only boot and verifies complete readback', async () => {
  const { root, hose } = await fixture(); await root.verifySavedBackup(stock);
  await root.flash(flashArgs(root)); assert.equal(root.phase, 'verified'); assert.equal(hose.writes.length, 1);
  assert.equal(await sha256(hose.writes[0]), root.metadata.imageSha256);
  assert.deepEqual(hose.reads, [[0, 34], [790528, 131072], [0, 34], [790528, 131072], [790528, 131072]]);
});
test('changed boot after backup refuses any write', async () => {
  const { root, hose } = await fixture(); await root.verifySavedBackup(stock);
  hose.boot = image;
  await assert.rejects(root.flash(flashArgs(root)), /changed after backup/);
  assert.equal(hose.writes.length, 0); assert.equal(root.phase, 'failed');
});
test('changed disk identity refuses flash and restore', async () => {
  const { root, hose } = await fixture(); await root.verifySavedBackup(stock);
  const { fixGpt } = await import('./helpers.js'); hose.table[512 + 56] ^= 1; fixGpt(hose.table);
  await assert.rejects(root.flash(flashArgs(root)), /identity changed/);
  await assert.rejects(root.restore(restoreArgs(root)), /differs from backup/);
  assert.equal(hose.writes.length, 0);
});
test('cable loss retains backup, does not reboot/retry, and separate restore verifies original', async () => {
  const { root, hose } = await fixture(); await root.verifySavedBackup(stock); hose.failWrite = true;
  await assert.rejects(root.flash(flashArgs(root)), /Cable lost/); assert.equal(root.phase, 'failed');
  assert.equal(hose.writes.length, 1); assert.equal(await sha256(root.backupBytes()), root.metadata.backupSha256);
  await assert.rejects(root.restore(flashArgs(root)), /Restore requires/);
  hose.failWrite = false; await root.restore(restoreArgs(root)); assert.equal(root.phase, 'restored');
  assert.equal(await sha256(hose.boot), await sha256(stock));
});
test('readback mismatch is failure, never reported as successful root', async () => {
  const { root, hose } = await fixture(); await root.verifySavedBackup(stock); hose.corruptReadback = true;
  await assert.rejects(root.flash(flashArgs(root)), /readback mismatch/); assert.equal(root.phase, 'failed');
  assert(root.backupBytes().length === size);
});
test('concurrent operations cannot reuse flash authorization', async () => {
  const { root, hose } = await fixture(); await root.verifySavedBackup(stock);
  let release; const original = hose.read.bind(hose);
  hose.read = async (...args) => { await new Promise(resolve => { release = resolve; }); hose.read = original; return original(...args); };
  const first = root.flash(flashArgs(root));
  await assert.rejects(root.flash(flashArgs(root)), /current root state/);
  release(); await first; assert.equal(hose.writes.length, 1);
});
test('exported image/backup copies cannot mutate data authorized for writing', async () => {
  const { root } = await fixture();
  const before = root.metadata; const exported = root.imageBytes(); exported[0] ^= 2;
  const backup = root.backupBytes(); backup[0] ^= 2;
  assert.equal(await sha256(root.imageBytes()), before.imageSha256);
  assert.equal(await sha256(root.backupBytes()), before.backupSha256);
});
