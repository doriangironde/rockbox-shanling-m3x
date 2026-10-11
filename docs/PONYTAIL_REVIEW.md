What this change does: The M3X workspace now separates public source, generated builds and private recovery/evidence files. This maintenance pass removes concrete dead weight and fixes demonstrated installer, graphics, input, audio and diagnostic defects rather than replacing working code based on its authorship.

# Ponytail review — 2026-10-10

Ponytail 5.1.0 was installed from `DietrichGebert/ponytail` using the documented
omp marketplace commands. Its review instructions were read and applied here.
The installed plugin is user-wide; a new omp session loads its lifecycle hooks.

## Scope and load

Reviewed all M3X-modified source hunks and new target files, the complete
`tools/m3x/` tool set, launcher/module scripts, Android activity, regression
replays, configuration, theme text and connected upstream callers. Assumed one
player/user with concurrent UI, timer, audio and PCM workers, plus installer and
launch lock contention. Existing device calibration, thermal limits, error
recovery, signing identity, music/settings and backups were preserved.

This is not an exhaustive audit of every unmodified upstream Rockbox file,
third-party library, archived kernel or EDL utility. Their relevant interfaces
were traced where the M3X changes touched them. No code was identified as bad
merely because AI helped write it.

## Must fix before broader release claims

1. **Shared executable modules remain a trust boundary** (`tools/m3x/install.sh`, `rockbox/firmware/export/rbpaths.h`).
   - **What this is:** The root player uses codecs/plugins in shared `.rockbox` storage.
   - **Problem:** [INFERENCE] An Android app with write access to that tree could replace code the root player later loads. On-device app permissions were not exercised here.
   - **Fix:** A hardened release needs root-owned executable modules separated from writable settings/music. This pass documents the development requirement not to give untrusted apps storage-write access; it does not change the loader/storage architecture.
   - **If we skip it:** This setup must remain a trusted development device, not be presented as a hardened root boundary.

2. **USB switching does not resume plugin-generated audio** (`rockbox/apps/playback.c:audio_m3x_switch_output`, `rockbox/apps/plugins/midi/midiplay.c`, `rockbox/apps/plugins/doom/i_sound.c`).
   - **What this is:** MIDI and Doom can stop music and use their own mixer channel.
   - **Problem:** The stopped-music hotplug branch stops PCM and refreshes buffers without restarting those plugin channels; plugin callbacks also lack dynamic output-rate updates.
   - **Fix:** Define plugin mixer restart/rate handling before claiming USB hotplug support for plugin audio. No speculative plugin resampling policy was added during cleanup.
   - **If we skip it:** Switching USB/internal output during these plugins can interrupt their sound. Normal music playback hotplug is covered separately.

## Must fix findings applied

3. **Bundle checks missed unchecked files and symlinks** (`tools/m3x/install.py`, `tools/m3x/install.sh`).
   - **What this is:** Host and direct-device installers verify extracted files before root installation.
   - **Problem:** The shell route checked only listed hashes; the host route missed directory/metadata symlinks. Extra module code could bypass validation.
   - **Fix:** Reject all symlinks, malformed/duplicate/escaping checksum paths and mismatched file inventories before installation. Behavioral regressions pass.
   - **If we skipped it:** A supposedly checked bundle could install unchecked filesystem objects as root.

4. **Asset path aliases bypassed private-state checks** (`tools/m3x/package-installer.py:asset_files`).
   - **What this is:** The packager imports `.rockbox` asset ZIP members.
   - **Problem:** Names such as `.rockbox//config.cfg` normalized differently from their stored names and escaped the settings exclusion.
   - **Fix:** Require canonical POSIX paths and reject backslashes, aliases, duplicate members, symlinks and private state.
   - **If we skipped it:** Malformed bundled assets could replace settings or resume state.

5. **The root helper and logs used shell-writable locations** (`tools/m3x/install.sh`, `m3x-module/launch.sh`, `m3x-module/service.sh`).
   - **What this is:** The launcher runs the framebuffer helper and redirects logs as root.
   - **Problem:** A predictable temporary helper staging path followed symlinks; a helper or log in `/data/local/tmp` was exposed to directory replacement by the shell user.
   - **Fix:** Stage/deploy `fbpan` inside the root-owned module, put production logs in root-owned module/session state, and migrate every launch/probe/collector caller. Legacy rollback retains the original snapshot layout; no runtime alias remains.
   - **If we skipped it:** Lower-privilege directory control could redirect root writes or execution. The symlink-victim preservation regression now passes.

6. **Failed launcher removal falsely completed rollback** (`tools/m3x/install.sh:restore_snapshot`).
   - **What this is:** First-install rollback removes the newly added Android icon.
   - **Problem:** Package Manager uninstall failure was ignored.
   - **Fix:** Require a successful uninstall response, propagate failure and retain the snapshot for retry.
   - **If we skipped it:** Rollback could report success while leaving components installed.

7. **A failed boot-disable operation leaked the launch lock** (`m3x-module/launch.sh`).
   - **What this is:** Manual launch must disable boot autostart before starting its worker.
   - **Problem:** If `touch disable` failed, the newly created lock had no PID and remained forever.
   - **Fix:** Release that lock before returning failure; test both refusal and a successful retry.
   - **If we skipped it:** Future launches would remain blocked until manual lock removal. The regression failed before the fix and passes now.

8. **Boot output could destroy its own input or a backup** (`tools/m3x/patch-dtb-gpio18.py:main`).
   - **What this is:** The offline patcher writes a new boot image.
   - **Problem:** Opening an existing output with `wb` truncated it, including the source or a symlinked recovery image.
   - **Fix:** Refuse existing destinations and create output exclusively; close intermediate files through standard-library helpers. Synthetic overwrite checks and both private image-preservation regressions pass.
   - **If we skipped it:** A mistaken path could destroy irreplaceable recovery material. The saved old implementation demonstrably overwrote its input.

9. **Native bitmaps and scalar colors disagreed with RGBA storage** (`rockbox/tools/configure`, `rockbox/firmware/export/lcd.h`, `rockbox/apps/plugins/fft/fft.c`).
   - **What this is:** Build-time bitmap encoding and scalar helpers supply native framebuffer pixels.
   - **Problem:** M3X emitted two-byte RGB565 data for four-byte RGBA pixels; little-endian scalar packing swapped red/blue. FFT's workaround also broke the ordinary high-depth palette contract.
   - **Fix:** Use the named-field native bitmap encoder, convert RGBA channels explicitly and delete the workaround. Reconfigure both builds and regenerate their bitmaps.
   - **If we skipped it:** Artwork, solid colors and plugin palettes would disagree or fail to build. Before/after smoke proves old failures, new RGBA correctness and unchanged XRGB behavior.

10. **UI touch locking raced the decoder** (`rockbox/firmware/target/hosted/ibasso/m3x/button-m3x.c`, shared button driver).
    - **What this is:** UI calls toggle touch while a timer thread decodes contact slots.
    - **Problem:** UI state mutation could invalidate the primary slot after the input thread checked it. Hosted IRQ masking does not serialize these threads.
    - **Fix:** Atomically request enable/reset from UI and apply pending reset on the input tick. Replace the pass-through filter with a required tick hook and migrate callers.
    - **If we skipped it:** Concurrent locking could corrupt touch state or index an invalid slot. ThreadSanitizer detected the old race; the fixed 200,000-iteration stress run exits cleanly.

11. **Other targets lost required framebuffer providers** (`rockbox/firmware/asm/SOURCES`).
    - **What this is:** Source selectors supply framebuffer copy/YUV routines.
    - **Problem:** The M3X addition had removed selectors still needed by Creative Zen/Zen X-Fi and M:Robe fallback callers.
    - **Fix:** Restore their previous selectors while retaining M3X.
    - **If we skipped it:** Those target builds could lose required symbols. Their full cross-builds were not performed.

12. **Internal PCM retry could recover into silence** (`rockbox/firmware/target/hosted/ibasso/pcm-ibasso.c`).
    - **What this is:** An internal audio open failure removes the DAC route before retry.
    - **Problem:** The retry did not restore the route.
    - **Fix:** Restore the muted, calibrated route before the second open attempt.
    - **If we skipped it:** A recovered PCM handle could produce no internal audio. Saved-source reproduction fails before and passes after.

13. **Queue clearing could strand a pending output change** (`rockbox/apps/audio_thread.c`, `rockbox/apps/playback.c`).
    - **What this is:** The PCM worker waits for the audio thread to process an output-change notification.
    - **Problem:** Stop/buffer shrink cleared the queued event but left the driver pending flag set.
    - **Fix:** Use one queue-clear helper that clears stale work and replays a still-pending output notification; migrate both clearing sites.
    - **If we skipped it:** Later playback could remain blocked. The modeled queue-delivery smoke fails on the old clearing behavior and passes on the new helper.

14. **Voice could start during a partially updated output handoff** (`rockbox/apps/playback.c:audio_m3x_switch_output`).
    - **What this is:** Hotplug updates sink capabilities, mixer buffers, settings and frequency.
    - **Problem:** New UI speech could restart PCM between these operations.
    - **Fix:** Stop voice before locking, then hold the existing recursive PCM lock across the complete handoff.
    - **If we skipped it:** The worker could see partially updated buffer/frame state. A compiled actual-function smoke with a competing voice starter fails before and passes after.

15. **Framebuffer probes used the wrong page/color or hid failures** (`tools/m3x/fbpan.c`, `tools/m3x/fbdump.c`).
    - **What this is:** The helper selects/fills pages; the dump tool writes raw framebuffer evidence.
    - **Problem:** Pan printed but never assigned the requested offset; red/blue fill bytes were reversed; fill, short-write and close failures could still report success.
    - **Fix:** Apply the requested page and RGBA bytes, and propagate nonzero fill/write/close failures.
    - **If we skipped it:** Recovery could retain the old page and failed captures could look valid. Fake-I/O regressions fail on the saved originals and pass now; the probes also cross-compile for ARM64.

16. **Tone generation could overrun memory and emit bad PCM** (`tools/m3x/m3xtest.c`).
    - **What this is:** The direct diagnostic generates test tones in supported sample formats.
    - **Problem:** Low frequencies exceeded the allocation; normal chunk traversal reached uninitialized samples; S16/S24 scaling and missing CLI arguments were unsafe.
    - **Fix:** Generate bounded, initialized chunks with continuous phase and correct scaling; validate arithmetic, format combinations, finite inputs and required arguments. Preserve the existing amplitude/channel balance.
    - **If we skipped it:** A diagnostic could corrupt memory or produce distorted/gapped audio. Sample-by-sample, low-frequency and invalid-format regressions now pass.

17. **A read probe forced kernel-owned I2C access** (`tools/m3x/i2cdump.c`).
    - **What this is:** The register reader targets DAC addresses also used by the kernel driver.
    - **Problem:** `I2C_SLAVE_FORCE` bypassed ownership protection without establishing the advertised idle condition.
    - **Fix:** Delete the FORCE fallback and respect EBUSY.
    - **If we skipped it:** Register-pointer transactions could interfere with the live kernel driver. Actual hardware refusal was not exercised.

## Should fix findings applied

18. **Optimization disabled release checks** (`tools/m3x/check-package.py`, `tools/m3x/package-offline.py`).
    - **What this is:** Python verifies ZIP integrity and matching codec/plugin bytes.
    - **Problem:** `python -O` removed `assert` checks while the validator still printed PASS.
    - **Fix:** Use explicit validation failures. The optimized-Python stale-module regression passes.
    - **If we skipped it:** An optimized environment could package mismatched modules silently.

19. **Evidence and public metadata could stay stale** (`tools/m3x/run-simulator-test.py`, `tools/m3x/package-installer.py`).
    - **What this is:** Simulator runs save PASS summaries; packaging creates release archives and user-facing metadata.
    - **Problem:** A failed simulator rerun retained the old summary, and release-level README/checksums still described an older archive pair.
    - **Fix:** Remove the old summary before a new run, reuse the preview runner pattern, package canonical root docs and refresh release README/checksums with the generated pair.
    - **If we skipped it:** Old success or release information could be mistaken for the current run.

20. **Unnecessary code and misleading explanations increased maintenance work** (`m3x-module/post-fs-data.sh`, target input/LCD headers, FFT, theme generator and README files).
    - **What this is:** Boot hooks, graphics/input integration and documentation explain the port.
    - **Problem:** The deliberately passive boot hook had redundant checks; a required target hook was weak/advertised optional; old commented LCD configuration, an unused theme-text copy and duplicate/inaccurate comments remained. Two debug format strings also mismatched ARM64 argument types.
    - **Fix:** Keep an explicit passive hook, require the target hook, delete dead blocks/copies/comments, correct format types and replace chronological README clutter with task-oriented guides.
    - **If we skipped it:** Readers would have to distinguish obsolete attempts from working paths, and integration omissions could reach runtime rather than fail linking.

## Exercised verification

| Check | Observed result |
| --- | --- |
| `sh tools/m3x/verify-offline.sh` | Native ARM64 build, device-test compilation, complete suite: **60 tests passed**, including both private boot fixtures; 56 packaged modules matched |
| Actual simulator MP3 smoke | PASS; playlist-control file created; 184,320 stereo frames captured to a file; menu/playback screenshots |
| Actual iPod-theme interaction smoke | PASS; seven screenshots; scrolling, pause/resume and Home; 206,848 stereo frames captured to a file |
| Saved-source regressions | Original launch lock, framebuffer/dump/tone, boot overwrite, RGBA and audio cases failed as expected; corresponding current checks passed |
| Input concurrency | Old code: ThreadSanitizer race; fixed code: stress run exit 0 with no race report |
| Changed probe compilation | `fbpan`, `fbdump`, `i2cdump`, `m3xtest` built as ARM64 PIE under `local/work/probes/` |
| Shell entrypoints | `sh -n` passed for launcher/install/diagnostic scripts |
| Documentation navigation | All checked local README/guide links existed |

The simulator menu/playback PNGs were visually inspected. The tested native
build retains its existing debug configuration. No device session, ADB command,
root installation, firmware flash, audible output or signing-key replacement
was performed during this maintenance pass.

The system Python lacked Pillow and its pkg-config did not see Homebrew SDL2.
The existing Homebrew Python/SDL2 installation completed the smoke runs; PATH
requirements are documented. Existing fixed-point host warnings and Pillow's
future `getdata` deprecation remain; no warning was suppressed.

## Workspace cleanup

Private validation, device dumps, backups, firmware, EDL/kernel research and
historical notes now live under `local/`. The old boot prototype and downloaded
Magisk APK are archived there. The unused standalone toolchain is retained under
`local/toolchains/`; active build/NDK paths stay unchanged. Approved Python caches,
loose compiled probes and the redundant NDK ZIP were removed. Signing keys,
recovery images, source history, older release archives and validation records
were preserved. `publish/` remains an export snapshot; root files are canonical.

Verdict: fix 1 and 2 before broader hardened/plugin-hotplug release claims.
The development cleanup and exercised host/simulator workflows pass.

Lean: -407 documentation lines applied across the README and tool guide.
Dead FFT conversion, I2C force-claim, optional-hook scaffolding, commented LCD
attempt and unused text allocation removed without a blanket rewrite.

Not checked: physical hardware, Android app storage permissions, actual power/USB-loss recovery, other target cross-builds or an exhaustive audit of unrelated upstream/kernel code.
