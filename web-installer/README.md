# Experimental M3X WebUSB installer / rooting prototype

**Development prototype, not a hardware-validated one-click rooting service.**
Default builds cannot write boot partitions. No connected player was rebooted,
rooted or flashed during development. The existing installed Rockbox payload and
published `dev-bf6af2444b56` release are unchanged.

## Run locally

Use Node 22.18+ or Node 24/26, and desktop Chromium (Chrome/Edge):

```sh
cd web-installer
npm ci --ignore-scripts
npm run dev
```

Open the printed localhost URL. Production builds use `npm run build`; serve
`dist/` over HTTPS. `npm run preview` serves a local production preview.
WebUSB does not work in Safari/Firefox, non-secure remote HTTP or arbitrary
embedded frames. Windows needs a compatible USB driver; Linux may need USB
permissions. Do not replace drivers blindly; use the native installer/QDL fallback
when WebUSB cannot claim an interface. Browser behavior on those OSes is untested.

## Published web installer

[Open the GitHub Pages installer](https://doriangironde.github.io/rockbox-shanling-m3x/).
The page follows a GrapheneOS-style, single-column installation guide with section
anchors, inline controls, system light/dark colors and keyboard navigation. Its
styling and audio-wave icon are original; no GrapheneOS branding or assets are used.

`.github/workflows/pages.yml` installs the pinned dependencies, runs the unit suite,
builds `web-installer/dist/` and deploys only that directory on relevant pushes to
`main` or a manual workflow dispatch. Enable Pages with **GitHub Actions** as its
publishing source. Relative Vite asset paths support the repository subdirectory.
The workflow does not enable `VITE_M3X_ENABLE_EDL_WRITES`: public boot writes remain
disabled. Firmware, private keys, local fixtures and recovery backups are not
published.

GitHub Pages ignores `public/_headers`; the HTML CSP still applies, but custom
response headers such as `frame-ancestors` and `Permissions-Policy` require a host
that supports them. Project Pages share `doriangironde.github.io` with other sites
under that account; all content on that origin must be trusted.


## Existing-root installation

- Connect in Android; accept USB-debugging authorization and any Magisk ADB root
  prompt. Stop desktop ADB if it owns the USB interface.
- Preflight checks exact Shanling M3X, 1.75/API 25, ARM64, Android active,
  Rockbox stopped and battery >=50%.
- Select the **attached installer ZIP** from release `dev-bf6af2444b56`, not
  GitHub's automatic source ZIP. Full archive SHA-256 is pinned in `src/profile.js`;
  internal checksums, complete file inventory and manifest are also checked.
- Run check, then explicitly consent to install. ADB uploads the verified bundle
  and invokes its **existing transactional `install.sh`**, not a JS rewrite.
- Keep the reported rollback snapshot path. Snapshot rollback is exposed separately
  and requires confirmation. Incomplete/disconnected install staging is retained,
  never removed from under a potentially running root process.

Browser ADB interoperability and real device installation are **not yet tested**.
Unit/browser tests use synthetic USB/ADB adapters and the actual published bundle.
The CLI installer remains the hardware-tested fallback.

## Read-only EDL diagnostics on an already-rooted player

A separate path permits transport validation without trying to re-root the working
baseline. Connect/check in Android, select the verified programmer, acknowledge
EDL consent and click **Reboot to EDL for read-only diagnostics**. Then click
**Connect EDL + read-only GPT / boot backup**. This still reboots the player and
executes the verified programmer in RAM, so obtain explicit permission first.

It binds the Sahara serial, checks programmer/eMMC identity and GPT CRC/layout,
and reads the complete current boot partition. It creates **no root candidate,
no write authorization and no partition writes**. Stock matching is not required
for a read-only backup of an already-rooted image. Save
`m3x-current-boot-backup.bin` with its private recovery JSON; disconnect and
power-cycle explicitly. Use the recorded full SHA-256 if native recovery later
needs this same-device backup. This browser diagnostic path is also untested on
hardware; do not confuse host replay with real USB validation.

## Root preparation and EDL flow

M3X's LK fastboot supports queries but cannot flash/boot/reboot/continue. Do not
use generic `fastboot flashing unlock`, `fastboot flash` or factory-reset recipes.
The recorded working native path uses **Sahara + a Qualcomm Firehose programmer
in EDL**, writes only `boot`, then boots Magisk plus the GPIO18/19 audio fix.

The browser implementation mirrors that device-specific path:

1. Start on an **actually unrooted stock M3X 1.75**. A root denial is not proof of
   being unrooted: if Magisk is already present, grant ADB root and skip rooting.
2. Supply stock `boot.img` extracted from your firmware, the official
   [Magisk-v30.7.apk](https://github.com/topjohnwu/Magisk/releases/tag/v30.7), and
   the exact verified `MSM8937.mbn` programmer. Files stay in the browser/device;
   no upload server exists. Hashes and sizes are pinned.
3. Consent to temporary file patching and installation of the official manager.
   Extract Magisk's ARM64 patch tools from the authenticated APK, run official
   `boot_patch.sh` as **unprivileged ADB shell** against a staged regular file,
   and read `new-boot.img` back. This invocation is **untested on an unrooted M3X**;
   Magisk/SELinux incompatibility must fail, not fall back to a guessed image.
4. Verify the patched image retains the exact stock kernel and expected header
   fields and contains a Magisk ramdisk. Modify only the four conflicting
   `tlmm_gpio_key` pin lists in M3X DTB #26. FDT_NOP fills freed words, preserving
   bank/header offsets and other properties. This is semantically equivalent to
   the existing dtc-based fix, but its new output has not been boot-tested.
5. Explicitly consent/click to reboot into EDL; choose EDL USB permission again.
   The SHA-256-pinned programmer is executed in device RAM, not installed to disk.
   Sahara chip serial must equal the checked ADB serial. Missing/mismatched
   identity refuses the operation; no USB-ID-only device identification.
6. Configure verified MSM8937/eMMC, validate primary GPT CRCs and the tested
   partition map, then read the **entire 64 MiB boot partition**. Require exact
   stock 1.75 bytes plus zero padding. Different firmware, previously patched
   images, unknown tails or geometry changes are refused.
7. Save the full boot backup and recovery JSON locally. Reopen the saved boot
   file and verify its full SHA-256. Downloads alone are not counted as a saved
   backup. Keep recovery metadata private; it contains device identity.
8. In a developer write-enabled build only: acknowledge risks and type the
   displayed action/image-bound phrase. Recheck GPT identity and current boot
   hash, write **boot only**, then read the complete partition back and compare.
   No erase, GPT/system/recovery/userdata write, unlock or automatic reboot exists.
9. On failure, remain in EDL and recover. In-session restore requires a separate
   typed phrase and readback. After verified completion, explicitly disconnect,
   power-cycle, finish Magisk setup, reconnect and verify root before Rockbox.
   Readback success alone does not establish bootability, root or audible audio.

### Safety / developer gating

Rooting changes Android's trust model and can prevent booting or require EDL
recovery. The recorded legacy Magisk profile sets `KEEPVERITY=false` and
`KEEPFORCEENCRYPT=false`; verified-boot/force-encryption policy may be weakened.
No erase command is issued, but that is **not a guarantee against data loss**.
Back up music/settings separately and have stock firmware/native recovery ready.
A boot backup does not back up userdata or all other partitions.

Partition writes are disabled in normal builds. For controlled development only:

```sh
VITE_M3X_ENABLE_EDL_WRITES=1 npm run dev
# or VITE_M3X_ENABLE_EDL_WRITES=1 npm run build
```

This is an intentional development opt-in, **not a security boundary against
modified JavaScript**, and does not make the prototype hardware-validated. Do not
use a working player to prove this path merely because host tests pass. First
validate browser ADB and non-root file patching, then an explicitly authorized
read-only EDL session/identity/GPT/backup. A boot test and recovery test require a
separate explicit decision, a saved backup and a prepared recovery environment.
**Physical EDL re-entry without a booting Android system is not yet validated.**
The recorded successful `adb reboot edl` requires Android to boot. Do not enable
boot writes until a no-Android recovery-entry method is established for the M3X;
working QDL reads/writes alone do not establish recovery from a non-booting player.

### Native recovery if the tab/device connection is lost

Do not rely on the same browser tab surviving a flash. Standalone browser recovery
across a closed tab is **not implemented**; the private recovery JSON is a record,
not an automatic recovery authorization. Keep native
[linux-msm/qdl](https://github.com/linux-msm/qdl) and the exact programmer available
before entering EDL. Re-enter EDL if needed, close/release browser USB and connect
only the player matching your saved recovery record.

The following **writes the boot partition**. Use only the verified backup from
that same M3X 1.75, not another device's image. `--skip-reset` leaves EDL active:

```sh
qdl --skip-reset --storage=emmc MSM8937.mbn write 0/790528+131072 m3x-stock-boot-backup.bin
qdl --skip-reset --storage=emmc MSM8937.mbn read 0/790528+131072 m3x-restored-boot-readback.bin
```

Compare both files' full SHA-256 with `backupSha256` in your recovery JSON before
an explicit power-cycle. On readback mismatch, do not reboot. Native backup/write
mechanism was used during the original port bring-up; these browser recovery file
handoffs and the new browser-generated image have not been hardware-tested.

## Tests and evidence limits

```sh
npm test
npx playwright install chromium
npm run test:browser
npm run build
```

Node tests cover identity, archive/image trust, FDT/GPT parsing, ADB shell/sync
semantics, ephemeral keys, fragmented/coalesced Sahara/Firehose, malformed replies,
unsafe ranges, state/confirmation gates, backup verification, stale target,
partial/cable-loss writes, readback mismatch and separate restore. Synthetic state
replays mock the image validator only in isolated test workers, not production.
Real private stock/Magisk fixtures test equivalence to the hardware-tested audio
patch; unavailable fixtures explicitly skip. Chromium tests use mock ADB (no live
USB), verify pinned actual bundle parsing and fail-closed UI controls.

Protocol framing, chip-serial mapping, USB ZLP/driver behavior, programmer identity
attributes, unprivileged Magisk invocation, image bootability, root/audio and live
recovery still need device/browser validation. Tests do not certify rooting safety.

## Hosting and security

The installer page can execute ADB/root commands. **A compromised origin or build
can compromise the player.** Package hashes pinned in JS do not protect against a
compromised page replacing the verifier. Signed download manifests could protect
package delivery, not malicious installer JavaScript.

- Dedicated trusted HTTPS origin; bundle dependencies locally, pin lockfile.
- No CDN JS, analytics, service worker, remote command channel or runtime package
  discovery. Downloads selected locally avoid relying on GitHub release CORS.
- Strict CSP; serve `X-Content-Type-Options: nosniff`,
  `Content-Security-Policy: ...; frame-ancestors 'none'`, and
  `Permissions-Policy: usb=(self)`. `public/_headers` is an example for hosts that
  support that format; other hosts must apply equivalent response headers.
- ADB keys are generated in memory only; no localStorage/IndexedDB/private-key
  persistence. Disconnect/reload requires fresh authorization. After use revoke
  USB debugging authorization and disable debugging on the player.
- Stock firmware, patched images, programmer and Magisk APK are **not bundled**.
  Programmer provenance/redistribution licensing must be resolved before any
  turnkey downloadable root package. See `THIRD_PARTY.md`.
