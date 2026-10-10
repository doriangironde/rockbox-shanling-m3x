#!/usr/bin/env python3
"""Build a read-only observer tied to the current native ELF and ABI."""
import hashlib
import json
from pathlib import Path
import shlex
import struct
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
build = root / "build-m3x"
out = Path(sys.argv[1] if len(sys.argv) > 1 else "/tmp/m3x-observer-build").resolve()
out.mkdir(parents=True, exist_ok=True)
cc = build / "android-toolchain/bin/aarch64-linux-android-gcc"
objcopy = build / "android-toolchain/bin/aarch64-linux-android-objcopy"

with tempfile.TemporaryDirectory() as directory:
    temporary = Path(directory)
    makefile = temporary / "Makefile"
    makefile.write_text(f"include {build}/Makefile\n"
                        "probe-flags: ; @printf '%s\\n' '$(CC) $(CFLAGS)'\n")
    result = subprocess.check_output(
        ["make", "-s", "NODEPS=1", "-f", str(makefile), "probe-flags"], cwd=build, text=True)
    flags = shlex.split(result.splitlines()[-1])
    flags = flags[next(i for i, token in enumerate(flags) if token == str(cc)):]

    def extract(name, source, compiler_flags):
        src = temporary / f"{name}.c"
        obj = temporary / f"{name}.o"
        raw = temporary / f"{name}.bin"
        src.write_text(source)
        subprocess.run(compiler_flags + ["-c", str(src), "-o", str(obj)], cwd=build, check=True)
        subprocess.run([str(objcopy), "-j", ".m3x_layout", "-O", "binary", str(obj), str(raw)], check=True)
        data = raw.read_bytes()
        return struct.unpack(f"<{len(data) // 8}Q", data)

    fields = ["sizeof(struct mp3entry)", "offsetof(struct mp3entry, path)",
              "sizeof(((struct mp3entry*)0)->path)", "offsetof(struct mp3entry, tracknum)",
              "offsetof(struct mp3entry, frequency)", "offsetof(struct mp3entry, length)",
              "offsetof(struct mp3entry, elapsed)"]
    declaration = 'const unsigned long layout[] __attribute__((section(".m3x_layout"))) = '
    values = extract("id3", '#include "config.h"\n#include "metadata.h"\n#include <stddef.h>\n' +
                     declaration + "{" + ",".join(fields) + "};\n", flags)
    underruns, = extract("pcm", '#include <stddef.h>\n#include <sys/types.h>\n#undef __bitwise\n#include "' +
                          str(root / "rockbox/firmware/target/hosted/tinyalsa/pcm.c") + '"\n' +
                          declaration + "{offsetof(struct pcm, underruns)};\n",
                          [str(cc), "-I" + str(root / "rockbox/firmware/target/hosted/tinyalsa/include")])
    nm = subprocess.check_output([str(build / "android-toolchain/bin/aarch64-linux-android-nm"),
                                  str(build / "rockbox")], text=True)
    symbols = {}
    for line in nm.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] in ("static_id3_entries", "_alsa_handle", "screen_locked"):
            symbols[parts[2]] = int(parts[0], 16)
    layout = dict(zip(("SIZE", "PATH", "PATH_SIZE", "TRACK", "FREQUENCY", "LENGTH", "ELAPSED"), values))
    layout.update(ID3=symbols["static_id3_entries"], PCM=symbols["_alsa_handle"],
                  UNDERRUNS=underruns, LOCKED=symbols["screen_locked"])
    (out / "observe-layout.h").write_text("".join(f"#define OBS_{k} {v}UL\n" for k, v in layout.items()))

subprocess.run([str(cc), "-std=gnu11", "-O2", "-fPIE", "-pie", "-Wall", "-Wextra", "-Werror",
                "-I" + str(out), str(root / "tools/m3x/observe-playback.c"),
                "-o", str(out / "m3x-playback-observer")], check=True)
binary = (build / "rockbox").read_bytes()
manifest = {"binary_sha256": hashlib.sha256(binary).hexdigest(),
            "binary_md5": hashlib.md5(binary).hexdigest(), "layout": layout,
            "method": "read-only /proc/PID/mem; no ptrace attachment or process writes"}
(out / "observer-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
(out / "m3x-playback-observer.md5").write_text(manifest["binary_md5"] + "\n")
print(json.dumps(manifest, indent=2))
