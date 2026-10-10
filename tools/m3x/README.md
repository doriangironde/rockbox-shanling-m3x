# M3X device-probe tools

All of these run on the device as root:
`adb shell "su -c '/data/local/tmp/<tool>'"`

Built against Rockbox' bundled tinyalsa where relevant, with
`-fPIE -pie` (the Android linker rejects non-PIE binaries).

| tool | purpose |
|---|---|
| `fbpan.c` | **Display.** `FBIOBLANK` / `FBIOPAN_DISPLAY` / fb fills. Uses the real `<linux/fb.h>` ioctl numbers. This is the working display tool. |
| `m3xtest.c` | mixer + PCM inspector. `route` applies the whole AK4497 route and opens the PCM in one process. |
| `mdpcommit.c` | `MSMFB_DISPLAY_COMMIT` ioctl tester. |
| `fbinfo.c` | fb geometry dump. |
| `fbdump.c` | dumps fb0 to a PNG for pixel-exact verification. |
| `i2cdump.c` | raw I2C register read (bus 5 is owned by the kernel driver, so this usually fails with EIO). |

## Do not add fb ioctl numbers by hand

An earlier version of this directory contained an `fbflip.c` that defined its own
constants:

```c
#define FBIOPANIC  _IOW('F', 0x20, __u32)   /* actually encodes to 0x4602 = FBIOGET_FSCREENINFO */
```

That is what produced a completely wrong conclusion about the display being
unreachable. `mdss_fb_do_ioctl()` returns
`-ENOSYS` for any ioctl number it does not recognise, so invented numbers look
exactly like "the driver lacks this feature".

**Always `#include <linux/fb.h>`.** The real values:

```
FBIOGET_VSCREENINFO  0x4600     FBIOGET_FSCREENINFO  0x4602
FBIOPUT_VSCREENINFO  0x4601     FBIOPAN_DISPLAY      0x4606
FBIOBLANK            0x4611
```

## Building

```sh
TC=build-m3x/android-toolchain/bin/aarch64-linux-android-clang
$TC --target=aarch64-linux-android24 -O2 -fPIE -pie -o /tmp/fbpan tools/m3x/fbpan.c
adb push /tmp/fbpan /data/local/tmp/fbpan
adb shell su -c 'chmod 755 /data/local/tmp/fbpan'
```

`m3xtest` additionally links Rockbox' tinyalsa:

```sh
$TC --target=aarch64-linux-android24 -O2 -fPIE -pie -o /tmp/m3xtest tools/m3x/m3xtest.c \
    rockbox/firmware/target/hosted/tinyalsa/pcm.c \
    rockbox/firmware/target/hosted/tinyalsa/mixer.c \
    -Irockbox/firmware/target/hosted/tinyalsa/include -lm -ldl
```

## Idle and thermal diagnostics

`idle-probe.sh` runs an existing binary for a bounded interval, records system
CPU counters, process/thread CPU counters, wait channels and temperatures, then
kills its child and releases the Rockbox wakelock. It requires the boot module
to be disabled. It stops early at CPU sensor0 >= 55 C or battery >= 40000 mC.
These are conservative test cutoffs, not hardware limits. Use a menu startup
configuration with voice and automatic playback disabled.

```sh
adb push tools/m3x/idle-probe.sh /data/local/tmp/idle-probe.sh
adb shell "su -c 'sh /data/local/tmp/idle-probe.sh /data/local/tmp/rockbox-dbg /data/local/tmp/m3x-idle android 60'"
adb pull /data/local/tmp/m3x-idle.txt /tmp/m3x-idle.txt
python3 tools/m3x/summarize-idle-probe.py /tmp/m3x-idle.txt
```

Mode `android` leaves Android running. Mode `takeover` stops SurfaceFlinger
alone and restores it afterward; this reproduces the framework watchdog failure
and should only be used to investigate that failure. Mode `native` stops both
zygotes first and restores them after SurfaceFlinger on exit. Its 180-second
device test passed on 2026-10-07. Avoid repeated `su` invocations during
takeover: Magisk launches Android content-provider calls to log each invocation,
which can accumulate while the framework cannot render.

The CPU column for Rockbox is percent of one core; system CPU is the aggregate
CPU counter ratio. The vendor kernel can move idle counters backwards when
cores go offline; the summary omits invalid intervals. Linux load average
includes uninterruptible kernel tasks, so it is not a
CPU utilization measurement. The M3X has several idle kernel threads in state
`D` even when Android is running and CPU use is low.

The current module source is `m3x-module/`; `magisk-module/` is an earlier
prototype. Launcher regression checks:

```sh
python3 -m unittest discover -s tools/m3x/tests -v
sh -n m3x-module/service.sh m3x-module/post-fs-data.sh tools/m3x/idle-probe.sh
```

`launcher-probe.sh` checks the installed launcher for about 75 seconds using a
temporary module directory while the actual boot module stays disabled. It
detaches and delays takeover ten seconds so Magisk can finish logging the
initiating root command while Android is still available. Without that delay,
even one root request can cause Magisk to repeatedly launch `am start` after
the framework stops, consuming about one CPU core. This is a diagnostic
artifact; module boot startup does not use `su`.

The launcher test passed: Rockbox ~1.0% of one core, aggregate CPU ~0.6%, CPU
sensor0 cooling 47 -> 43 C, with Android and thermal-engine restored on exit.
Starting Android may kill the diagnostic observer before its final snapshot;
verify restoration through service properties and the launcher's own log.

`bounded-boot-service.sh` is a prepared one-boot test wrapper. Save the normal
launcher as `service-normal.sh` before installing it as the module's service.
The wrapper restores `service.sh` immediately, records boot diagnostics in
`/data/local/tmp/m3x-boot-probe.txt`, and sets the disable marker after 180
seconds. The normal launcher's monitor then exits within its five-second check
interval. Personal development logs and raw device dumps are not distributed.

## Input and battery regression replay

The test compiles the real M3X target decoder, shared input loop and battery
reader. Linux pipes supply complete and partial input frames; no events go to
Android or the physical input devices. It checks secondary-finger isolation,
frame boundaries, queued tap preservation, axis limits, overflow recovery,
software touch lock, all six reported key codes, repeat handling and invalid
fuel-gauge readings. It also reads the live M3X capacity. The shared driver's
legacy iBasso branch is compiled separately with warnings treated as errors.

```sh
sh tools/m3x/tests/build-device-tests.sh
adb push /tmp/m3x-input-battery-test /data/local/tmp/m3x-input-battery-test
adb shell "su -c '/data/local/tmp/m3x-input-battery-test'"
```

The test needs root only for the live battery node. `getevent -pl` evidence and
the device replay result were recorded locally. Key codes
are verified against the device's capabilities; physical button presses remain
a separate manual check. Touch handling follows the kernel's protocol-B slot
and frame semantics: https://www.kernel.org/doc/html/latest/input/multi-touch-protocol.html

Internal storage uses `/data/media/0` directly, independent of Android FUSE.
The inserted 128 GB microSD was detected on `mmc1` as `/dev/block/mmcblk1` and
formatted through Android as public FAT32 storage. Android mounts it at
`/mnt/media_rw/external_sd1`; the development installation exposes this raw mount
through `/data/media/0/microSD`, a symlink visible as **Files → microSD** in
Rockbox. This avoids the Android FUSE path during native playback.

The card passed write/read checks, native MP3/FLAC playback at 44.1/48 kHz with
zero observed underruns, and Android unmount/remount with unchanged test-file
hashes. The mount remained available while Android's app runtimes were stopped.
This is basic mounted-card access; physical removal during playback and reboot
persistence have not been tested. Return to Android and eject the card before
removing it. The standalone installer creates this link if no entry exists.

For an already mounted card at that verified path, expose it once with Android
running and Rockbox stopped (the command refuses to replace an existing entry):

```sh
adb shell "su -c 'test -d /mnt/media_rw/external_sd1 && test ! -e /data/media/0/microSD && test ! -L /data/media/0/microSD && ln -s /mnt/media_rw/external_sd1 /data/media/0/microSD'"
```

Do not format a card merely to expose an existing filesystem. FAT32 has a
4 GiB single-file limit; other filesystems have not been validated here.

The one-boot wrapper restores its normal launcher through a temporary file and
atomic rename. Never copy over the executing script's inode: the shell may
read the replacement tail. The post-fs-data hook is now passive; service.sh
owns the runtime shutdown and display takeover.

Boot startup may wait at most 115 seconds for Android's initial CPU burst to
cool below the same 55 C / battery 40 C test thresholds. Native thermal
services and Android remain running while it waits; missing sensors abort
immediately. The 180-second boot wrapper includes this wait in its total bound.

The corrected bounded cold boot passed: final Rockbox CPU 0.66% of one core,
CPU sensor0 49 C, matching theme/assets loaded from direct internal storage,
and automatic Android restoration at the timeout. The installed development
module was left disabled after that test.

## Playlist/storage path regression

```sh
python3 tools/m3x/tests/build-filesystem-test.py
adb push /tmp/m3x-filesystem-test /data/local/tmp/m3x-filesystem-test
adb shell "su -c '/data/local/tmp/m3x-filesystem-test'"
```

This compiles the real hosted filesystem adapter using production build flags.
It validates playlist-control writes and rotation under `/data/media/0/.rockbox`,
plus codec, viewer, config and song reads. It requires the installed assets and
`Music/Kevin MacLeod - Sneaky Snitch.mp3`. Its disposable control probe cannot
overwrite an existing file (O_EXCL) and is removed afterward. The original
failure was desktop path redirection enabled because M3X was absent from the
native-hosted target conditions in rbpaths.h.

## Offline codec and plugin validation

```sh
make -C build-m3x -j4
python3 -m unittest discover -s tools/m3x/tests -p test_shared_symbols.py -v
make -C build-m3x zip
```

This checks the actual ARM64 codec and plugin binaries without connecting the
M3X. The M3X build uses `-fPIC -fvisibility=hidden` for these shared modules;
their loader header `__header` remains public. The test verifies that codec
and plugin API pointers and private libc helpers cannot collide with player
symbols. When changing these flags in an existing build, remove the module
object files under `build-m3x/lib/rbcodec/codecs` and `build-m3x/apps/plugins`
before rebuilding: Make does not track flag changes as dependencies.

An earlier player launch exited with status 139 after the playlist-path probe
passed. Symbol isolation addresses a concrete loading hazard, but the original
crash cause remains unconfirmed. Subsequent bounded native menu, song-selection
and MP3 playback tests passed; plugin loading still needs a device check.

## On-device playback findings — 2026-10-09

Native menu/control tests and 3.5 mm headphone playback were verified on the
M3X. S32_LE passed `pcm_open()` but failed `pcm_prepare()` with the DSP's
`ADSP_EFAILED`, reported by ALSA as ENOMEM. S16_LE passed preparation and
produced clean sound. The native software-volume output and ALSA stream now
both use 16-bit samples. Earlier open-only format probes did not establish
playback support.

The inspector now supports a silent prepare-only check; it never starts the
stream or writes samples:

```sh
# With Android running, no Rockbox instance, and the helper installed:
adb shell "su -c '/data/local/tmp/m3xtest prepare 0 44100 2 0 256 4'"
```

The format argument is 0 for S16_LE and 1 for S32_LE in the bundled tinyalsa.
The production sink checks preparation at initialization and rate changes.
Unrecoverable write errors exit to launcher recovery instead of retrying a
kernel handle whose DSP client may already have been freed.

Holding the PCM mutex across blocking writes protected stream lifetime but
could starve UI/control requests. The M3X worker now hands off to a waiting
control request before starting another write. A continuous-writer replay
reproduces the old starvation and checks bounded control latency, nested
callbacks and safe concurrent sample-rate changes. The user confirmed prompt
volume changes and clean sound on the M3X after the fix.

## Pocket-mode input and display

A short physical power press toggles a native M3X screen/touch lock in the
input driver. It is consumed before application and plugin keymaps, so it
works from menus as well as playback. Goodix-generated KEY_POWER gestures
cannot toggle it. Music and volume keys retain their normal mappings.
Unlock suppresses existing contacts until all fingers lift and does not
change Rockbox's independent software touch-lock state.

The backlight worker rejects wake requests while this lock is active,
including requests from UI activity and selective-backlight settings. The
backlight is set to zero and framebuffer updates are disabled; no framebuffer
blank ioctl or `/sys/power/state` suspend request is used. The launcher's
suspend blocker stays held so audio continues. Holding physical power for
three seconds posts the existing graceful shutdown request, once per hold,
without depending on Linux autorepeat.

The input replay covers locking with a held finger, pocket touches and wake
gestures, media/volume buttons, unlock contact suppression, independent touch
lock, power repeats, long holds and tick wrap. On-device screen-off/playback
results are recorded in the root checklist.

## Offline development and simulator

`sh tools/m3x/verify-offline.sh` builds the ARM64 player, runs the host gates,
cross-compiles the device probes and verifies the packaged modules. It never
invokes adb. Native driver tests use pipes and mock hardware with AddressSanitizer
and UndefinedBehaviorSanitizer; sanitizer findings terminate the test.
The shared fixed-point library has two existing Clang warnings on the 64-bit
host; the replay permits those warnings while retaining sanitizer checks.

Build the simulator on macOS with the installed SDL2 and GCC 16:

```sh
mkdir -p build-m3x-sim
cd build-m3x-sim
../rockbox/tools/configure --target=shanlingm3x --type=s --sdl-threads --no-ccache
make -j4
make install
SDL_AUDIODRIVER=dummy ./rockboxui --nobackground --zoom 0.5
```

The simulator uses the M3X's 768x1280 display, mouse input for touch, Space for
play/pause, Left/Right for previous/next, Up/Down or +/- for volume and Escape
for power. It uses the same supported plugin selection as the hosted build.
The simulator does not exercise the Android PCM hardware.

`python3 tools/m3x/run-simulator-test.py` uses a temporary copy of the installed
simulator assets. It boots, selects the bundled MP3, saves menu and playing
screenshots, verifies a playlist-control file was created and checks decoded
stereo output. SDL writes audio exclusively to a temporary file, so nothing
is played through speakers. Results go to
`validation/offline-20261007-batch/simulator/`.

For the next device session, `collect-state.sh` gathers service states,
temperatures, memory/storage state, mmc1 card identity, launcher logs and
Rockbox tombstones. It changes no services, mounts or audio state. Copy it to
the M3X and run it as root only when the device is available.

The full offline work list, results and remaining hardware checks are in
`M3X_OFFLINE_CHECKLIST.md`.

## Full-album screen-off diagnostics

`playback-soak.sh` starts the installed launcher in a temporary module
directory, leaving boot autostart disabled. It samples PCM pointers/state,
temperatures, process CPU/RSS, USB/battery sensors and a read-only playback
observer every roughly two seconds. The existing 55 C CPU / 40 C battery
cutoffs remain active. An independent 20-minute guard restores Android if
the host disappears. Four consecutive stopped PCM samples end the run;
a pause is therefore not a valid uninterrupted-album result.

Build the observer from the **same completed native build** as the installed
player. The build script derives metadata offsets with that build's compiler
flags, the private tinyalsa underrun offset from its source, and ELF symbol
addresses with `nm`. It reads `/proc/PID/mem` with `O_RDONLY`, without ptrace,
process suspension or memory writes. The launcher checks the installed ELF's
MD5 against the observer manifest before starting; this detects accidental
build mismatches. Rebuild the player and observer together after source edits.

```sh
python3 tools/m3x/build-playback-observer.py /tmp/m3x-observer
adb push /tmp/m3x-observer/m3x-playback-observer /data/local/tmp/
adb push /tmp/m3x-observer/m3x-playback-observer.md5 /data/local/tmp/
adb push tools/m3x/playback-soak.sh /data/local/tmp/m3x-playback-soak.sh
adb shell mkdir /data/local/tmp/m3x-soak-unique-run
adb shell "su -c 'chmod 755 /data/local/tmp/m3x-playback-observer; sh /data/local/tmp/m3x-playback-soak.sh /data/local/tmp/m3x-soak-unique-run'"
```

Wait for the `ready` file, select the first album track, then briefly press
physical Power and leave the controls untouched. The native lock flag and
zero backlight are recorded. The initiating root request detaches and waits
ten seconds before takeover, allowing Magisk's Android logging to finish.
Use ordinary shell reads during playback; repeated `su` requests while the
framework is stopped can introduce artificial CPU load.

For automated controls, write a line to `OUT/command.pending` as the shell
user, then atomically rename it to `OUT/command`. Supported commands are
`tap X Y` in 768×1280 panel coordinates, `lock`, `unlock`, `capture LABEL`,
`duplicate` and `finish`. `duplicate` verifies that the installed launcher
refuses a second instance without restoring Android under the playing one.
`lock` sends a brief key press through `qpnp_pon`; confirm the
observer's actual `screen_locked` flag, since manual presses also toggle it.
The initial fade-out is allowed before checking for unexpected relighting.

After `done` appears, pull into a **new** local directory and summarize:

```sh
adb pull /data/local/tmp/m3x-soak-unique-run /tmp/m3x-soak-result
python3 tools/m3x/summarize-playback-soak.py /tmp/m3x-soak-result \
    --expected-album /path/to/metadata.json
```

The optional metadata JSON is an ordered array with `file` for each expected
track. The summary rejects incomplete captures, seeks/timing discontinuities,
released screen lock, incomplete track coverage, observed underruns, missing
natural completion and failed Android recovery. The cumulative tinyalsa
counter catches internally recovered underruns that PCM snapshots can miss.
Counter resets across handle changes and analog output are separate limits:
this cannot replace listening or an external audio capture. USB-connected
battery/current readings do **not** establish unplugged battery consumption.

`make-format-fixtures.py OUTPUT_DIR` generates eight quiet synthetic stereo
MP3/FLAC tracks at 32–192 kHz, an M3U8 playlist and a checksum/metadata manifest.
It requires local `ffmpeg`/`ffprobe` and does not use user music. These fixtures
allow a short device test of codec and sample-rate transitions after the album
pass. Generating fixtures alone does not validate the hardware path.

Boot-image tests skip when their four private fixtures are absent. ARM64
build tests require a completed native build; see the root README for setup.


## Android icon and clean return

`android-launcher/` contains the small Android app that starts a one-time native
session. Build it with `python3 tools/m3x/build-android-launcher.py`; keep the local
signing key for subsequent APK updates. The app needs Magisk root permission and
an installed `m3x-module/launch.sh`, native player, normal service and fbpan helper.
See [the launcher instructions](../../android-launcher/README.md).

`launch.sh` keeps the real Magisk module disabled, serializes requests with a
root-owned process lock, detaches a root worker, and waits ten seconds for the
app's su request/logging to finish. It then invokes the existing service through
a private session directory. There is no manual-session duration timer. A stale
lock whose owner exited is reclaimed on the next request.

The native main menu's **Return to Android** action uses normal Rockbox shutdown
cleanup, saving state and stopping/closing audio. Its target power boundary then
exits status 0 instead of calling hardware power-off. If shutdown is cancelled
while the database is busy, the temporary return flag is cleared. Physical Power
continues to control screen lock and hardware shutdown. Native-only guards leave
other targets and the simulator unchanged.

The no-ADB round trip was tested during FLAC playback, including restored Android
home UI, closed PCM, released launch lock and disabled boot autostart. Host replays
cover clean process exit versus power-off and repeated/concurrent/stale requests.

## USB-C headphones without a live ADB cable

The M3X PCM backend supports the internal DAC and the Apple USB-C EarPods
described below. USB headphones working in Android do not establish their native
capabilities or whether their driver stays bound after Android takeover.

`usb-audio-capture.sh` is a read-only, detached five-minute recorder. Start it
while the player is connected to the computer and Android is running:

```sh
adb push tools/m3x/usb-audio-capture.sh /data/local/tmp/m3x-usb-audio-capture.sh
adb shell "su -c 'sh /data/local/tmp/m3x-usb-audio-capture.sh /data/local/tmp/m3x-usb-audio-test1 300'"
```

Then unplug the computer cable, connect the USB headphones, and play about
30 seconds in Android. Stop Android playback, tap the Rockbox icon and try the
same track for about 30 seconds. Choose Return to Android, disconnect the
headphones, and reconnect the computer. The collector keeps running through
these changes; it never starts/stops music or alters USB/audio settings.

Pull the capture after its `done` marker exists. The raw data includes ALSA card
identities, USB streaming formats/rates, PCM ownership/state/parameters, USB
interface drivers and role state, Android service state, and before/after kernel
logs. Keep raw device logs private. No actual audio samples are recorded.

The optional duration is 30–600 seconds, sampled every five seconds. Use a fresh
`/data/local/tmp/m3x-usb-audio-*` directory each time. Capture completion only stops
the recorder; it does not end the Rockbox session.

`usb-earpods-probe.c` is a separate direct-output diagnostic for the observed
Apple EarPods ID 05ac:110b. It waits up to 90 seconds for that exact USB audio
card, logs its mixer values without changing them, and tries three-second stereo
S16_LE/440 Hz tones at 48 and 44.1 kHz, at -50 dBFS with fades. It never opens
card 0 or changes Android services. Its 120-second process deadline also bounds
blocked PCM calls. A successful stream log proves writes, not audible sound.
This is a diagnostic and does not add USB output to the Rockbox player.

## Apple USB-C EarPods output

The M3X PCM sink detects Apple USB-C EarPods (`05ac:110b`) at startup and during
a session, discovers
their ALSA card, and uses stereo S16_LE at 44100 Hz. Rockbox resamples other
source rates through the sink capability table. Insertion selects USB; removal
selects the internal DAC. Playback restarts from the saved track position with a
brief gap, preserving pause state. A USB write failure requests stream recovery
instead of exiting the player. An unavailable USB endpoint falls back to the
internal DAC and is retried after removal and reinsertion. Other USB audio
devices, headset controls and microphone input are unsupported. The Android
launch icon and boot behavior are unchanged.

`usb-audio-capture.sh` optionally runs the installed read-only playback observer
when its MD5 matches the running executable. It also captures screen brightness
and CPU/battery temperatures. The recorder remains bounded and never starts
playback, changes routes or controls the screen.

Driver regression replay includes USB card enumeration, exact device identity,
fixed-rate capabilities, bounded ownership handover, removal and write failure,
paused removal, in-flight write serialization, busy fallback and reinsertion.
The direct USB diagnostic was audible at 44100 Hz. Its initial 48000 Hz attempt
was busy, so it establishes no 48000 Hz result. Audible Rockbox playback through
the EarPods was confirmed by the user after the startup enumeration correction.
Subsequent removal produced ENODEV and a player exit, causing the launcher to
restore Android. Kernel uptime continued; this was not a device reboot. The
hotplug implementation addresses this failure. Repeated physical swaps were
confirmed working by the user; captured logs show three internal fallbacks and
four USB selections followed by clean exit 0 and Android restoration.

The device test exposed a USB re-enumeration gap during Android takeover.
Startup detection now retries for at most two seconds before choosing the
internal DAC; the delayed-enumeration regression fails before this correction
and passes afterward.

## Standalone installer and rollback

After building the matching native assets, framebuffer helper and signed launch
APK, run `python3 tools/m3x/package-installer.py`. It writes deterministic
installer and corresponding-source archives under `build-m3x/release/`. See
[installation and recovery instructions](INSTALL.md). The installer checks the
tested rooted M3X firmware 1.75/API 25 target, file hashes, active Android and
backup space, and shares the manual launch lock. It snapshots the player,
assets/settings, scripts, helper and APK before replacement. Android stays the
default boot and an existing microSD entry is preserved. No card is formatted.

Host tests cover corrupted payload refusal, wrong target/running player/pending
launch refusal, settings/music preservation, full rollback, first-install
removal and automatic recovery after APK failure or a partial module copy.
Device update and rollback passed with unchanged settings/resume and card-file
hashes. A matching source archive records all source hashes and excludes private
firmware, signing keys and device logs.
