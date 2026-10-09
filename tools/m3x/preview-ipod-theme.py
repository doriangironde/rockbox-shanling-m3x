#!/usr/bin/env python3
"""Render and exercise the M3X theme in an isolated, silent macOS simulator."""
from pathlib import Path
import array
import json
import os
import shlex
import shutil
import subprocess
import tempfile
import zipfile
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build-m3x-sim"
OUT = ROOT / "validation/ipod-theme"
OUT.mkdir(parents=True, exist_ok=True)
(OUT / "summary.json").unlink(missing_ok=True)
with tempfile.TemporaryDirectory(prefix="m3x-ipod-") as directory:
    base = Path(directory)
    disk = base / "simdisk"
    disk.mkdir()
    shutil.copytree(BUILD / "simdisk/.rockbox", disk / ".rockbox")
    # Only theme assets/fonts are copied from the native ZIP, never its codecs.
    with zipfile.ZipFile(ROOT / "build-m3x/rockbox.zip") as z:
        for name in z.namelist():
            if (name.startswith(".rockbox/wps/m3x-ipod") or
                name == ".rockbox/themes/m3x-ipod.cfg" or
                (name.startswith(".rockbox/fonts/") and "Helvetica" in name)):
                z.extract(name, disk)
    for control in (disk / ".rockbox").glob(".playlist_control*"):
        control.unlink()
    settings = (disk / ".rockbox/themes/m3x-ipod.cfg").read_text()
    settings += ("resume on startup: off\nstart in screen: root\nvoice menus: off\n"
                 "touchscreen mode: point\nvolume: -30\nbacklight timeout: on\n")
    (disk / ".rockbox/config.cfg").write_text(settings)
    (disk / "Music").mkdir()
    for number in range(1, 26):
        shutil.copyfile(ROOT / "media/Kevin MacLeod - Sneaky Snitch.mp3",
                        disk / f"Music/{number:02d} - Sneaky Snitch.mp3")
    flags = subprocess.check_output(["pkg-config", "--cflags", "--libs", "sdl2"], text=True)
    helper = base / "smoke.dylib"
    subprocess.run(["cc", "-dynamiclib", str(ROOT / "tools/m3x/tests/simulator-smoke.c"),
                    *shlex.split(flags), "-o", str(helper)], check=True)
    capture = base / "audio.raw"
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="disk",
               SDL_DISKAUDIOFILE=str(capture), M3X_SMOKE_IPOD="1",
               DYLD_INSERT_LIBRARIES=str(helper))
    result = subprocess.run([str(BUILD / "rockboxui"), "--nobackground", "--root", str(disk)],
                            cwd=base, env=env, capture_output=True, text=True, timeout=40)
    (OUT / "run.txt").write_text(result.stdout + result.stderr)
    dumps = sorted(disk.glob("dump_*.bmp"))
    names = ("menu", "settings", "library", "scrolled", "playing", "paused", "home")
    for source, name in zip(dumps, names):
        with Image.open(source) as im:
            assert im.size == (768, 1280)
            im.save(OUT / f"{name}.png")
    assert result.returncode == 0, result.stderr
    assert len(dumps) == len(names), f"Expected {len(names)} theme captures, got {len(dumps)}"
    with Image.open(OUT / "library.png") as before, Image.open(OUT / "scrolled.png") as after:
        # Compare the filename numbers with selection colors removed; moving
        # the highlight alone must not count as scrolling the list.
        before_digits = [min(p) > 250 for p in before.crop((112,170,160,210)).convert("RGB").getdata()]
        after_digits = [max(p) < 80 for p in after.crop((112,170,160,210)).convert("RGB").getdata()]
        assert sum(a != b for a, b in zip(before_digits, after_digits)) > 20, "Swipe did not move the file list"
    with Image.open(OUT / "playing.png") as playing, Image.open(OUT / "paused.png") as paused:
        assert playing.crop((334,1082,434,1172)).tobytes() != paused.crop((334,1082,434,1172)).tobytes(), "Pause did not change playback state"
    samples = array.array("h", capture.read_bytes())
    assert len(samples) > 44100 * 2 and any(samples), "No decoded audio captured"
    assert (disk / ".rockbox/.playlist_control").stat().st_size > 0
    summary = {"result": "PASS", "screenshots": len(dumps),
               "stereo_frames": len(samples)//2, "audio_output": "file only",
               "output": str(OUT)}
    (OUT / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary))
