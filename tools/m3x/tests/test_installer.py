"""Exercise the real device installer against a harmless fake Android tree."""
import hashlib
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


class InstallerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='m3x installer ')
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.bundle = self.base / 'bundle'
        self.bundle.mkdir()
        self.bin = self.base / 'bin'
        self.bin.mkdir()
        self.mod = self.base / 'data/adb/modules/rockbox_m3x'
        self.assets = self.base / 'data/media/0/.rockbox'
        self.helper = self.base / 'data/local/tmp/fbpan'
        self.backups = self.base / 'data/adb/rockbox-m3x-backups'
        self.card = self.base / 'data/media/0/microSD'
        self.apk = self.base / 'installed.apk'
        for p, data in {
            self.mod / 'rockbox': b'old-player', self.mod / 'disable': b'',
            self.mod / 'service.sh': b'old-service', self.assets / 'config.cfg': b'volume: -43\n',
            self.assets / 'custom playlist.m3u8': b'/Music/my-song.mp3\n',
            self.assets / '.playlist_control': b'old-resume', self.assets / 'codecs/mpa.codec': b'old-codec',
            self.helper: b'old-helper', self.apk: b'old-apk',
            self.base / 'data/media/0/Music/my-song.mp3': b'music-is-not-an-install-asset',
            self.base / 'dev/graphics/fb0': b'', self.base / 'dev/snd/pcmC0D0p': b'',
        }.items():
            self.put(p, data)
        self.helper.chmod(0o755)
        for name, data in {
            'payload/rockbox': b'new-player', 'payload/fbpan': b'new-helper',
            'payload/Rockbox-M3X.apk': b'new-apk', 'payload/release-id': b'dev-test\n',
            'payload/assets/.rockbox/codecs/mpa.codec': b'new-codec',
            **{'payload/module/' + n: b'new-' + n.encode() for n in
               ('launch.sh', 'service.sh', 'post-fs-data.sh', 'module.prop')},
        }.items():
            self.put(self.bundle / name, data)
        source = (ROOT / 'tools/m3x/install.sh').read_text()
        for old, new in [('/data/', str(self.base / 'data') + '/'),
                         ('/dev/graphics/fb0', str(self.base / 'dev/graphics/fb0')),
                         ('/dev/snd/pcmC0D0p', str(self.base / 'dev/snd/pcmC0D0p')),
                         ('/sbin/.magisk/busybox/busybox', str(self.bin / 'busybox'))]:
            source = source.replace(old, new)
        self.script = self.bundle / 'install.sh'
        self.script.write_text(source)
        self.command('id', 'echo 0')
        self.command('uname', 'echo aarch64')
        self.command('chown', 'exit 0')
        self.command('pidof', f'test -e "{self.base}/running"')
        self.command('getprop', f'''case "$1" in
ro.product.manufacturer) echo Shanling;;
ro.product.model) echo 'Shanling M3X';;
ro.build.version.sdk) echo 25;;
ro.build.display.id) if [ -f "{self.base}/wrong-firmware" ]; then echo 1.74; else echo 1.75; fi;;
*) echo running;; esac''')
        sha = shutil.which('sha256sum')
        checksum = [sha] if sha else [shutil.which('shasum'), '-a', '256']
        self.python_command('busybox', f'''import subprocess, sys
from pathlib import Path
flag = Path({str(self.base / 'fail-stage')!r})
if sys.argv[1] == 'cp' and flag.exists() and sys.argv[-2].endswith('/new-module'):
    Path(sys.argv[-1]).mkdir()
    flag.unlink()
    sys.exit(1)
command = {checksum!r} if sys.argv[1] == 'sha256sum' else [sys.argv[1]]
sys.exit(subprocess.call(command + sys.argv[2:]))''')
        self.python_command('pm', f'''from pathlib import Path
import shutil, sys
base = Path({str(self.base)!r})
apk = base / 'installed.apk'
if sys.argv[1] == 'path':
    if apk.exists(): print('package:' + str(apk))
elif sys.argv[1] == 'install':
    src = Path(sys.argv[-1])
    if (base / 'fail-pm').exists() and src.read_bytes() == b'new-apk':
        print('Failure [injected failure]')
    else:
        shutil.copyfile(src, apk)
        print('Success')
elif sys.argv[1] == 'uninstall':
    apk.unlink(missing_ok=True)
    print('Success')''')
        self.env = dict(os.environ, PATH=str(self.bin) + os.pathsep + os.environ['PATH'])
        self.checksums()

    def put(self, path, data):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def command(self, name, body):
        path = self.bin / name
        path.write_text('#!/bin/sh\n' + body + '\n')
        path.chmod(0o755)

    def python_command(self, name, body):
        path = self.bin / name
        path.write_text('#!/usr/bin/env python3\n' + body + '\n')
        path.chmod(0o755)

    def checksums(self):
        self.bundle.joinpath('SHA256SUMS').write_text(''.join(
            hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + p.relative_to(self.bundle).as_posix() + '\n'
            for p in sorted(self.bundle.rglob('*')) if p.is_file() and p.name != 'SHA256SUMS'))

    def run_install(self, action='install', *extra):
        return subprocess.run(['/bin/sh', str(self.script), action, *map(str, extra)],
                              env=self.env, capture_output=True, text=True, timeout=15)

    def snapshot(self):
        return next(self.backups.iterdir())

    def assert_old(self):
        self.assertEqual((self.mod / 'rockbox').read_bytes(), b'old-player')
        self.assertEqual((self.assets / 'config.cfg').read_bytes(), b'volume: -43\n')
        self.assertEqual((self.assets / 'codecs/mpa.codec').read_bytes(), b'old-codec')
        self.assertEqual(self.helper.read_bytes(), b'old-helper')
        self.assertEqual(self.apk.read_bytes(), b'old-apk')
        self.assertTrue((self.mod / 'disable').exists())
        self.assertFalse((self.base / 'data/adb/rockbox-m3x-manual/launch.lock').exists())
        self.assertEqual((self.base / 'data/media/0/Music/my-song.mp3').read_bytes(),
                         b'music-is-not-an-install-asset')

    def test_update_preserves_settings_music_and_restores_complete_snapshot(self):
        result = self.run_install()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.mod / 'rockbox').read_bytes(), b'new-player')
        self.assertEqual((self.assets / 'config.cfg').read_bytes(), b'volume: -43\n')
        self.assertEqual((self.assets / '.playlist_control').read_bytes(), b'old-resume')
        self.assertEqual((self.assets / 'custom playlist.m3u8').read_bytes(), b'/Music/my-song.mp3\n')
        self.assertEqual((self.assets / 'codecs/mpa.codec').read_bytes(), b'new-codec')
        self.assertTrue(self.card.is_symlink())
        self.assertEqual(self.apk.read_bytes(), b'new-apk')
        result = self.run_install('rollback', self.snapshot())
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assert_old()
        self.assertFalse(self.card.is_symlink())

    def test_failed_apk_update_automatically_restores_native_assets_and_settings(self):
        (self.base / 'fail-pm').touch()
        result = self.run_install()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Previous installation restored', result.stderr)
        self.assert_old()
        self.assertTrue((self.snapshot() / 'snapshot-ready').exists())
        self.assertFalse(self.card.is_symlink())

    def test_corrupt_bundle_is_rejected_before_replacement(self):
        (self.bundle / 'payload/rockbox').write_bytes(b'corrupted')
        result = self.run_install()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('checksum verification failed', result.stderr)
        self.assertFalse(self.backups.exists())
        self.assert_old()

    def test_partial_module_copy_failure_restores_already_replaced_assets(self):
        (self.base / 'fail-stage').touch()
        result = self.run_install()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Previous installation restored', result.stderr)
        self.assert_old()

    def test_preflight_is_read_only_and_rejects_wrong_firmware_or_running_player(self):
        self.assertEqual(self.run_install('check').returncode, 0)
        self.assertFalse(self.backups.exists())
        for flag in ('wrong-firmware', 'running'):
            with self.subTest(flag=flag):
                (self.base / flag).touch()
                self.assertNotEqual(self.run_install().returncode, 0)
                (self.base / flag).unlink()
        self.assert_old()

    def test_pending_launch_blocks_install_and_keeps_its_lock(self):
        lock = self.base / 'data/adb/rockbox-m3x-manual/launch.lock'
        lock.mkdir(parents=True)
        (lock / 'pid').write_text('1234')
        result = self.run_install()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual((lock / 'pid').read_text(), '1234')
        self.assertEqual((self.mod / 'rockbox').read_bytes(), b'old-player')

    def test_fresh_install_rollback_removes_components_but_keeps_music_and_existing_card_entry(self):
        shutil.rmtree(self.mod)
        shutil.rmtree(self.assets)
        self.helper.unlink()
        self.apk.unlink()
        self.card.mkdir()
        (self.card / 'keep.txt').write_text('existing user folder')
        result = self.run_install()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('volume: -60', (self.assets / 'config.cfg').read_text())
        self.assertEqual(self.run_install('rollback', self.snapshot()).returncode, 0)
        for path in (self.mod, self.assets, self.helper, self.apk):
            self.assertFalse(path.exists(), str(path))
        self.assertEqual((self.card / 'keep.txt').read_text(), 'existing user folder')
        self.assertTrue((self.base / 'data/media/0/Music/my-song.mp3').exists())


class BundleTests(unittest.TestCase):
    @staticmethod
    def load(name):
        spec = importlib.util.spec_from_file_location(name, ROOT / 'tools/m3x' / (name + '.py'))
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def test_archive_is_deterministic_and_host_verification_detects_corruption(self):
        package = self.load('package-installer')
        installer = self.load('install')
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            files = {'payload/test file': b'hello', 'install.sh': b'installer'}
            files['SHA256SUMS'] = ''.join(hashlib.sha256(v).hexdigest() + '  ' + k + '\n'
                                        for k, v in files.items()).encode()
            package.write_zip(root / 'one.zip', files)
            package.write_zip(root / 'two.zip', files)
            self.assertEqual((root / 'one.zip').read_bytes(), (root / 'two.zip').read_bytes())
            import zipfile
            package.write_zip(root / 'source.zip', {'rockbox/tools/configure': b'configure'},
                              {'rockbox/tools/configure': 0o755})
            with zipfile.ZipFile(root / 'source.zip') as z:
                self.assertEqual((z.getinfo('rockbox/tools/configure').external_attr >> 16) & 0o777, 0o755)
            with zipfile.ZipFile(root / 'one.zip') as z:
                z.extractall(root / 'unpacked')
            installer.verify_bundle(root / 'unpacked')
            (root / 'unpacked/payload/test file').write_bytes(b'corrupted')
            with self.assertRaisesRegex(ValueError, 'Checksum mismatch'):
                installer.verify_bundle(root / 'unpacked')
