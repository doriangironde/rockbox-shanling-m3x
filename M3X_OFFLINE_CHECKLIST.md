# M3X offline development pass — 2026-10-07

Everything in this pass was performed locally. No M3X connection, installation,
boot-image flash or audible playback occurred.

| Work possible offline | Result |
|---|---|
| Native ARM64 build and toolchain configuration | Built; reconfiguration now reuses the existing ARM64 toolchain. |
| Memory allocation audit | Reduced the hosted audio reservation from 2047 MiB to 127 MiB. Total build-reported RAM use is about 136 MiB. |
| Codec/plugin loading contracts | All 40 codecs and 16 plugins retain public loader headers and private API pointers/helpers; checked ARM64 PIE, non-executable stacks and absence of text relocations. |
| PCM format and sample calculations | Matched software output to S32_LE, tested channel order, full-scale signed samples, attenuation, mute and expanded buffer sizes. Fixed signed shifts exposed by sanitizers. |
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

The host suite has 21 tests. Driver replays run under address and undefined
behavior sanitizers. The simulator uses an isolated temporary filesystem and
file-only audio output. It validates shared Rockbox behavior using macOS
codecs; it cannot establish Android linker or physical DAC behavior.

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

1. Deploy the matched executable/assets, then verify the previous status-139
   crash with Android diagnostics available. Symbol collision was a concrete
   hazard; the original crash has not been traced to it conclusively.
2. Check physical buttons, encoder behavior, touchscreen edges, gestures and
   software lock against the real evdev devices.
3. Confirm DAC output and volume polarity/calibration, both jacks, both DACs,
   balance, filters, sample-rate transitions, underruns and long playback.
4. Measure temperature and battery use during sustained playback and screen
   off/on. Whole-system suspend remains deliberately unimplemented pending
   hardware investigation.
5. Insert a microSD card, collect mmc1 and mount state, then implement and test
   mounting/removal. Its live device identity and filesystem are still unknown.
6. Check charger changes, unplugged battery reporting, reboot and poweroff on
   hardware. Current-draw estimates and the voltage fallback are uncalibrated.
