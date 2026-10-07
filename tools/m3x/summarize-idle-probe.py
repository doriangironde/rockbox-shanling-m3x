#!/usr/bin/env python3
"""Summarize idle-probe.sh captures using Linux USER_HZ (100 on the M3X)."""
import argparse
import re
from pathlib import Path


def snapshots(path):
    for section in Path(path).read_text().split("=== ")[1:]:
        name, body = section.split(" ===\n", 1)
        lines = body.splitlines()
        tasks = {}
        processes = {}
        thermal = {}
        for index, line in enumerate(lines):
            match = re.match(r"(\d+) \((.*)\) (.*)", line)
            if match:
                fields = match[3].split()
                stat = (match[2], fields[0], int(fields[11]) + int(fields[12]))
                pid = int(match[1])
                if index + 1 < len(lines) and lines[index + 1].startswith("wchan "):
                    tasks[pid] = stat
                else:
                    processes[pid] = stat
            elif line.startswith("thermal "):
                _, sensor, value = line.split()
                thermal[sensor] = int(value)
        yield {
            "name": name,
            "time": float(lines[0].split()[0]),
            "load": lines[1].split()[0],
            "cpu": list(map(int, next(l for l in lines if l.startswith("cpu ")).split()[1:9])),
            "tasks": tasks,
            "processes": processes,
            "thermal": thermal,
        }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture")
    parser.add_argument("--hz", type=int, default=100)
    args = parser.parse_args()
    baseline = None
    print("Sample          CPU C  Battery C  Load  RB %core  System %  Threads")
    for snap in snapshots(args.capture):
        if snap["name"] == "start":
            baseline = snap
        # A bounded cold boot may wait for Android startup heat to settle.
        # Start CPU accounting at the first snapshot containing Rockbox.
        if snap["tasks"] and (baseline is None or not baseline["tasks"]):
            baseline = snap
        rb = system = "-"
        if (baseline and snap["time"] > baseline["time"]
                and snap["tasks"].keys() == baseline["tasks"].keys()):
            duration = snap["time"] - baseline["time"]
            ticks = sum(stat[2] - baseline["tasks"].get(tid, ("", "", 0))[2]
                        for tid, stat in snap["tasks"].items())
            rb = f"{100 * ticks / args.hz / duration:.2f}"
            delta = [b - a for a, b in zip(baseline["cpu"], snap["cpu"])]
            # The vendor kernel can move aggregate idle counters backwards
            # when cores go offline. Such an interval has no valid ratio.
            if sum(delta) > 0 and all(value >= 0 for value in delta):
                system = f"{100 * (sum(delta) - delta[3] - delta[4]) / sum(delta):.2f}"
        cpu = snap["thermal"].get("tsens_tz_sensor0", 0)
        battery = snap["thermal"].get("battery", 0) / 1000
        print(f"{snap['name']:<15} {cpu:5} {battery:10.1f} {snap['load']:>5} "
              f"{rb:>9} {system:>9} {len(snap['tasks']):>8}")


if __name__ == "__main__":
    main()
