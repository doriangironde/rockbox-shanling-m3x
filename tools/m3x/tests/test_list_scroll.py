"""Replay kinetic ownership and late redraws against the real list controller."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


class ListScrollTests(unittest.TestCase):
    def test_gui_owns_kinetic_state_and_late_redraws_are_safe(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "kinetic-scroll"
            (Path(directory) / "autoconf.h").write_text(
                "#define ARCH_NONE 0\n#define ARCH_ARM 1\n#define ARCH_ARM64 2\n"
                "#define ARCH_MIPS 3\n#define ARCH_M68K 4\n#define ARCH_X86 5\n"
                "#define ARCH_AMD64 6\n#define ARCH ARCH_NONE\n#define GCCNUM 1602\n"
                "#define HAVE_SDL_THREADS\n#define ROCKBOX_LITTLE_ENDIAN 1\n"
                '#define ROCKBOX_DIR "/.rockbox"\n')
            (Path(directory) / "sysfont.h").write_text("")
            (Path(directory) / "lang.h").write_text(
                "extern unsigned char *language_strings[];\n"
                "#define str(x) language_strings[x]\n#define VOICEONLY_DELIMITER 0\n")
            includes = ("rockbox/apps", "rockbox/apps/gui",
                        "rockbox/apps/recorder", "rockbox/firmware/export",
                        "rockbox/firmware/include", "rockbox/firmware/kernel/include",
                        "rockbox/firmware/target/hosted/sdl",
                        "rockbox/firmware/target/hosted/ibasso/m3x",
                        "rockbox/firmware/target/hosted/ibasso",
                        "rockbox/firmware/target/hosted", "rockbox/lib/fixedpoint",
                        "rockbox/lib/rbcodec", "rockbox/lib/rbcodec/metadata",
                        "rockbox/lib/rbcodec/dsp", "rockbox/lib/skin_parser")
            linker = "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"
            subprocess.run(["cc", "-std=gnu11", "-O1", "-g", "-ffunction-sections",
                            "-fdata-sections", "-fsanitize=address,undefined",
                            "-fno-sanitize-recover=all", "-DSHANLING_M3X", "-DSIMULATOR",
                            "-DROCKBOX", "-DMEMORYSIZE=128",
                            "-I" + directory,
                            *("-I" + str(ROOT / path) for path in includes),
                            str(ROOT / "tools/m3x/tests/kinetic-scroll.c"),
                            str(ROOT / "rockbox/apps/gesture.c"), linker,
                            "-o", str(output)], check=True)
            subprocess.run([str(output)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
