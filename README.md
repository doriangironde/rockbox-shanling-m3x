# Rockbox for Shanling M3X

An experimental native ARM64 Rockbox port for the Shanling M3X, running on its
existing rooted Android system. This is a development source repository,
not a finished firmware release or a ready-to-install Magisk package.

The port includes a 768×1280 touchscreen interface, hardware-key input, direct
internal-storage access, battery reporting, DAC routing and filters, software
volume/balance, and Android display takeover with thermal monitoring and recovery.
A desktop simulator and hardware-free driver replays support development without
the player attached.

## Current status

Display takeover and bounded menu tests worked on the M3X. Playlist-control
storage paths were fixed and tested on the device. A later launch exited with
status 139; subsequent codec/plugin symbol-isolation fixes and audio changes
have passed offline checks but still need confirmation on hardware.

The local offline pass completed 21 tests, an ARM64 build, and a silent simulator
MP3 decoding test. Two of those tests need private boot-image fixtures and skip
in a public checkout. Physical audio, volume calibration, sustained playback
temperature/battery use, suspend, microSD mounting and final button behavior
remain unverified. See [the full checklist](M3X_OFFLINE_CHECKLIST.md).

## Repository layout

- `rockbox/`: complete upstream source snapshot plus the M3X target and assets.
- `m3x-module/`: current Android takeover and recovery launcher scripts.
- `tools/m3x/`: probes, regression tests, theme generation and development packaging.
- `media/`: the licensed test track and its attribution.

Upstream provenance and component licenses are recorded in [UPSTREAM.md](UPSTREAM.md).
This repository is an independent port and is not an official Rockbox or Shanling release.

## Build the native player

The native build used Android NDK r17c with its ARM64 GCC 4.9 standalone toolchain
and API 24. A modern NDK without GCC is not a drop-in replacement. You need Python 3,
Perl, Make, a host C compiler and zip/unzip. The NDK is supplied separately.
On Apple silicon, the NDK's x86_64 tools require Rosetta.

Run from the repository root, replacing the NDK path with your installation:

```sh
export ANDROID_NDK_PATH=/absolute/path/to/android-ndk-r17c
mkdir -p build-m3x
cd build-m3x
../rockbox/tools/configure --target=shanlingm3x --type=n --no-ccache
make -j4
make zip
cd ..
```

Outputs are `build-m3x/rockbox` and the matching `build-m3x/rockbox.zip` assets.
The executable, codecs and plugins must come from the same build.

After building, run the full offline checks and create a manual development bundle:

```sh
sh tools/m3x/verify-offline.sh
```

This does not connect to, flash or install anything on a player. The resulting
`build-m3x/m3x-offline-bundle.zip` is for manual development updates to an existing
setup. Boot images, rooted firmware and a Magisk installer are not included.

## Test without a player

The source-only checks need Python 3 and a C compiler with address/undefined
behavior sanitizers; no Android NDK is needed:

```sh
python3 -m unittest discover -s tools/m3x/tests -p test_native_drivers.py -v
python3 -m unittest discover -s tools/m3x/tests -p test_service.py -v
python3 -m unittest discover -s tools/m3x/tests -p test_boot_wrapper.py -v
```

The build-contract and shared-symbol tests require the completed ARM64 build.
Boot-image tests require the four privately supplied fixtures listed in
`tools/m3x/tests/test_boot_images.py`; stock firmware and personal backups are
excluded from Git.

The simulator was built on macOS with SDL2, pkg-config and GCC 16:

```sh
mkdir -p build-m3x-sim
cd build-m3x-sim
../rockbox/tools/configure --target=shanlingm3x --type=s --sdl-threads --no-ccache
make -j4
make install
# Supply the font used by the simulator smoke test.
mkdir -p simdisk/.rockbox/fonts
../rockbox/tools/convbdf -f -o simdisk/.rockbox/fonts/35-Adobe-Helvetica.fnt \
    ../rockbox/fonts/35-Adobe-Helvetica.bdf
cd ..
python3 tools/m3x/run-simulator-test.py
```

The automated smoke test currently uses macOS dynamic-library injection. It
decodes the bundled MP3 to a temporary audio file and captures menu/playing
screenshots, so there is no audible output. It tests shared UI/storage/codec
behavior; real Android linking and the physical DAC need device testing.

See [the tool guide](tools/m3x/README.md) for simulator controls, probes and the
read-only state collector to use when an M3X is available.
