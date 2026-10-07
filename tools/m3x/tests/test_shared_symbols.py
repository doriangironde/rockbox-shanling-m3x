"""Check the production ARM64 modules for API-symbol interposition hazards."""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[3]
READELF = ROOT / "build-m3x/android-toolchain/bin/aarch64-linux-android-readelf"


class SharedSymbolTests(unittest.TestCase):
    def test_codec_and_plugin_api_pointers_are_private(self):
        codecs = list((ROOT / "build-m3x/lib/rbcodec/codecs").rglob("*.codec"))
        plugins = list((ROOT / "build-m3x/apps/plugins").rglob("*.rock"))
        self.assertTrue(codecs, "Build the M3X codecs first")
        self.assertTrue(plugins, "Build the M3X plugins first")
        private = {"ci", "rb", "memcpy", "memmove", "memset", "strlen", "strcpy"}
        for module in sorted(codecs + plugins):
            with self.subTest(module=module.name):
                output = subprocess.check_output([str(READELF), "-W", "--dyn-syms", str(module)], text=True)
                exported = {}
                for line in output.splitlines():
                    fields = line.split()
                    if len(fields) >= 8 and fields[0].endswith(":"):
                        if fields[4] in ("GLOBAL", "WEAK") and fields[6] != "UND":
                            exported[fields[7]] = fields[5]
                self.assertEqual(exported.get("__header"), "DEFAULT", "Loader header must stay discoverable")
                self.assertEqual(private.intersection(exported), set(),
                                 "Private API pointers/helpers must not interpose")


if __name__ == "__main__":
    unittest.main()
