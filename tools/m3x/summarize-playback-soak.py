#!/usr/bin/env python3
"""Summarize native playback evidence; never equate telemetry with listening."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("directory", type=Path)
parser.add_argument("--expected-album", type=Path)
args = parser.parse_args()
directory = args.directory
samples = []
for block in (directory / "samples.txt").read_text().split("=== ")[1:]:
    lines = block.splitlines()
    record = {"name": lines[0].split()[0], "uptime": float(lines[1].split()[0])}
    for line in lines:
        if match := re.fullmatch(r"thermal cpu (\d+) battery (\d+)", line):
            record.update(cpu_c=int(match[1]), battery_c=int(match[2]) / 1000)
        elif match := re.fullmatch(r"backlight (\d+)", line):
            record["backlight"] = int(match[1])
        elif match := re.fullmatch(r"screen_locked (\d+)", line):
            record["locked"] = int(match[1])
        elif match := re.fullmatch(r"tinyalsa handle=([0-9a-f]+) underruns=(-?\d+)", line):
            record.update(handle=match[1], underruns=int(match[2]))
        elif match := re.fullmatch(r"observer_status (\d+)", line):
            record["observer_status"] = int(match[1])
        elif match := re.fullmatch(r"metadata track=(\d+) frequency=(\d+) length_ms=(\d+) elapsed_ms=(\d+) path=(.*)", line):
            record.update(track=int(match[1]), frequency=int(match[2]), length_ms=int(match[3]),
                          elapsed_ms=int(match[4]), path=match[5])
        elif match := re.fullmatch(r"state:\s+(\S+)", line):
            record["pcm_state"] = match[1]
        elif match := re.fullmatch(r"rate:\s+(\d+) .*", line):
            record["output_rate"] = int(match[1])
        elif match := re.fullmatch(r"format:\s+(\S+)", line):
            record["output_format"] = match[1]
        elif match := re.fullmatch(r"hw_ptr\s+: (\d+)", line):
            record["hw_ptr"] = int(match[1])
        elif match := re.fullmatch(r"(\d+) \(rockbox\) (.*)", line):
            fields = match[2].split()
            record.update(pid=int(match[1]), ticks=int(fields[11]) + int(fields[12]),
                          rss_kib=int(fields[21]) * 4)
        elif line.startswith("usb_online "):
            record["usb_online"] = int(line.split()[-1])
        elif line.startswith("thermal_service "):
            record["thermal_service"] = line.split()[-1]
    samples.append(record)

playing = [s for s in samples if s.get("pcm_state") == "RUNNING" and s.get("track", 0) > 0]
tracks = []
for sample in playing:
    if not tracks or tracks[-1]["path"] != sample["path"]:
        tracks.append({"track": sample["track"], "path": sample["path"],
                       "frequency": sample["frequency"], "length_ms": sample["length_ms"],
                       "first_elapsed_ms": sample["elapsed_ms"], "first_uptime": sample["uptime"],
                       "last_elapsed_ms": sample["elapsed_ms"], "last_uptime": sample["uptime"],
                       "samples": 1})
    else:
        tracks[-1].update(last_elapsed_ms=sample["elapsed_ms"], last_uptime=sample["uptime"])
        tracks[-1]["samples"] += 1

discontinuities = []
stalls = []
for before, after in zip(playing, playing[1:]):
    dt = after["uptime"] - before["uptime"]
    if before.get("path") == after.get("path"):
        progress = after["elapsed_ms"] - before["elapsed_ms"]
        if progress < -500 or abs(progress - dt * 1000) > 2000:
            discontinuities.append({"sample": after["name"], "wall_seconds": dt,
                                    "progress_ms": progress, "track": after["track"]})
    if before.get("handle") == after.get("handle") and before.get("hw_ptr") == after.get("hw_ptr"):
        stalls.append(after["name"])

settled_locked = []
locked_since = None
for sample in samples:
    if sample.get("locked") == 1:
        if locked_since is None:
            locked_since = sample["uptime"]
        if sample["uptime"] - locked_since >= 10:
            settled_locked.append(sample)
    else:
        locked_since = None

restored = (directory / "restored.txt").read_text() if (directory / "restored.txt").exists() else ""
events = (directory / "events.txt").read_text() if (directory / "events.txt").exists() else ""
log = (directory / "player.log").read_text() if (directory / "player.log").exists() else ""
issues = []
if not (directory / "done").exists():
    issues.append("Capture is not complete")
if not playing:
    issues.append("No playing samples")
if playing:
    interval = [s for s in samples if playing[0]["uptime"] <= s["uptime"] <= playing[-1]["uptime"]]
    if any(s.get("pcm_state") != "RUNNING" for s in interval):
        issues.append("PCM left RUNNING between the first and last playing samples")
    if any(s.get("thermal_service") != "running" for s in interval):
        issues.append("Android thermal service unavailable during playback")
if discontinuities:
    issues.append("Playback elapsed-time discontinuities (manual seek or fault)")
if stalls:
    issues.append("Repeated PCM hardware pointers")
if any(s.get("underruns", 0) > 0 for s in samples):
    issues.append("tinyalsa reported underruns")
if playing and any("underruns" not in s or s["underruns"] < 0 for s in playing):
    issues.append("Underrun counter unavailable during playback")
if any(s.get("observer_status", 0) != 0 for s in samples):
    issues.append("Read-only observer missed samples")
if any(s["backlight"] != 0 for s in settled_locked):
    issues.append("Backlight lit while native screen lock was settled")
if not settled_locked:
    issues.append("No settled native screen-lock evidence")
if any(s.get("locked") != 1 for s in playing[5:]):
    issues.append("Native screen lock was released during the album")
if "playback ended sample " not in events:
    issues.append("Natural playlist completion not recorded")
if any(f"{name} running" not in restored for name in
       ("zygote", "zygote_secondary", "surfaceflinger", "thermal-engine")):
    issues.append("Android restoration not fully verified")
if any(s.get("pid") for s in samples if s["name"] == "restored"):
    issues.append("Rockbox remained after restoration")
if re.search(r"M3X PCM .*failed|thermal stop|rockbox exited \((?!137\))", log):
    issues.append("Player error or unexpected exit (inspect player.log)")
if args.expected_album:
    expected = json.loads(args.expected_album.read_text())
    if [Path(t["path"]).name for t in tracks] != [t["file"] for t in expected]:
        issues.append("Observed track order differs from expected full album")
    for track in tracks:
        if track["first_elapsed_ms"] > 5000 or track["length_ms"] - track["last_elapsed_ms"] > 5000:
            issues.append(f"Track {track['track']} not observed from near start to near end")

screen_issue_names = {"Backlight lit while native screen lock was settled",
                      "No settled native screen-lock evidence",
                      "Native screen lock was released during the album"}
playback_issues = [issue for issue in issues if issue not in screen_issue_names]
screen_issues = [issue for issue in issues if issue in screen_issue_names]
result = ("PASS" if not issues else "PARTIAL" if not playback_issues and settled_locked else
          "INCOMPLETE_OR_FAILED")
summary = {"result": result, "issues": issues,
           "continuous_playback_result": "PASS" if not playback_issues else "INCOMPLETE_OR_FAILED",
           "screen_off_result": "PASS" if not screen_issues else "PARTIAL" if settled_locked else "UNVERIFIED",
           "samples": len(samples), "playing_samples": len(playing), "tracks": tracks,
           "tinyalsa_underruns_max": max((s.get("underruns", -1) for s in samples), default=-1),
           "pcm_states": dict(Counter(s.get("pcm_state", "closed") for s in samples)),
           "settled_locked_samples": len(settled_locked),
           "settled_locked_backlight_nonzero": sum(s["backlight"] != 0 for s in settled_locked),
           "elapsed_discontinuities": discontinuities, "repeated_hw_pointers": stalls,
           "output_rates": sorted({s["output_rate"] for s in playing if "output_rate" in s}),
           "output_formats": sorted({s["output_format"] for s in playing if "output_format" in s}),
           "usb_online_values": sorted({s["usb_online"] for s in playing if "usb_online" in s}),
           "battery_drain_test": False, "analog_audio_dropout_test": False}
if playing:
    first, last = playing[0], playing[-1]
    summary.update(playing_observation_seconds=round(last["uptime"] - first["uptime"], 2),
                   cpu_c=[min(s["cpu_c"] for s in playing), max(s["cpu_c"] for s in playing)],
                   battery_c=[min(s["battery_c"] for s in playing), max(s["battery_c"] for s in playing)],
                   rss_kib=[min(s["rss_kib"] for s in playing), max(s["rss_kib"] for s in playing)])
    if first["pid"] == last["pid"] and last["uptime"] > first["uptime"]:
        summary["rockbox_percent_one_core"] = round((last["ticks"] - first["ticks"]) /
                                                    (last["uptime"] - first["uptime"]), 2)
    dark_seconds = 0.0
    longest_dark = 0.0
    dark_started = None
    for sample in playing:
        if sample.get("locked") == 1 and sample.get("backlight") == 0:
            if dark_started is None:
                dark_started = sample["uptime"]
            longest_dark = max(longest_dark, sample["uptime"] - dark_started)
        else:
            dark_started = None
    for before, after in zip(playing, playing[1:]):
        if all(s.get("locked") == 1 and s.get("backlight") == 0 for s in (before, after)):
            dark_seconds += after["uptime"] - before["uptime"]
    summary["screen_off_seconds_observed"] = round(dark_seconds, 2)
    summary["longest_screen_off_run_seconds"] = round(longest_dark, 2)
    summary["source_output_rate_pairs"] = sorted({(s["frequency"], s["output_rate"])
                                                 for s in playing if "output_rate" in s})
(directory / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
print(json.dumps(summary, indent=2))
