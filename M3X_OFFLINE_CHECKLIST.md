# M3X offline development pass — 2026-10-07

Everything in this pass was performed locally. No M3X connection, installation,
boot-image flash or audible playback occurred.

| Work possible offline | Result |
|---|---|
| Native ARM64 build and toolchain configuration | Built; reconfiguration now reuses the existing ARM64 toolchain. |
| Memory allocation audit | Reduced the hosted audio reservation from 2047 MiB to 127 MiB. Total build-reported RAM use is about 136 MiB. |
| Codec/plugin loading contracts | All 40 codecs and 16 plugins retain public loader headers and private API pointers/helpers; checked ARM64 PIE, non-executable stacks and absence of text relocations. |
| PCM format and sample calculations | Initially matched software output to S32_LE offline; device PREPARE testing later proved that stream unusable. Corrected to S16_LE and verified clean 3.5 mm output. Channel order, attenuation, mute and frame sizes remain covered by sanitizer replays. |
| Volume and balance | Replaced the incompatible 0–100 hardware mapping with centibel software attenuation and independent channels. Hardware stays at the previously captured OEM reference; startup stays muted until playback. |
| DAC filters | Exposed all six existing driver filters with valid settings and labels; tested the control names and invalid selections. |
| Playback synchronization | Serialized writes, stop, close and sample-rate changes; tested nested callbacks and a rate change during a blocked mock write. |
| Input and fuel-gauge replay | Replayed real target/shared input code and battery reader on the host. Fixed disappearing key taps during touch-frame processing. Existing multitouch, overflow, lock and gauge checks pass. |
| Backlight behavior | Preserves selected brightness across off/on, keeps changes made while dark, clamps limits and retries failed writes. |
| Kernel interface errors | Rejects malformed numeric sysfs values and reports buffered write/flush failures. |
| Launcher and fatal-error recovery | Added SurfaceFlinger stop confirmation, verified crash status 139 restores Android without respawning, and changed fatal M3X panic handling to exit instead of spinning. |
| Existing boot-image patch | Confirmed only four M3X pin-list properties change, GPIO13/17 remain available, the other 67 boards and compressed kernel are unchanged, and the Magisk ramdisk is preserved. |
| Desktop simulator | Added the M3X screen definition, keyboard map and supported plugin selection; fixed simulator logging and built/installed it locally. |
| End-to-end storage/UI/MP3 decoding | Simulator boots, selects Sneaky Snitch, creates its playlist-control file, loads the MP3 codec and captures nonzero stereo audio to a file. Screenshots show menu and now-playing screens at 768x1280. |
| Repeatable tests and packaging | Host gates, ARM64 probe compilation, ZIP integrity and packaged-module comparisons are scripted. |
| Preparation for hardware investigation | Added a read-only collector for runtime, thermal, mmc1/microSD and Rockbox crash diagnostics. |

That offline pass had 21 host tests. Driver replays run under address and undefined
behavior sanitizers. The simulator uses an isolated temporary filesystem and
file-only audio output. It validates shared Rockbox behavior using macOS
codecs; it cannot establish Android linker or physical DAC behavior.

## Device follow-up — 2026-10-09

The M3X was reconnected and the prepared build installed with the old module,
assets and configuration backed up. The input/battery and real filesystem
probes passed. The user confirmed menus, touch and physical controls respond.
The first native playback attempt was silent: S32_LE opens, but the vendor DSP
rejects PREPARE with ADSP_EFAILED, mapped by the kernel to ENOMEM. A silent
S16_LE probe passed. Matching 16-bit software output and hardware stream then
produced clean sound through 3.5 mm headphones.

The user reported both UI and audio lag on volume changes. A continuous-writer
stress replay reproduced control starvation; an explicit worker/control
handoff fixed it. The user confirmed prompt volume response with clean audio.
The host suite now has 24 passing tests, including prepare/write failure
recovery and bounded control latency. The 56 packaged modules match the build.
Local device evidence is under `validation/device-20261009/` and is not public.

The menu test held CPU sensor0 at 32–33 C. The first successful 16-bit playback
run held it at 37–39 C, with battery 27.0–28.7 C. These are short USB-connected
tests, not long-run playback or unplugged battery validation. The final
handoff run recorded CPU sensor0 at 38–40 C and battery at 28.5–30.0 C. Its
completed trace and a final ADB check confirmed Rockbox stopped and both
zygotes, SurfaceFlinger and thermal-engine were running. Boot autostart stays
disabled. The error handler also copies its message before releasing the PCM
lock, preventing a concurrent handle replacement from invalidating the text;
the nine native-driver replays and ARM64 build passed again after that change.

## Pocket controls and iPod-style theme — 2026-10-09

The user confirmed the bounded pocket-mode test passed: short physical power
turns off the screen and locks touch, touch/gestures do not wake it, physical
music and volume controls remain usable, and short power wakes it normally.
The observer recorded backlight zero while the PCM stream kept advancing.
CPU sensor0 was 39–41 C and battery 29.7–30.5 C during this short USB-connected
run; Android restoration was verified afterward. The CPU reached 48 C during
framework restoration. A three-second power hold is covered by replays, but
actual hardware shutdown and sustained screen-off power use remain untested.

The new `m3x-ipod` native theme has full-screen silver headers, light list rows,
blue selection, dark icons/chevrons and centred artwork with onscreen playback
controls, Back and Home. It has no click wheel. Original bitmap artwork and
regeneration tooling are included. The top bar shows Rockbox branding and
battery level. Existing Rockbox lists and database structure remain.

Simulator verification exercises menu taps, a 25-song list and swipe, decoded
MP3 audio, pause/resume and Home. Seven 768×1280 captures were inspected, the
existing simulator playback check also passed, and all 24 host tests passed.
The ARM64 build and 56-module package check passed. Skinned rows now honor
backdrop drawing, touch hit-testing and their configured scrolling height;
these fixes leave the built-in list path available for other themes.
The theme was installed with a backup and the user's -60 dB setting preserved.
A real-device framebuffer capture confirms the menu layout and blue colors;
a subsequent album test confirmed the correct artwork, metadata, clean sound,
onscreen Next, pause/resume and Home while music continues. The initial menu
run completed with CPU sensor0 at 40–41 C and battery 30.7–31.5 C; Android
restoration was verified. The observer recorded no playback in that menu run.

## Real FLAC album and artwork test — 2026-10-09

A six-track album of 24-bit/48 kHz stereo FLAC sources, with both embedded
600×600 JPEG artwork and a 3000×3000 `cover.jpg`, was copied to the player.
All six tracks and the cover were read back and SHA-256 matched. The user
confirmed correct artwork and track metadata, clean sound, onscreen Next,
pause/resume, and Home returning to the menu without stopping music. A native
framebuffer capture confirms the artwork and metadata on track 3 of 6.
The trace shows advancing PCM at 48 kHz, stereo S16_LE; decoding 24-bit sources
does not imply 24-bit hardware output. The simulator also decoded FLAC and
rendered the artwork successfully. This does not establish uninterrupted
playback of every track or all sample-rate transitions. Album media and device
captures remain private and are not included in this repository.

The bounded album run held CPU sensor0 at 41–42 C and battery at 30.7–31.5 C.
Android recovery was verified afterward; boot autostart remains disabled.

## Artifacts and commands

- ARM64 player: `build-m3x/rockbox`
- Matching assets: `build-m3x/rockbox.zip`
- Review/update bundle: `build-m3x/m3x-offline-bundle.zip` (executable, assets,
  launcher scripts, framebuffer helper, diagnostics and checksums)
- Simulator: `build-m3x-sim/rockboxui`
- Evidence: `validation/offline-20261007-batch/`
- Native verification: `sh tools/m3x/verify-offline.sh`
- Silent simulator verification: `python3 tools/m3x/run-simulator-test.py`

## Device checks still required

1. Longer playback, plugin loading and repeated starts still need testing.
   The previous immediate status-139 crash did not recur in the bounded menu
   and MP3 runs, but its original cause was not traced conclusively.
2. Check physical buttons, encoder behavior, touchscreen edges, gestures and
   software lock against the real evdev devices.
3. Clean 3.5 mm playback and prompt volume changes are confirmed. Balanced
   output, absolute volume calibration, both DACs, balance, filters, sample-rate
   transitions, underruns and long playback still require hardware checks.
4. Measure temperature and battery use during sustained playback and screen
   off/on. Whole-system suspend remains deliberately unimplemented pending
   hardware investigation.
5. Insert a microSD card, collect mmc1 and mount state, then implement and test
   mounting/removal. Its live device identity and filesystem are still unknown.
6. Check charger changes, unplugged battery reporting, reboot and poweroff on
   hardware. Current-draw estimates and the voltage fallback are uncalibrated.

## Sustained playback and rate changes — 2026-10-09

The complete six-track FLAC album (11:13) played in order and ended naturally,
with zero observed tinyalsa underruns, no repeated PCM pointers and no seek/time
discontinuities. CPU sensor0 was 41–45 C, battery 32.2–33.0 C, average Rockbox CPU
4.05% of one core and RSS 137320–137424 KiB. Continuous screen-off playback was
observed for 8:38, then the user confirmed manually unlocking on track 6. Thus
full-album playback passes, while full-album screen-off coverage remains partial.
USB stayed connected; unplugged battery drain and analog dropouts are unmeasured.

A synthetic eight-track M3U8 test passed MP3/FLAC changes with the screen locked,
native output rates 44.1/48/88.2/96/176.4/192 kHz and zero observed underruns.
The 32 kHz MP3 source output at 48 kHz. Output remains stereo S16_LE.

Fixed a launcher refusal bug that could restore Android beneath an existing
Rockbox process. Both early and post-cooling refusal paths pass replays; all 25
host tests and the 56-module package check pass. The fixed launcher was backed
up and installed. A real duplicate launch was refused without stopping playback
or starting Android. Both completed tests restored Android and added no tombstones.
The native binary and assets were not changed.

External mmc1 is Qualcomm sdhc2 at 7864900.sdhci with driver sdhci_msm; no card was
detected, so mounting/removal remains untested. Retained older linker SIGABRT
records do not explain the original status-139 crash. Boot autostart remains
disabled, original album playlist restored and volume -60 dB preserved.

Repeatable tools: tools/m3x/build-playback-observer.py, observe-playback.c,
playback-soak.sh, summarize-playback-soak.py and make-format-fixtures.py.
Private report: validation/soak-20261009/REPORT.md. Full raw album/format traces
are in validation/soak-20261009/device-full/ and
validation/format-20261009/device-final/. Remaining normal launch/exit, battery,
shuffle/repeat, plugins, calibration, power, suspend, card and installer checks
are not implied by these passes.

## No-ADB launch and return — 2026-10-09

Added a small Android **Rockbox** launch app and a native main-menu **Return to
Android** action. The app starts a detached, one-time root session through
m3x-module/launch.sh; it keeps the real boot module disabled and reuses the normal
thermal-protected launcher. There is no session duration timer. Startup normally
takes about 20 seconds, including the delay for Magisk logging to finish.

The return action uses normal Rockbox shutdown cleanup to save state and close
audio, then exits status 0 instead of calling hardware power-off. The launcher
restores Android. The Power button retains screen-lock and shutdown behavior.

Two round trips passed on the device, including a direct Android home-icon tap,
FLAC playback, Return to Android while playing, saved-position resume on the next
launch, closed PCM and usable Android home afterward. Root access is granted in
Magisk and the icon was added beside Poweramp on the player's home screen.
All 30 host tests and 56 packaged-module checks passed. Physical shutdown remains
unverified and is distinct from the new return action.

Build/user instructions: android-launcher/README.md. APK:
build-m3x-launcher/Rockbox-M3X.apk. Keep the private local signing key for updates.
Device rollback files: /data/local/tmp/m3x-before-manual-launch-20261009/.
Local rollback files: backups/manual-launch-20261009/.
Private verification evidence: validation/manual-launch-20261009/.

A subsequent normal reboot verified Android as the default, with Rockbox stopped,
PCM closed, boot autostart disabled and the app still installed.

## microSD basic access — 2026-10-10

An inserted 128 GB SD card was detected on mmc1 and, at the user's request,
formatted through Android as public FAT32 storage. Android mounts it at
`/mnt/media_rw/external_sd1`; the development installation exposes a symlink
from `/data/media/0/microSD`, shown as **Files → microSD** in native Rockbox.
The card passed write/read hash checks, MP3/FLAC playback at 44.1/48 kHz with
zero observed underruns, and Android unmount/remount with unchanged file hashes.
The raw mount remained readable while Android app runtimes were stopped.
Physical removal during playback and reboot persistence are still untested.
Return to Android and eject before removing the card. Installer integration
remains pending. Private evidence: validation/microsd-20261010/REPORT.md.

## Standalone installation/update/rollback — 2026-10-10

Added a checksummed standalone installer for the already-rooted M3X firmware
1.75/API 25 setup with its existing I2C5 audio fix. It includes the native binary,
matching assets, Android launch APK, framebuffer helper and manual-session
scripts. Android remains the default boot. Updates preserve settings and music,
keep/create the microSD link, and retain a private snapshot for rollback.
Host failure-injection checks cover corrupted bundles, blocked launches, failed
APK installation and partial copy recovery. A real device update and explicit
rollback passed with unchanged binary/settings/resume/card-file hashes.
Packaging emits deterministic installer and corresponding-source ZIPs, with
fixed ZIP metadata and source/binary checksums. This is a development installer
for the tested prerequisite setup, not a firmware image or Magisk-flashable ZIP.
See tools/m3x/INSTALL.md. Private evidence: validation/installer-20261010/.
