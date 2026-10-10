#!/usr/bin/env python3
"""Generate quiet synthetic stereo fixtures; never copy user music."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

out = Path(sys.argv[1] if len(sys.argv) > 1 else "/tmp/m3x-format-fixtures").resolve()
out.mkdir(parents=True, exist_ok=True)
formats = [("mp3", 44100), ("flac", 48000), ("flac", 96000), ("flac", 192000),
           ("mp3", 32000), ("flac", 44100), ("flac", 88200), ("flac", 176400)]
manifest = []
for index, (extension, rate) in enumerate(formats, 1):
    name = f"{index:02d} {extension.upper()} {rate}Hz.{extension}"
    command = ["ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
               f"sine=frequency=440:sample_rate={rate}:duration=12", "-ac", "2",
               "-metadata", "artist=M3X diagnostics", "-metadata", f"title={extension.upper()} {rate} Hz",
               "-metadata", f"track={index}", "-metadata", "album=M3X Format Test"]
    command += (["-c:a", "libmp3lame", "-b:a", "192k"] if extension == "mp3" else
                ["-c:a", "flac", "-sample_fmt", "s32", "-bits_per_raw_sample", "24"])
    subprocess.run(command + [str(out / name)], check=True)
    info = json.loads(subprocess.check_output([
        "ffprobe", "-v", "error", "-select_streams", "a:0", "-show_entries",
        "stream=sample_rate,channels,bits_per_raw_sample:format=duration", "-of", "json",
        str(out / name)], text=True))
    assert int(info["streams"][0]["sample_rate"]) == rate
    assert info["streams"][0]["channels"] == 2
    manifest.append({"file": name, "sample_rate": rate, "channels": 2,
                     "seconds": float(info["format"]["duration"]),
                     "bits": int(info["streams"][0].get("bits_per_raw_sample", 0)),
                     "sha256": hashlib.sha256((out / name).read_bytes()).hexdigest()})
(out / "format-test.m3u8").write_text("#EXTM3U\n" + "\n".join(x["file"] for x in manifest) + "\n")
(out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
print(f"Created {len(manifest)} tracks and an M3U8 playlist in {out}")
