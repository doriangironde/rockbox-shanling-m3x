// SPDX-License-Identifier: GPL-2.0-or-later
import { PROFILE, checkIdentity, confirmationFor, requireCondition as check, sha256 } from './profile.js';
import { parseGpt } from './gpt.js';
import { applyAudioFix, paddedBoot, validateStockPartition } from './images.js';

// Transport never writes a partition during prepare/inspect/backup. Destructive
// calls require a saved/reopened backup, typed image-bound phrase and explicit
// build-time opt-in. No unlock, erase, repartition, fastboot or automatic reboot.
export class RootWorkflow {
  #phase = 'new'; #busy = false; #image; #stock; #backup; #backupDigest; #imageDigest; #gpt;
  constructor({ context, firehose, writesEnabled = false, log = () => {} }) {
    this.context = checkIdentity(context);
    check(this.context.rooted === false, 'Already rooted or root status unknown: skip rooting');
    this.firehose = firehose; this.writesEnabled = writesEnabled; this.log = log;
  }
  get phase() { return this.#phase; }
  get metadata() {
    return Object.freeze({ phase: this.#phase, model: PROFILE.model, firmware: PROFILE.firmware,
      serial: this.context.serial, diskGuid: this.#gpt?.diskGuid,
      backupSha256: this.#backupDigest, imageSha256: this.#imageDigest,
      bootStart: PROFILE.bootStart, bootSectors: PROFILE.bootSectors });
  }
  async #operation(allowed, fn, fatal = true) {
    check(!this.#busy && allowed.includes(this.#phase), 'Operation unavailable in current root state');
    this.#busy = true;
    try { return await fn(); }
    catch (error) { if (fatal) this.#phase = 'failed'; throw error; }
    finally { this.#busy = false; }
  }
  async prepare(stock, magisk) {
    return this.#operation(['new'], async () => {
      this.#stock = stock.slice();
      this.#image = paddedBoot(await applyAudioFix(this.#stock, magisk.slice()));
      this.#imageDigest = await sha256(this.#image); this.#phase = 'prepared';
      return this.metadata;
    });
  }
  async inspect(progress) {
    return this.#operation(['prepared'], async () => {
      await this.firehose.configure();
      const gptBytes = await this.firehose.read(0, 34);
      this.#gpt = parseGpt(gptBytes);
      // Do not infer firmware from GPT or USB ID: require exact current stock
      // bytes across the entire boot partition, including its zero-filled tail.
      this.#backup = await this.firehose.read(PROFILE.bootStart, PROFILE.bootSectors, progress);
      await validateStockPartition(this.#backup, this.#stock);
      this.#backupDigest = await sha256(this.#backup); this.#phase = 'inspected';
      return this.metadata;
    });
  }
  imageBytes() {
    check(this.#imageDigest && !this.#busy, 'No completed prepared image available'); return this.#image.slice();
  }
  backupBytes() {
    check(['inspected', 'backed-up', 'verified', 'failed'].includes(this.#phase) && this.#backupDigest,
      'No verified full boot backup available'); return this.#backup.slice();
  }
  async verifySavedBackup(bytes) {
    return this.#operation(['inspected', 'backed-up'], async () => {
      check(bytes.length === PROFILE.bootSectors * 512 && await sha256(bytes) === this.#backupDigest,
        'Saved/reopened backup does not match the actual device read');
      this.#phase = 'backed-up'; return this.metadata;
    }, false);
  }
  async #checkTargetUnchanged() {
    const table = parseGpt(await this.firehose.read(0, 34));
    check(table.diskGuid === this.#gpt.diskGuid, 'Device partition identity changed');
    const current = await this.firehose.read(PROFILE.bootStart, PROFILE.bootSectors);
    check(await sha256(current) === this.#backupDigest, 'Boot partition changed after backup; flash refused');
  }
  async flash({ phrase, riskAccepted }, progress) {
    // Invalid UI confirmation must not poison a sound inspected/backup session.
    check(this.writesEnabled, 'Browser partition writes disabled: hardware validation required');
    check(riskAccepted === true && phrase === confirmationFor(this.#imageDigest), 'Flash confirmation does not match image');
    return this.#operation(['backed-up'], async () => {
      await this.#checkTargetUnchanged();
      this.#phase = 'writing'; await this.firehose.writeBoot(this.#image, progress);
      const readback = await this.firehose.read(PROFILE.bootStart, PROFILE.bootSectors, progress);
      check(await sha256(readback) === this.#imageDigest, 'Boot readback mismatch. Stay in EDL; restore saved boot backup');
      this.#phase = 'verified'; return this.metadata;
    });
  }
  async restore({ phrase, riskAccepted }, progress) {
    check(this.writesEnabled, 'Browser partition writes disabled');
    check(this.#backupDigest && riskAccepted === true && phrase === confirmationFor(this.#backupDigest, true),
      'Restore requires the original verified backup and its own typed confirmation');
    return this.#operation(['backed-up', 'verified', 'failed'], async () => {
      const table = parseGpt(await this.firehose.read(0, 34));
      check(table.diskGuid === this.#gpt.diskGuid, 'Restore target differs from backup device');
      this.#phase = 'restoring'; await this.firehose.writeBoot(this.#backup, progress);
      const readback = await this.firehose.read(PROFILE.bootStart, PROFILE.bootSectors, progress);
      check(await sha256(readback) === this.#backupDigest, 'Restore readback failed; do not reboot');
      this.#phase = 'restored'; return this.metadata;
    });
  }
}
