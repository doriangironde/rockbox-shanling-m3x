"""Boot and decode the test song through the M3X simulator, with silent output."""
from pathlib import Path
import array
import json
import os
import shlex
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build-m3x-sim"
EVIDENCE = ROOT / "validation/offline-20261007-batch/simulator"
EVIDENCE.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix="m3x-simulator-") as directory:
    base = Path(directory)
    disk = base / "simdisk"
    disk.mkdir()
    shutil.copytree(BUILD / "simdisk/.rockbox", disk / ".rockbox")
    # Only the temporary copy is reset; development/user files are untouched.
    for control in (disk / ".rockbox").glob(".playlist_control*"):
        control.unlink()
    (disk / ".rockbox/config.cfg").write_text(
        "resume on startup: off\nstart in screen: root\nvoice menus: off\n"
        "touchscreen mode: point\nvolume: -30\nbacklight timeout: on\n"
        "font: /.rockbox/fonts/35-Adobe-Helvetica.fnt\n")
    (disk / "Music").mkdir()
    shutil.copyfile(ROOT / "media/Kevin MacLeod - Sneaky Snitch.mp3",
                    disk / "Music/Kevin MacLeod - Sneaky Snitch.mp3")
    flags = subprocess.check_output(["pkg-config", "--cflags", "--libs", "sdl2"], text=True)
    helper = base / "smoke.dylib"
    subprocess.run(["cc", "-dynamiclib", str(ROOT / "tools/m3x/tests/simulator-smoke.c"),
                    *shlex.split(flags), "-o", str(helper)], check=True)
    capture = base / "output.raw"
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="disk",
               SDL_DISKAUDIOFILE=str(capture), M3X_SMOKE_PLAYBACK="1",
               DYLD_INSERT_LIBRARIES=str(helper))
    result = subprocess.run([str(BUILD / "rockboxui"), "--nobackground", "--root", str(disk)],
                            cwd=base, env=env, capture_output=True, text=True, timeout=30, check=True)
    log = result.stdout + result.stderr
    (EVIDENCE / "run.txt").write_text(log)
    assert "Codec: entering run state" in log and "Codec: cleaning up" in log, log
    assert "Error accessing playlist" not in log, log
    samples = array.array("h", capture.read_bytes())
    assert len(samples) > 44100 * 2 and any(samples), "No decoded music captured"
    dumps = sorted(disk.glob("dump_*.bmp"))
    assert len(dumps) == 2, dumps
    for source, name in zip(dumps, ("menu.bmp", "playing.bmp")):
        data = source.read_bytes()
        assert struct.unpack_from("<ii", data, 18) == (768, 1280)
        shutil.copyfile(source, EVIDENCE / name)
    assert (disk / ".rockbox/.playlist_control").stat().st_size > 0
    summary = {"result": "PASS", "audio_output": "file only; no audible playback",
               "stereo_frames": len(samples) // 2,
               "nonzero_samples": sum(value != 0 for value in samples),
               "peak": max(map(abs, samples)), "screenshots": 2,
               "playlist_control": "created successfully"}
    (EVIDENCE / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary))
