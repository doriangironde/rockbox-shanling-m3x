"""The one-boot wrapper must restore the launcher without rewriting its open inode."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]

class BootWrapperTests(unittest.TestCase):
    def test_atomic_restore_runs_bounded_child(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            mod = base / "module"
            mod.mkdir()
            commands = base / "bin"
            commands.mkdir()
            output = base / "boot-probe"
            calls = base / "calls"
            for name, body in {"sleep": "/bin/sleep 0.01", "cat": "exit 0",
                               "getprop": "echo stopped", "pidof": "exit 1"}.items():
                command = commands / name
                command.write_text("#!/bin/sh\n" + body + "\n")
                command.chmod(0o755)
            normal = f'#!/bin/sh\necho launched > "{calls}"\n/bin/sleep 0.05\n'
            (mod / "service-normal.sh").write_text(normal)
            source = (ROOT / "tools/m3x/bounded-boot-service.sh").read_text()
            source = source.replace("/data/local/tmp/m3x-boot-probe.txt", str(output))
            source = source.replace("/data/local/tmp/fbdump", str(base / "absent-fbdump"))
            service = mod / "service.sh"
            service.write_text(source)
            with service.open() as running_inode:
                subprocess.run(["/bin/sh", str(service)],
                               env=dict(os.environ, PATH=f"{commands}:{os.environ['PATH']}"),
                               capture_output=True, check=True, timeout=5)
                self.assertEqual(running_inode.read(), source)
            self.assertEqual(service.read_text(), normal)
            self.assertEqual(calls.read_text().strip(), "launched")
            self.assertTrue((mod / "disable").exists())
            self.assertIn("=== start ===", output.read_text())
