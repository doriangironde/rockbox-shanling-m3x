"""Check the detached manual session keeps boot disabled and owns its lock."""
import os
from pathlib import Path
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[3]

class ManualLaunchTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.mod = self.base / 'module'
        self.mod.mkdir()
        self.state = self.base / 'state'
        self.bin = self.base / 'bin'
        self.bin.mkdir()
        self.calls = self.base / 'calls'
        self.executable(self.bin / 'id', 'echo 0')
        self.executable(self.bin / 'pidof', 'exit 1')
        self.executable(self.bin / 'sleep', '/bin/sleep 0.1')
        self.executable(self.bin / 'setsid', 'exec "$@"')
        self.executable(self.mod / 'rockbox', 'exit 0')
        self.executable(self.base / 'fbpan', 'exit 0')
        self.executable(self.mod / 'service.sh',
            f'test -e "{self.mod}/disable" || exit 9\n'
            f'test ! -e "${{0%/*}}/disable" || exit 8\n'
            f'echo launched >> "{self.calls}"\ntouch "${{0%/*}}/disable"')
        source = (ROOT / 'm3x-module/launch.sh').read_text()
        source = source.replace('/data/adb/rockbox-m3x-manual', str(self.state))
        source = source.replace('/data/local/tmp/rockbox-m3x-manual.log', str(self.base / 'log'))
        source = source.replace('/data/local/tmp/fbpan', str(self.base / 'fbpan'))
        source = source.replace('/system/bin/sh', '/bin/sh')
        self.executable(self.mod / 'launch.sh', source)
        self.env = dict(os.environ, PATH=f'{self.bin}:{os.environ["PATH"]}')

    def executable(self, path, text):
        path.write_text('#!/bin/sh\n' + text + '\n')
        path.chmod(0o755)

    def launch(self):
        return subprocess.run(['/bin/sh', str(self.mod / 'launch.sh')],
            env=self.env, capture_output=True, text=True, timeout=3)

    def wait_done(self):
        deadline = time.monotonic() + 3
        while not self.calls.exists() or (self.state / 'launch.lock').exists():
            self.assertLess(time.monotonic(), deadline)
            time.sleep(0.01)

    def test_one_time_launch_and_relaunch_keep_boot_disabled(self):
        for _ in range(2):
            self.assertEqual(self.launch().returncode, 0)
            self.wait_done()
            self.assertTrue((self.mod / 'disable').exists())
        self.assertEqual(self.calls.read_text(), 'launched\nlaunched\n')

    def test_second_request_cannot_clear_a_live_launch_lock(self):
        self.assertEqual(self.launch().returncode, 0)
        second = self.launch()
        self.assertNotEqual(second.returncode, 0)
        self.assertIn('already starting', second.stderr)
        self.wait_done()
        self.assertEqual(self.calls.read_text(), 'launched\n')

    def test_stale_lock_is_recovered(self):
        lock = self.state / 'launch.lock'
        lock.mkdir(parents=True)
        (lock / 'pid').write_text('99999999\n')
        self.assertEqual(self.launch().returncode, 0)
        self.wait_done()

    def test_incomplete_installation_does_not_take_over_android(self):
        (self.mod / 'rockbox').unlink()
        result = self.launch()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('incomplete', result.stderr)
        self.assertFalse(self.calls.exists())
