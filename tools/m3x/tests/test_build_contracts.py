"""Validate the production build's paths, audio ABI and memory budget."""
from pathlib import Path
import re
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[3]
BUILD = ROOT / "build-m3x"
READELF = BUILD / "android-toolchain/bin/aarch64-linux-android-readelf"


class BuildContractTests(unittest.TestCase):
    def test_player_is_android_arm64_pie_with_bounded_audio_memory(self):
        header = subprocess.check_output([str(READELF), "-h", str(BUILD / "rockbox")], text=True)
        self.assertIn("AArch64", header)
        self.assertRegex(header, r"Type:\s+DYN")
        symbols = subprocess.check_output([str(READELF), "-W", "--syms", str(BUILD / "rockbox")], text=True)
        buffers = [line.split() for line in symbols.splitlines() if line.endswith(" audiobuffer")]
        self.assertTrue(buffers)
        self.assertEqual(int(buffers[0][2], 0), 127 * 1024 * 1024)

    def test_production_pcm_and_native_paths(self):
        cc = BUILD / "android-toolchain/bin/aarch64-linux-android-gcc"
        command = [str(cc), "-dM", "-E", "-x", "c", "-DSHANLING_M3X", "-DAPPLICATION", "-DROCKBOX",
                   "-DMEMORYSIZE=128", "-I" + str(BUILD),
                   "-I" + str(ROOT / "rockbox/firmware/export"),
                   "-I" + str(ROOT / "rockbox/firmware/target/hosted/ibasso/m3x"),
                   "-include", "audiohw.h", "-include", "rbpaths.h", "-"]
        output = subprocess.check_output(command, input="", text=True)
        macros = dict(re.findall(r"^#define (\w+)(?: (.*))?$", output, re.M))
        self.assertEqual(macros["PCM_NATIVE_BITDEPTH"], "16")
        self.assertEqual(macros["PCM_SW_VOLUME_FRACBITS"], "16")
        self.assertNotIn("WANT_SWVOL_32", macros)
        self.assertIn("AUDIOHW_HAVE_FILTER_ROLL_OFF", macros)
        self.assertNotIn("AUDIOHW_HAVE_MONO_VOLUME", macros)
        self.assertNotIn("HAVE_SPECIAL_DIRS", macros)
        self.assertEqual(macros["PIVOT_ROOT"], '"/data/media/0"')

    def test_all_modules_have_no_text_relocations_or_executable_stack(self):
        modules = [BUILD / "rockbox"]
        modules += list((BUILD / "lib/rbcodec/codecs").rglob("*.codec"))
        modules += list((BUILD / "apps/plugins").rglob("*.rock"))
        for module in modules:
            with self.subTest(module=module.name):
                result = subprocess.check_output([str(READELF), "-W", "-l", "-d", str(module)], text=True)
                self.assertNotIn("TEXTREL", result)
                stack = next(line for line in result.splitlines() if "GNU_STACK" in line)
                self.assertNotIn("RWE", stack)
