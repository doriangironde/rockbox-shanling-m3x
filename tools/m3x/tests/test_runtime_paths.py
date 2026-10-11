"""Compile production path/loader code on host; mock metadata and linker only."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
FIRMWARE = ROOT / 'rockbox/firmware'


class RuntimePathTests(unittest.TestCase):
    def test_private_runtime_translation_and_loader_trust_gate(self):
        with tempfile.TemporaryDirectory(prefix='m3x-runtime-') as temporary:
            output = Path(temporary) / 'runtime-test'
            includes = [ROOT / 'build-m3x', FIRMWARE, FIRMWARE / 'export',
                        FIRMWARE / 'include', FIRMWARE / 'kernel/include',
                        FIRMWARE / 'target/hosted', FIRMWARE / 'target/hosted/ibasso',
                        FIRMWARE / 'target/hosted/ibasso/m3x']
            command = ['cc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                       '-D_FORTIFY_SOURCE=0', '-DSHANLING_M3X', '-DAPPLICATION',
                       '-DROCKBOX', '-DMEMORYSIZE=128', '-ffunction-sections', '-fdata-sections',
                       '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                       '-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections']
            command += ['-I' + str(path) for path in includes]
            command += [str(ROOT / 'tools/m3x/tests/runtime-paths.c'),
                        str(FIRMWARE / 'target/hosted/filesystem-app.c'),
                        str(FIRMWARE / 'common/pathfuncs.c'),
                        str(FIRMWARE / 'common/strmemccpy.c'),
                        str(FIRMWARE / 'common/strlcpy.c'),
                        str(FIRMWARE / 'common/strlcat.c'), '-o', str(output)]
            subprocess.run(command, check=True)
            result = subprocess.run([str(output)], check=True, capture_output=True,
                                    text=True, timeout=10)
            self.assertIn('PASS: private paths', result.stdout)
