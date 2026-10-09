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
regeneration tooling are included. Clock text falls back to "Rockbox" when the
target has no RTC support. Existing Rockbox lists and database structure remain.

Simulator verification exercises menu taps, a 25-song list and swipe, decoded
MP3 audio, pause/resume and Home. Seven 768×1280 captures were inspected, the
existing simulator playback check also passed, and all 24 host tests passed.
The ARM64 build and 56-module package check passed. Skinned rows now honor
backdrop drawing, touch hit-testing and their configured scrolling height;
these fixes leave the built-in list path available for other themes.
The theme was installed with a backup and the user's -60 dB setting preserved.
A real-device framebuffer capture confirms the menu layout and blue colors;
human confirmation of the new playback touch controls is pending. This menu
run completed with CPU sensor0 at 40–41 C and battery 30.7–31.5 C; Android
restoration was verified. The observer recorded no playback in this run.

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
