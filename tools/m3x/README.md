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

Internal storage now uses `/data/media/0` directly, independent of Android
FUSE. No microSD card is detected on `mmc1`; card mounting/hotplug remains
unverified.

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

Boot-image tests skip when their four private fixtures are absent. ARM64
build tests require a completed native build; see the root README for setup.
