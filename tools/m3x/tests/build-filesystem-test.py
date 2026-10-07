#!/usr/bin/env python3
"""Compile the real filesystem adapter with the production build's flags."""
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
root = Path(__file__).resolve().parents[3]
build = root / "build-m3x"
source = root / "rockbox/firmware/target/hosted/filesystem-app.c"
recipe = "m3x-fs-test-flags: ; @printf '%s\\n' '$(CC) $(CFLAGS)'"
with tempfile.TemporaryDirectory() as temporary:
    makefile = Path(temporary) / "Makefile"
    makefile.write_text("include " + str(build / "Makefile") + "\n" + recipe + "\n")
    result = subprocess.run(["make", "-s", "NODEPS=1", "-f", str(makefile), "m3x-fs-test-flags"],
                            cwd=build, capture_output=True, text=True, check=True)
command = shlex.split(result.stdout.splitlines()[-1])
index = next(i for i, token in enumerate(command) if token.endswith("aarch64-linux-android-gcc"))
command = command[index:]
output = sys.argv[1] if len(sys.argv) > 1 else "/tmp/m3x-filesystem-test"
command += ["-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-pie",
            str(root / "tools/m3x/tests/filesystem.c"), str(source),
            str(root / "rockbox/firmware/target/hosted/filesystem-unix.c"),
            str(root / "rockbox/firmware/common/pathfuncs.c"), "-o", output]
subprocess.run(command, cwd=build, check=True)
