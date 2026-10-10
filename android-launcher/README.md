# M3X Android launch icon

Tap **Rockbox** in Android's app list to start a one-time native session. Magisk
must grant this app root access. The first launch waits ten seconds for the root
request to finish before stopping Android; the display takeover then takes about
twelve seconds. A hot player can wait longer while the normal launcher cools it.

In Rockbox, open the main menu (tap **Home** from Now Playing), scroll down and
choose **Return to Android**. Rockbox saves its state, stops playback and closes
its audio hardware through the normal shutdown path, then exits. The launcher
restores Android. This action does not power off or reboot the M3X.

A short Power press still locks/unlocks the screen. Holding Power still requests
hardware shutdown. Android remains the default after reboot: manual launch keeps
the Magisk module's boot autostart disabled, and has no duration timer.

## Apple USB-C EarPods (experimental)

Apple USB-C EarPods (USB ID `05ac:110b`) can be connected before launch or
during a native session. The player selects their ALSA playback endpoint and uses
16-bit stereo at 44.1 kHz; Rockbox resamples other source rates. Rockbox's volume
and balance remain software controls. Start quietly and adjust volume normally.
With these EarPods absent, playback uses the internal DAC as before.

Connecting the EarPods switches playback to USB; disconnecting them switches
back to the internal DAC. Expect a brief gap while the audio stream restarts.
A paused track stays paused. If USB cannot open, the player falls back to the
internal DAC; unplug and reconnect to retry. Other USB audio devices, headset
buttons and microphone input are not supported. Android's own USB audio routing
is unchanged.

## Build and install

The APK only starts the separately installed native player. It includes no
firmware, native player, codecs, root exploit or network permission. Install the
matching native build, `m3x-module/service.sh` and `m3x-module/launch.sh` in the
existing rooted M3X development installation first. The framebuffer helper at
`/data/local/tmp/fbpan` is also required.

With a JDK, Android SDK build-tools 36.0.0 and platform android-37.0 installed:

```sh
python3 tools/m3x/build-android-launcher.py --sdk /absolute/path/to/android-sdk
```

The APK is `build-m3x-launcher/Rockbox-M3X.apk`, targeting Android API 25.
Copy it to the player and open it in an Android file manager to install, or use
`adb install -r` for development. Grant **Rockbox** access in Magisk's Superuser
screen. The boot module can stay disabled.

Keep `build-m3x-launcher/launcher-signing.jks` privately: updates need the same
signing identity. The build script creates a local development key on first use;
the key is not included in source or the APK. The build output directory is
ignored by the source repository.

To remove the icon, uninstall **Rockbox** through Android. This leaves native
player files and music intact. Before replacing a native build, stop Rockbox and
keep a copy of the previous player and launcher scripts for rollback.
