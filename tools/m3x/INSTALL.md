# M3X native Rockbox development installer

This bundle installs the native player, matching codecs/plugins and theme,
framebuffer helper, Android launch icon and manual-session scripts. It supports
the tested rooted **Shanling M3X, firmware 1.75 / Android 7.1.1 (API 25)** with
Magisk and the existing I2C5 GPIO18/19 audio device-tree fix. Rooting and that
firmware prerequisite must already be complete; this package contains no boot
image. The APK uses the retained development signing identity.

Android stays the default boot. Start a session from the **Rockbox** app, grant
Magisk root access when prompted, and choose **Return to Android** to exit.
The package has no playback timer. Keep the matching source archive alongside
the installer when distributing it. This is an independent development release.

## Install or update from a computer

Install Python 3.9+ and Android platform-tools (ADB), extract the installer ZIP,
connect the M3X and authorize USB debugging. Return to Android and stop Rockbox
before proceeding. Run these commands inside the extracted directory:

```sh
python3 install.py check
python3 install.py install
```

The first command checks the target, Android runtime, file checksums and free
space without replacing installed files. The second repeats the checks, takes
a private backup, installs the complete bundle and prints its backup path and
rollback command. Save that output. With several ADB devices, use `--serial`;
with ADB outside PATH, use `--adb /absolute/path/to/adb`.

Existing Rockbox settings, saved playlists and custom assets are preserved by
overlaying the new matching files onto a staged copy. Music directories are
left in place. A new installation starts quietly at -60 dB with resume disabled.
An ordinary install error triggers automatic snapshot restoration. If power or
USB is lost mid-install, reconnect with Android running and use the retained
snapshot to restore. Backups are not deleted automatically.

The installer shares the launch lock with the Android app; a pending session
blocks installation. If an interrupted installer leaves a lock, reboot into
Android before retrying. If the lock persists, verify that its recorded PID is
not running before removing that lock directory.

## Restore a backup

With Android running and Rockbox stopped, use the snapshot printed at install:

```sh
python3 install.py rollback --backup dev-BUILD.SNAPSHOT
```

The snapshot includes the previous native player, all `.rockbox` assets/settings,
module scripts, framebuffer helper and launcher APK. Rollback restores those
settings as they were when the backup was made. Music is left in place and boot
autostart remains disabled. A first-install rollback removes newly installed
components. Keep the backup until you are satisfied with the update.

## Install or restore on the player

An Android terminal with root access can run the extracted bundle directly:

```sh
su -c 'sh /storage/emulated/0/Download/Rockbox-M3X/install.sh check'
su -c 'sh /storage/emulated/0/Download/Rockbox-M3X/install.sh install'
```

Use the actual extraction path. A backup contains its own restore script:

```sh
su -c 'sh /data/adb/rockbox-m3x-backups/dev-BUILD.SNAPSHOT/restore.sh rollback /data/adb/rockbox-m3x-backups/dev-BUILD.SNAPSHOT'
```

This ZIP is a standalone installer, not a ZIP to flash through recovery or the
Magisk module installer. No reboot is required after a successful update.

## microSD and USB headphones

Android manages the mounted card. The installer creates **Files → microSD** as
a link to `/mnt/media_rw/external_sd1` if no entry already exists; it preserves
an existing entry and never formats the card. With the card absent, the link
may be unavailable. Basic FAT32 playback and Android unmount/remount passed.
Return to Android and eject before physical removal; live card removal and
reboot persistence still need validation. FAT32 limits a single file to 4 GiB.

Apple USB-C EarPods (`05ac:110b`) use stereo 16-bit/44.1 kHz output, with automatic
USB/internal switching. The internal DAC supports the tested sample-rate
changes. Balanced output, filters, calibrated volume, whole-system suspend and
unplugged battery consumption remain outside this release's validation.

## Build and reproduce the archives

Build native Rockbox and its matching ZIP as described in the repository README,
then build the helper and launch APK (retain the same private signing key):

```sh
build-m3x/android-toolchain/bin/aarch64-linux-android-gcc -std=gnu11 -O2 -fPIE -pie -Wall -Wextra tools/m3x/fbpan.c -o build-m3x/m3x-fbpan
python3 tools/m3x/build-android-launcher.py
python3 tools/m3x/package-installer.py
```

The packager validates the current assets against built modules, records hashes
of the native executable/APK/helper and source tree, and writes installer and
corresponding source ZIPs under `build-m3x/release/`. ZIP order, timestamps and
permissions are fixed, so identical inputs produce identical archives. This
does not claim that a native or APK rebuild with different toolchains/signing
keys produces identical binaries. Signing keys, private device data, firmware
images and validation logs are excluded from the source archive.
