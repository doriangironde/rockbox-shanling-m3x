import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


class PlaybackSummaryTests(unittest.TestCase):
    def test_untagged_track_is_playback_but_cleared_metadata_is_not(self):
        with tempfile.TemporaryDirectory() as directory:
            capture = Path(directory)
            blocks = []
            for name, uptime, elapsed, path, locked in (
                ("1", 100, 1000, "/Music/untagged.mp3", 1),
                ("2", 112, 13000, "/Music/untagged.mp3", 1),
                ("3", 234, 135000, "/Music/untagged.mp3", 1),
                ("4", 236, 0, "", 0),
            ):
                stat = ["0"] * 22
                stat[0], stat[11], stat[21] = "S", str(uptime), "128"
                blocks.append(
                    f"=== {name} ===\n{uptime} 0\n"
                    f"10 (rockbox) {' '.join(stat)}\n"
                    "thermal cpu 40 battery 23000\nbacklight 0\n"
                    f"metadata track=-1 frequency={44100 if path else 0} "
                    f"length_ms={136620 if path else 0} elapsed_ms={elapsed} path={path}\n"
                    "tinyalsa handle=abc underruns=0\nobserver_status 0\n"
                    f"screen_locked {locked}\nstate: RUNNING\n"
                    f"hw_ptr : {uptime * 44100}\n"
                    "rate: 44100 (44100/1)\nformat: S16_LE\n"
                    "thermal_service running\nusb_online 1\n"
                )
            blocks.append("=== restored ===\n245 0\nclosed\n")
            (capture / "samples.txt").write_text("".join(blocks))
            (capture / "events.txt").write_text("playback ended sample 4\n")
            (capture / "restored.txt").write_text("".join(
                f"{service} running\n" for service in
                ("zygote", "zygote_secondary", "surfaceflinger", "thermal-engine")
            ))
            (capture / "done").write_text("complete\n")
            expected = capture / "expected.json"
            expected.write_text(json.dumps([{"file": "untagged.mp3"}]))
            result = subprocess.run(
                [sys.executable, str(ROOT / "tools/m3x/summarize-playback-soak.py"),
                 str(capture), "--expected-album", str(expected)],
                capture_output=True, text=True, check=True, timeout=10,
            )
            summary = json.loads(result.stdout)
            self.assertEqual(summary["result"], "PASS", summary["issues"])
            self.assertEqual([track["path"] for track in summary["tracks"]],
                             ["/Music/untagged.mp3"])
            self.assertEqual(summary["tracks"][0]["track"], -1)


if __name__ == "__main__":
    unittest.main()
