# M3X validation record

This is a dated history, not a claim that every build has passed device tests.
Later sections supersede earlier pending items. Private evidence now lives in
`local/validation/`; old directory names in archived notes are historical.

## Remaining hardware checks

- Balanced output, both DACs, absolute volume/balance calibration and filters.
- Physical shutdown, whole-system suspend and unplugged battery consumption.
- Broader plugins, shuffle/repeat, and USB switching during plugin-generated audio;
  heavy-plugin thermal behavior after the recorded CPU cutoff.
- Physical microSD removal during playback and microSD reboot persistence.
- Analog dropouts; software progress and underrun counters do not measure them.
- Re-test changed native builds on the device. Host replays and the desktop
  simulator do not establish Android linker or physical DAC behavior.

## Initial offline development pass — 2026-10-07

This initial pass was performed locally: no M3X connection, installation,
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
Local device evidence is under `local/validation/device-20261009/` and is not public.

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
- Evidence: `local/validation/offline-20261007-batch/`
- Native verification: `sh tools/m3x/verify-offline.sh`
- Silent simulator verification: `python3 tools/m3x/run-simulator-test.py`

## Checks outstanding at the initial pass (superseded by later results)

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
Private report: local/validation/soak-20261009/REPORT.md. Full raw album/format traces
are in local/validation/soak-20261009/device-full/ and
local/validation/format-20261009/device-final/. Remaining normal launch/exit, battery,
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
Local rollback files: local/backups/manual-launch-20261009/.
Private verification evidence: local/validation/manual-launch-20261009/.

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
Return to Android and eject before removing the card. The later installer adds
the microSD link. Private evidence: local/validation/microsd-20261010/REPORT.md.

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
See tools/m3x/INSTALL.md. Private evidence: local/validation/installer-20261010/.

## Maintenance and Ponytail review — 2026-10-10

The complete host gate passed 60 tests, including the two private boot-image
preservation checks, and matched all 56 codec/plugin modules. Native ARM64
Rockbox, the simulator and changed device probes built. Actual simulator runs
decoded the licensed MP3, created playlist state and exercised theme scrolling,
pause/resume and Home; screenshots were inspected. All audio was captured to
files, not played through speakers. Before/after replays also demonstrated
graphics, input-race, audio-recovery and diagnostic fixes.

Private recovery/evidence/research files moved under `local/`; public root
documentation is now canonical. Current simulator outputs use
`local/validation/simulator-smoke/` and `local/validation/theme-preview/`, not
hardcoded historical pass directories. Builds and the retained signing key stay
in their original work directories.

Production `fbpan` and launch logs now use root-owned module/session paths.
Installer validation rejects unchecked files, path aliases and symlinks, and
rollback reports launcher-removal failures. The full scope and remaining
shared-storage/plugin-audio limitations are in [the review](docs/PONYTAIL_REVIEW.md).
This maintenance pass made no player connection, installation or firmware flash;
previous device results do not validate these newly changed native builds.

## Maintenance build on hardware — 2026-10-11

Checked on the connected rooted Shanling M3X: firmware 1.75, API 25, existing
I2C5 audio fix. The installed/tested bundle was `dev-a0ee34178f21`; later host
report/documentation changes did not alter these native binaries:

```text
rockbox  463a12b30fed6947cba68f718acce0a88fc6e91c0581b0d64b3158d0aa5642e1
fbpan    49723154df96830f93834a3737a1e189918c4adf5fbdb0424afe3873aa5990cc
```

- **Installer/recovery:** Preflight and update passed. An actual rollback
  restored the prior native/helper/config/playlist hashes. Reinstallation left
  the maintained binary and root-owned mode-755 module helper installed, with
  the original config and playlist hashes, existing microSD link, boot
  autostart disabled and Android active. No firmware or boot image was flashed.
- **Device replays:** ARM64 input/battery validation passed, including a live
  battery read. The filesystem probe exclusively created, wrote/fsynced,
  rotated, reopened and removed its temporary playlist-control file and read
  actual codecs/plugins/config/music. These are not mechanical-button tests.
- **UI and manual session:** Native menu/files/playback framebuffers were
  inspected. Launching the installed Android app started the maintained player;
  tapping its native Return to Android menu exited 0, released the launch lock,
  closed PCM and restored all four Android/thermal services and the home screen.
  A duplicate service launch was refused without disturbing the active player.
- **Mixed formats/rates:** All eight synthetic tracks were observed in order
  near their start and end over 95.15 seconds. MP3 at 32 kHz used 48 kHz output;
  the other source/output pairs matched at 44.1, 48, 88.2, 96, 176.4 and 192 kHz.
  Output was stereo `S16_LE`; the maximum observed underrun counter was zero.
  **The strict report is INCOMPLETE_OR_FAILED, not an uninterrupted-album pass.**
  Samples 76 and 92 caught PCM closed/open during rate changes. The test only
  requested lock at sample 83, after the first two tracks; the native lock did
  not spontaneously release. No gapless-rate-transition claim is made.
- **Continuous playback:** A separate full 136.62-second licensed MP3 without
  an ID3 track number passed the unchanged continuous-playback/lock criteria:
  134.05 seconds observed, 126.21 seconds locked and dark, zero reported
  underruns, no observed elapsed jumps or repeated hardware pointers, and
  automatic Android restoration after natural playlist completion.
- **Report fix and regression:** Physical metadata used `track=-1`; the host
  summarizer previously discarded it. Signed parsing and nonempty-path playback
  detection now handle untagged files without counting cleared end-of-stream
  metadata. The regression failed before the fix; the complete suite then
  passed **61 tests**. No timing, lock or underrun threshold was relaxed.

Unique test music, upload and controller directories were removed from the
player. Private evidence and the previous-installation backup remain under
`local/validation/maintenance-20261011-111937/`; device rollback snapshots remain
under `/data/adb/rockbox-m3x-backups/`. Raw device logs, screenshots, serials and
user music are not published.

Limits: ADB/USB stayed connected and the battery was charging. Touch/Power
events were injected, not human-operated. No analog listening/dropout,
unplugged-battery, balanced-output, physical shutdown, USB-headphone switching,
power-loss, microSD-removal/reboot-persistence or exhaustive plugin test was
performed in this follow-up.

## User-confirmed hardware follow-up — 2026-10-11

The user subsequently confirmed the following checks and observations. These
are human-operated results, separate from the earlier injected-input/ADB runs:

- **Player buttons:** Human-operated checks completed and working.
- **USB headphones:** Switching checked and working; headset model, output rates
  and reconnection count were not specified in this report.
- **Battery:** Normal drain reported; no measured drain rate, test duration or
  charging/discharging setup was supplied.
- **Temperature and stability:** No overheating or other issues reported; no
  temperature trace or timed soak was supplied.

Physical shutdown was explicitly excluded from testing, not reported broken.
No shutdown, reboot, firmware flash or repeat device check was performed for
this documentation update. The user observations complement the measured
results above; they do not change the strict mixed-rate verdict or establish
a quantified battery/thermal operating envelope.

## Private native runtime — 2026-10-11 (host-only)

Moved native codecs/plugins/overlays and bundled Lua support files out of shared
Android storage. The installer builds `/data/adb/modules/rockbox_m3x/runtime/`
only from the verified bundle, normalizes root ownership and permissions, and
backs up the complete old layout. Old shared native modules are retired from
the active installation; shared settings, plugin data, playlists and music stay
in place. Preflight and private staging checks reject shared asset symlinks
before root overlay copies. M3X virtual runtime paths bypass the shared-storage
pivot; simulator paths stay unchanged. Reversi's unused save-path definition now
uses the data-directory macro; this does not add game-saving functionality.

The central native `lc_open` checks the runtime prefix, rejects traversal/empty
components and verifies root-owned, non-group/other-writable directories and a
regular module file with no symlink components before calling `dlopen`. This
also covers overlays and arbitrary plugin API loader calls. It is not a complete
root sandbox: explicitly selected Lua scripts retain the root plugin API, and
shared data parsers and installer authentication are not addressed by this gate.

Host verification:

- Native ARM64 player and simulator rebuilt; matching ZIP contains all 56 native
  codec/plugin modules. Device input/filesystem probes and framebuffer helper
  cross-compiled without deploying them.
- Sanitized replay uses real filesystem/path/loader code with mocked metadata
  and linker calls. Checks private module paths, unchanged shared data paths,
  bounded path buffers and rejection before any untrusted `dlopen` call.
- Installer/archive regressions cover legacy migration, removal of stale native
  code, preservation of saved plugin data, runtime mode normalization, shared
  executable rejection, staged-link injection and rollback/failure restoration.
- Actual simulator smoke decoded MP3 to file (71,680 stereo frames); theme replay
  passed scrolling, pause/resume and Home (92,160 frames, seven screenshots).
  Playback screenshot was inspected. No audible output was used.
- An initial full run while the simulator was rebuilding tripped the existing
  PCM control-latency assertion (`elapsed < 0.1`). The serial rerun passed all 67
  then-current tests with that threshold unchanged (maximum control wait
  73.747 ms). After adding the staged-link regression, the final complete host
  gate passed **68 tests** in 108.263 seconds and matched all 56 packaged modules.
  Logs retain both the initial failure and subsequent passes.

Native SHA-256:

```text
rockbox  d4c54f16521e13f3d321f92e0fc36cb3d252a0dd637fe0a76d421d2911a88608
fbpan    49723154df96830f93834a3737a1e189918c4adf5fbdb0424afe3873aa5990cc
```

No player connection, installation, shutdown or boot-image flash occurred.
Earlier device observations do not validate this new binary/layout. Next device
checks: installer update/rollback; private codec/plugin loading; MP3/FLAC and USB
music switching; plugin launch/save/relaunch; Android return and screen lock.
Plugin-generated audio USB switching remains a separate unresolved issue.
Private host evidence: `local/validation/private-runtime-20261011/`.

## Private runtime on hardware and BootAnimation fix — 2026-10-11

Connected rooted M3X firmware 1.75/API 25 with the existing audio fix. The initial
private-runtime bundle was `dev-eec3f9f1071d`; the subsequent launcher-only fix was
installed/tested as `dev-2371b9ca3dd9`. Native player/helper hashes remain those in
the preceding section. Final production launcher:

```text
service.sh  44684c34ff2a491d2c3e167a5f650b6258445b8a466f2da459c3314452aaeee3
```

- **Update/rollback:** Preflight, update, actual rollback and reinstall passed.
  Rollback restored original native/helper/settings/playlist hashes and shared
  codec layout; reinstall returned to private runtime. The launcher fix was then
  installed with another private snapshot. All 125 runtime code/support files
  matched the bundle, with no symlinks, non-root owners or group/other write bits.
- **Device probes:** Live input/battery replay and real playlist-control
  create/write/fsync/rotate/reopen/removal passed. Actual Android `dlopen` loaded
  private MP3 codec and properties plugin headers. Valid ELF copies in shared
  storage, dot traversal, symlinks and actual non-root/writable fixture files and
  directories were rejected before loading. Only disposable fixture copies were
  modified; installed modules were not altered.
- **MP3/FLAC:** A 136.62-second licensed MP3 was observed from 1.151 to 135.029
  seconds (63 samples, 133.84 seconds of telemetry), with native screen lock and
  dark backlight in 58 samples. A separate 12-second 48 kHz FLAC was observed
  from 1.066 to 11.861 seconds (six samples, 10.75 seconds of telemetry). Each
  interval stayed PCM RUNNING with zero reported underruns, advancing pointers
  and no observed elapsed jumps; output was stereo S16_LE at 44.1/48 kHz.
  Metadata cleared after each track without a stop/seek command. Playback CPU
  was 40–42°C; battery was 30.7–31.2°C. This is targeted sequential testing,
  **not** a new mixed-rate/gapless or whole-album strict-report pass.
- **Plugin data:** Chopper loaded from private runtime, ran a game, saved
  `highscore: 400` through shared `.rockbox/rocks/chopper.cfg`, then unloaded and
  relaunched with the same saved value. Reversi loaded, accepted a move and quit.
  Its save-path macro is unused upstream; no Reversi save/reload claim is made.
- **Heavy-plugin limit:** The sequence reached CPU sensor0 56°C while Boomshine
  was starting (53°C already observed before its launch). The unchanged 55°C
  guard killed Rockbox with status 137 and restored all four Android/thermal
  services; battery was 30.7°C at cutoff. No claim that Boomshine alone caused
  the rise, or that heavy-plugin sustained use passes. No cutoff was raised.
- **Recovery:** A subsequent normal app launch and native Return to Android
  exited 0, closed PCM and released the launch lock. Android home was inspected.
- **BootAnimation race/fix:** The first takeover added one Android BootAnimation
  SIGSEGV tombstone in `SurfaceComposerClient::getDisplayInfo`, not a Rockbox
  crash. The launcher now stops/confirms boot animation before SurfaceFlinger,
  refusing takeover on missing/stuck service state or a remaining process.
  The new regression failed on the previous script; **69 host tests** then
  passed without relaxing existing thresholds. A controlled live-animation
  launch verified `bootanim running` before, `stopped` with no process during
  takeover, and normal native exit 0. Another ordinary fixed-launcher round trip
  also passed. Both restored all services and added no new tombstones.
- **Input evidence limit:** Touch/Power events were injected, not human-operated.
  Repeated same-coordinate injected Return taps were initially ignored in the
  live-animation check; changing coordinates then repeating worked. Human
  first-tap behavior across repeated launches still needs checking.

Original config/playlist hashes and −60 dB volume were restored after tests.
Disposable media/controller/loader fixtures and the test-created Chopper save
were removed; backups and device tombstones were retained. Final state: new
native player/private runtime/launcher installed, boot autostart disabled,
Rockbox stopped, PCM closed, usable Android home and all four services running.
No reboot, physical shutdown or firmware/boot-image flash occurred.

USB/ADB stayed connected, battery full/charging. No human analog listening,
USB-headphone switching, unplugged-battery soak, balanced output/calibration,
physical card removal/reboot persistence or exhaustive plugin check occurred.
Private evidence: `local/validation/private-runtime-device-20261011/`.

## User confirmation after private-runtime update — 2026-10-11

After the private-runtime and BootAnimation launcher updates, the user reported:
**“Everything works, just tested!”** This records a user-operated follow-up on the
installed player, separate from the earlier injected-input tests.

The report did not specify individual steps, USB-headset model/switching count,
listening conditions, test duration or battery/thermal measurements. It supports
normal-use functionality as reported, but does not establish that every pending
specialized check was performed. The measured heavy-plugin cutoff, plugin-audio
USB-switching issue and remaining shutdown/balanced-output/battery/storage checks
stay recorded. No device connection, installation, playback, shutdown or firmware
change was performed for this documentation update; binaries remain unchanged.
