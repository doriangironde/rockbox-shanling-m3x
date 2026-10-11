# M3X development tools

Run host commands from the project root. These tools serve three different jobs:
build/package the port, replay driver behavior without hardware, and diagnose an
already rooted M3X. **Host verification never needs ADB or a firmware flash.**

For routine installation, use [INSTALL.md](INSTALL.md), not a diagnostic probe.
The [root README](../../README.md) covers prerequisites and daily use;
[M3X_OFFLINE_CHECKLIST.md](../../M3X_OFFLINE_CHECKLIST.md) records prior hardware
results and outstanding checks.

## Host workflows

| Command | Purpose |
| --- | --- |
| `sh tools/m3x/verify-offline.sh` | Build ARM64 Rockbox, run replays, cross-compile device tests/helper, check matching modules and create development bundles; package an installer if an APK is present |
| `python3 -m unittest discover -s tools/m3x/tests -v` | Host regression suite; build-output checks need the native build, private boot-image checks skip without fixtures |
| `python3 tools/m3x/check-package.py` | Check ZIP integrity and packaged codec/plugin bytes against the current build |
| `python3 tools/m3x/package-offline.py` | Bundle an existing development installation's manual update files; not a standalone installer |
| `python3 tools/m3x/build-android-launcher.py --sdk /path/to/android-sdk` | Build/sign the Android icon; preserve `build-m3x-launcher/launcher-signing.jks` |
| `python3 tools/m3x/package-installer.py` | Emit checksummed installer and corresponding-source ZIPs in `build-m3x/release/` |

Driver replays compile real target code against pipes and mock hardware, using
AddressSanitizer and UndefinedBehaviorSanitizer. They cover input frames and
locks, battery/sysfs failures, backlight state, PCM format/volume, worker/control
handoff, USB hotplug and clean return versus physical shutdown. Launcher tests
use harmless child processes and temporary files rather than stopping Android.

Shared codec/plugin builds hide private symbols while exposing their loader
headers. If compiler/linker flags change, use a clean native build: Make does not
track changed flags as object dependencies. Never deploy mismatched native
executables, codecs or plugins.

Native module paths now use a private runtime inside the root-owned Magisk
module. The standalone installer splits the ordinary `rockbox.zip` into private
runtime code/support files and shared data; extracting that ZIP into shared
storage is no longer a valid native update. Simulator paths remain unchanged.
The root loader gate has sanitizer-backed path/ownership/mode/symlink replays
and limited device loading/migration/playback checks. See the validation record
for the exact native/launcher hashes, heavy-plugin thermal stop and remaining
USB, analog, battery and broader plugin checks.

## Desktop UI and theme

Build the macOS SDL2 simulator using the [root build steps](../../README.md#test-without-the-player).
On Apple silicon/Homebrew, use `export PATH=/opt/homebrew/bin:$PATH` so SDL2's
`pkg-config` and the Python with Pillow take precedence over system tools.
Then:

```sh
python3 tools/m3x/run-simulator-test.py
python3 tools/m3x/preview-ipod-theme.py
```

The first boots the real simulator, selects the licensed test MP3 and captures
menu/playback images and decoded stereo audio. The second needs Pillow and
checks theme taps, a scrolling song list, pause/resume and Home. Both use isolated
temporary disks and file-only audio. Evidence lives in `local/validation/`.
Neither proves Android linking or physical audio quality.

Interactive simulator controls: mouse for touch, Space for play/pause,
Left/Right for track changes, Up/Down or +/- for volume, and Escape for Power.
For a quiet interactive launch:

```sh
SDL_AUDIODRIVER=dummy build-m3x-sim/rockboxui --nobackground --zoom 0.5
```

To regenerate theme assets, install Pillow and run
`python3 tools/m3x/make-ipod-classic-theme.py`, then rebuild the asset ZIP.
`make-768x1280-theme.py` generates the alternate cabbiev2 assets; it is not the
iPod-theme generator.

## Device safety

Use only the tested rooted M3X prerequisite setup. Install the matching bundle,
return to Android, disable boot autostart and stop Rockbox before a direct probe.
Begin with a cool player and headphones disconnected; output level is not
calibrated. Root commands below are diagnostics, not normal user instructions.

The normal launcher stops both Android app runtimes, then stops/confirms
BootAnimation before SurfaceFlinger, and restores Android when the player exits. Native thermal services stay active. It
stops at **CPU sensor0 ≥55 °C, battery ≥40 °C, or unavailable sensor readings**.
These are conservative test cutoffs, not hardware specifications. Startup waits
at most 115 seconds for cooling. Whole-system suspend stays disabled because the
previous display suspend path did not wake reliably.

Avoid repeated `su` calls during takeover: Magisk's Android logging can introduce
artificial load while the framework is stopped. Start detached probes while
Android is still running. Keep recovery backups and raw device logs private.

Production `fbpan` and scripts live in the root-owned module directory
`/data/adb/modules/rockbox_m3x/`. Manual-session logs live under
`/data/adb/rockbox-m3x-manual/`. Do not replace production helpers through
Android's shell-writable `/data/local/tmp` directory.

## Read-only state collection

`collect-state.sh` reads runtime/service state, thermal and battery nodes,
mmc1/microSD identity, launcher logs and Rockbox tombstones. It does not change
services, mounts or audio routes:

```sh
adb push tools/m3x/collect-state.sh /data/local/tmp/m3x-collect-state.sh
adb shell "su -c 'sh /data/local/tmp/m3x-collect-state.sh /data/local/tmp/m3x-state'"
mkdir -p local/validation/device-test
adb pull /data/local/tmp/m3x-state local/validation/device-test/
```

After any takeover test, confirm no Rockbox process remains and `zygote`,
`zygote_secondary`, `surfaceflinger` and `thermal-engine` are running. Preserve
crash logs, tombstones and the exact build if status 139 returns.

## Input, storage and bounded launch checks

```sh
sh tools/m3x/tests/build-device-tests.sh
python3 tools/m3x/tests/build-filesystem-test.py
```

These compile `/tmp/m3x-input-battery-test` and `/tmp/m3x-filesystem-test`.
Copy/run them on the M3X only for an intentional device session. The input test
also reads the live battery node. The filesystem replay needs installed assets
and `Music/Kevin MacLeod - Sneaky Snitch.mp3`; its temporary control file uses
exclusive creation and is removed afterward.

`launcher-probe.sh` uses a temporary module directory to observe the installed
service for about 75 seconds, with a separate 100-second shutdown guard. It
delays takeover ten seconds for Magisk logging. `idle-probe.sh` runs a binary for
a bounded CPU/temperature observation; modes are `android`, `native`, and
`takeover`. The last stops SurfaceFlinger alone and reproduces the framework
watchdog failure: use it only to investigate that failure.

Summarize a pulled idle trace with:

```sh
python3 tools/m3x/summarize-idle-probe.py /path/to/trace.txt
```

Rockbox CPU is a percentage of one core; system CPU is aggregate. Invalid
intervals caused by backwards vendor idle counters are omitted. Load average
includes blocked kernel tasks and is not CPU utilization.

`bounded-boot-service.sh` is a **one-boot diagnostic wrapper**, not the normal
launcher. Save `service.sh` as `service-normal.sh` before installing it. The
wrapper atomically restores the normal script, limits the run to 180 seconds
and leaves the module disabled. Never overwrite an executing shell script's
inode; the shell can read the replacement tail.

## Sustained playback diagnostics

`playback-soak.sh` runs the installed player with an independent 20-minute guard,
thermal cutoffs and a read-only observer. Build the observer from the **same
native executable** installed on the player:

```sh
python3 tools/m3x/build-playback-observer.py /tmp/m3x-observer
adb push /tmp/m3x-observer/m3x-playback-observer /data/local/tmp/
adb push /tmp/m3x-observer/m3x-playback-observer.md5 /data/local/tmp/
adb push tools/m3x/playback-soak.sh /data/local/tmp/m3x-playback-soak.sh
adb shell mkdir /data/local/tmp/m3x-soak-unique-run
adb shell "su -c 'chmod 755 /data/local/tmp/m3x-playback-observer; sh /data/local/tmp/m3x-playback-soak.sh /data/local/tmp/m3x-soak-unique-run'"
```

Wait for `ready`, select the album and briefly press Power to lock the screen.
The observer reads `/proc/PID/mem` without ptrace, suspension or memory writes.
Its build-derived offsets and executable hash prevent accidental mismatches;
rebuild it after changing the player.

An optional atomic `OUT/command.pending` → `OUT/command` rename submits `tap X Y`,
`lock`, `unlock`, `capture LABEL`, `duplicate` or `finish`. Coordinates use the
768×1280 panel. Check the observer's actual lock flag; manual Power presses also
toggle it. Four consecutive stopped PCM samples end the run, so a pause is not
an uninterrupted-album result.

After `done`, pull to a fresh private directory and summarize:

```sh
adb pull /data/local/tmp/m3x-soak-unique-run local/validation/soak-unique-run
python3 tools/m3x/summarize-playback-soak.py local/validation/soak-unique-run \
    --expected-album /path/to/metadata.json
```

The optional metadata is an ordered array with a `file` for each expected track.
Tracks need no ID3 track-number tag: a running PCM stream and nonempty file path
identify playback. The summary checks completion, timing, lock coverage,
underruns and Android recovery. Rate changes can reopen PCM and reset counters;
a completed format sweep is not necessarily an uninterrupted-album pass.
None of this measures analog output or unplugged battery consumption.

`make-format-fixtures.py OUTPUT_DIR` requires ffmpeg/ffprobe and generates quiet
synthetic MP3/FLAC tracks at 32–192 kHz plus a playlist and metadata. It does not
use user music; generating files alone is not a hardware pass.

## USB headphone diagnostics

`usb-audio-capture.sh` is a detached read-only recorder (default five minutes,
range 30–600 seconds). Start it while Android/ADB is available, disconnect the
computer, connect USB headphones, compare Android and native playback, return
to Android and reconnect:

```sh
adb push tools/m3x/usb-audio-capture.sh /data/local/tmp/m3x-usb-audio-capture.sh
adb shell "su -c 'sh /data/local/tmp/m3x-usb-audio-capture.sh /data/local/tmp/m3x-usb-audio-test1 300'"
```

Use a new output directory and pull after `done`. It records ALSA/USB identities,
stream formats, service/thermal state and kernel logs, not actual audio samples.
It never starts playback or changes USB routes. An optional matching observer
adds player progress. Recorder completion does not end a Rockbox session.

`usb-earpods-probe.c` is a separate direct-output diagnostic for Apple EarPods
`05ac:110b`. It waits for that exact device, tries quiet bounded tones, never
opens card 0 and never changes Android services. A write-success log does not
prove audible output. Native player support remains stereo S16_LE/44.1 kHz;
other USB devices, microphones and headset controls are unsupported.

## Low-level probes

| Source | Purpose / caution |
| --- | --- |
| `fbpan.c` | Proven blank/pan/fill operations using real `<linux/fb.h>` ioctls |
| `fbinfo.c`, `fbdump.c` | Geometry and raw framebuffer capture |
| `mdpcommit.c` | MSM display-commit ioctl investigation, not the normal display path |
| `m3xtest.c` | Mixer/PCM inspection, routing and explicit tone tests |
| `i2cdump.c` | Raw I2C access; refuses kernel-owned addresses with EBUSY instead of forcing access |
| `patch-dtb-gpio18.py` | Offline boot-image patcher for the prerequisite device-tree fix; not an installer or flasher |

Build disposable probes into a work directory, not next to their sources:

```sh
mkdir -p local/work/probes
TC=build-m3x/android-toolchain/bin/aarch64-linux-android-gcc
"$TC" -std=gnu11 -O2 -fPIE -pie -Wall -Wextra tools/m3x/fbpan.c -o local/work/probes/fbpan
```

Audio probes additionally link bundled tinyalsa's `pcm.c`/`mixer.c`, with
`-Irockbox/firmware/target/hosted/tinyalsa/include -lm -ldl`. Android rejects
non-PIE executables. `m3xtest prepare 0 44100 2 0 256 4` is a silent prepare-only
check; format 0 is S16_LE, 1 is S32_LE. The vendor DSP rejected S32_LE preparation
on the tested player even though open succeeded.

Never invent framebuffer ioctl constants. Include `<linux/fb.h>`; an earlier
incorrect number produced a false conclusion that the driver lacked support.
Private boot fixtures now live under `local/firmware/` and `local/backups/`.
The earlier `magisk-module/` prototype is archived under `local/archive/` and
must not replace the current `m3x-module/` service.
