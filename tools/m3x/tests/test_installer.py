"""Exercise the real device installer against a harmless fake Android tree."""
import hashlib
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
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
            'payload/runtime/codecs/mpa.codec': b'new-codec',
            'payload/runtime/rocks/viewers/properties.rock': b'new-plugin',
            'payload/runtime/rocks/viewers/lua/helper.lua': b'trusted-script',
            'payload/assets/.rockbox/rockbox-info.txt': b'new-info',
            'payload/assets/.rockbox/rocks/games/levels.dat': b'new-levels',
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
status = subprocess.call(command + sys.argv[2:])
if sys.argv[1] == 'cp' and sys.argv[-1].endswith('/new-assets/') and (flag.parent / 'inject-stage-link').exists():
    (Path(sys.argv[-1]) / 'rockbox-info.txt').symlink_to(flag.parent / 'unrelated-root-file')
sys.exit(status)''')
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
    if (base / 'fail-uninstall').exists():
        print('Failure [injected failure]')
    else:
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
        self.assertEqual((self.mod / 'fbpan').read_bytes(), b'new-helper')
        self.assertEqual((self.mod / 'fbpan').stat().st_mode & 0o777, 0o755)
        self.assertFalse(self.helper.exists())
        self.assertEqual((self.assets / 'config.cfg').read_bytes(), b'volume: -43\n')
        self.assertEqual((self.assets / '.playlist_control').read_bytes(), b'old-resume')
        self.assertEqual((self.assets / 'custom playlist.m3u8').read_bytes(), b'/Music/my-song.mp3\n')
        self.assertFalse((self.assets / 'codecs/mpa.codec').exists())
        self.assertEqual((self.mod / 'runtime/codecs/mpa.codec').read_bytes(), b'new-codec')
        self.assertEqual((self.mod / 'runtime/codecs/mpa.codec').stat().st_mode & 0o777, 0o644)
        self.assertEqual((self.mod / 'runtime/rocks/viewers/properties.rock').read_bytes(), b'new-plugin')
        self.assertTrue(self.card.is_symlink())
        self.assertEqual(self.apk.read_bytes(), b'new-apk')
        result = self.run_install('rollback', self.snapshot())
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assert_old()
        self.assertFalse(self.card.is_symlink())

    def test_runtime_update_discards_old_code_but_preserves_shared_plugin_data(self):
        self.put(self.mod / 'runtime/rocks/obsolete.rock', b'old-private-code')
        self.put(self.assets / 'rocks/games/custom.rock', b'untrusted-code')
        self.put(self.assets / 'rocks/games/reversi.rev', b'saved-game')
        (self.mod / 'runtime/rocks/obsolete.rock').chmod(0o666)
        result = self.run_install()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((self.mod / 'runtime/rocks/obsolete.rock').exists())
        self.assertFalse((self.assets / 'rocks/games/custom.rock').exists())
        self.assertEqual((self.assets / 'rocks/games/reversi.rev').read_bytes(), b'saved-game')
        self.assertEqual((self.assets / 'rocks/games/levels.dat').read_bytes(), b'new-levels')
        result = self.run_install('rollback', self.snapshot())
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.mod / 'runtime/rocks/obsolete.rock').read_bytes(), b'old-private-code')
        self.assertEqual((self.assets / 'rocks/games/custom.rock').read_bytes(), b'untrusted-code')
        self.assert_old()

    def test_missing_runtime_and_shared_executable_payload_are_rejected(self):
        runtime = self.bundle / 'payload/runtime/codecs/mpa.codec'
        data = runtime.read_bytes()
        runtime.unlink()
        self.checksums()
        result = self.run_install()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Private runtime modules are missing', result.stderr)
        runtime.write_bytes(data)
        for extension in ('rock', 'codec', 'ovl', 'lua'):
            with self.subTest(extension=extension):
                path = self.bundle / ('payload/assets/.rockbox/rocks/unsafe.' + extension)
                self.put(path, b'code')
                self.checksums()
                result = self.run_install()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('must not be bundled as shared assets', result.stderr)
                path.unlink()
        self.assertFalse(self.backups.exists())
        self.assert_old()

    def test_private_runtime_modes_are_normalized_not_copied_from_bundle(self):
        for path in (self.bundle / 'payload/runtime').rglob('*'):
            path.chmod(0o777)
        result = self.run_install()
        self.assertEqual(result.returncode, 0, result.stderr)
        runtime = self.mod / 'runtime'
        for path in [runtime, *runtime.rglob('*')]:
            self.assertEqual(path.stat().st_mode & 0o777,
                             0o755 if path.is_dir() else 0o644, str(path))

    def test_link_created_during_shared_copy_is_rejected_before_overlay(self):
        victim = self.base / 'unrelated-root-file'
        victim.write_bytes(b'keep')
        (self.base / 'inject-stage-link').touch()
        result = self.run_install()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Staged shared assets must not contain symlinks', result.stderr)
        self.assertEqual(victim.read_bytes(), b'keep')
        self.assertTrue((self.snapshot() / 'snapshot-ready').exists())
        self.assert_old()

    def test_shared_asset_symlink_cannot_redirect_root_overlay(self):
        victim = self.base / 'unrelated-root-file'
        victim.write_bytes(b'keep')
        (self.assets / 'rockbox-info.txt').symlink_to(victim)
        result = self.run_install()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Shared assets must not contain symlinks', result.stderr)
        self.assertEqual(victim.read_bytes(), b'keep')
        self.assertFalse(self.backups.exists())
        self.assert_old()

    def test_module_helper_update_and_rollback_do_not_recreate_legacy_helper(self):
        self.helper.unlink()
        self.put(self.mod / 'fbpan', b'previous module helper')
        (self.mod / 'fbpan').chmod(0o755)
        result = self.run_install()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.mod / 'fbpan').read_bytes(), b'new-helper')
        self.assertFalse(self.helper.exists())
        result = self.run_install('rollback', self.snapshot())
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.mod / 'fbpan').read_bytes(), b'previous module helper')
        self.assertFalse(self.helper.exists())
        self.assertEqual((self.assets / 'config.cfg').read_bytes(), b'volume: -43\n')

    def test_failed_apk_update_automatically_restores_native_assets_and_settings(self):
        (self.base / 'fail-pm').touch()
        result = self.run_install()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Previous installation restored', result.stderr)
        self.assert_old()
        self.assertTrue((self.snapshot() / 'snapshot-ready').exists())
        self.assertFalse((self.mod / 'runtime').exists())
        self.assertFalse(self.card.is_symlink())

    def test_corrupt_bundle_is_rejected_before_replacement(self):
        (self.bundle / 'payload/rockbox').write_bytes(b'corrupted')
        result = self.run_install()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('checksum verification failed', result.stderr)
        self.assertFalse(self.backups.exists())
        self.assert_old()

    def test_unlisted_payload_file_is_rejected_before_replacement(self):
        self.put(self.bundle / 'payload/module/unverified.sh', b'not checksummed')
        result = self.run_install()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('missing or unexpected files', result.stderr)
        self.assertFalse(self.backups.exists())
        self.assert_old()

    def test_bundle_symlinks_are_rejected_even_when_unlisted(self):
        (self.bundle / 'payload/module/unchecked').symlink_to(self.mod, target_is_directory=True)
        result = self.run_install()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('must not contain symlinks', result.stderr)
        self.assertFalse(self.backups.exists())
        self.assert_old()

    def test_duplicate_manifest_entries_cannot_hide_an_unlisted_file(self):
        self.put(self.bundle / 'payload/module/unverified.sh', b'not checksummed')
        sums = self.bundle / 'SHA256SUMS'
        sums.write_text(sums.read_text() + sums.read_text().splitlines()[0] + '\n')
        result = self.run_install()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Invalid or duplicate bundle checksum path', result.stderr)
        self.assertFalse(self.backups.exists())
        self.assert_old()

    def test_manifest_path_outside_bundle_is_rejected_before_replacement(self):
        external = self.base / 'external'
        external.write_bytes(b'outside bundle')
        sums = self.bundle / 'SHA256SUMS'
        sums.write_text(sums.read_text() + hashlib.sha256(external.read_bytes()).hexdigest()
                        + '  ../external\n')
        result = self.run_install()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Invalid or duplicate bundle checksum path', result.stderr)
        self.assertFalse(self.backups.exists())
        self.assert_old()

    def test_partial_module_copy_failure_restores_already_replaced_assets(self):
        (self.base / 'fail-stage').touch()
        result = self.run_install()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Previous installation restored', result.stderr)
        self.assert_old()

    def test_helper_staging_never_writes_through_a_shell_controlled_symlink(self):
        victim = self.base / 'unrelated-root-file'
        victim.write_bytes(b'keep this file')
        cp = shutil.which('cp')
        self.command('cp', f'''for target do :; done
case "$target" in
    "{self.helper}.new-"*) ln -s "{victim}" "$target";;
esac
exec "{cp}" "$@"''')
        result = self.run_install()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(victim.read_bytes(), b'keep this file')
        self.assertEqual((self.mod / 'fbpan').read_bytes(), b'new-helper')
        self.assertFalse(self.helper.exists())
        result = self.run_install('rollback', self.snapshot())
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(victim.read_bytes(), b'keep this file')
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

    def test_failed_first_install_launcher_uninstall_reports_incomplete_restore(self):
        shutil.rmtree(self.mod)
        shutil.rmtree(self.assets)
        self.helper.unlink()
        self.apk.unlink()
        result = self.run_install()
        self.assertEqual(result.returncode, 0, result.stderr)
        (self.base / 'fail-uninstall').touch()
        snapshot = self.snapshot()
        result = self.run_install('rollback', snapshot)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Restore failed; snapshot retained:', result.stderr)
        self.assertEqual(self.apk.read_bytes(), b'new-apk')
        self.assertTrue((snapshot / 'snapshot-ready').is_file())
        self.assertFalse((self.base / 'data/adb/rockbox-m3x-manual/launch.lock').exists())
        (self.base / 'fail-uninstall').unlink()
        result = self.run_install('rollback', snapshot)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(self.apk.exists())


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

    def test_host_verification_rejects_unlisted_directory_and_metadata_symlinks(self):
        installer = self.load('install')
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            payload = root / 'payload'
            payload.mkdir()
            data = b'verified'
            (payload / 'file').write_bytes(data)
            (root / 'SHA256SUMS').write_text(hashlib.sha256(data).hexdigest() + '  payload/file\n')
            for name, target in (('unchecked', payload), ('.DS_Store', payload / 'file')):
                with self.subTest(name=name):
                    link = root / name
                    link.symlink_to(target)
                    with self.assertRaisesRegex(ValueError, 'must not contain symlinks'):
                        installer.verify_bundle(root)
                    link.unlink()
            installer.verify_bundle(root)

    def test_asset_archive_rejects_private_state_path_aliases_and_symlinks(self):
        import zipfile
        package = self.load('package-installer')
        with tempfile.TemporaryDirectory() as temp:
            archive = Path(temp) / 'assets.zip'
            for name in ('.rockbox//config.cfg', '.rockbox/./config.cfg',
                         '.rockbox\\config.cfg', '.rockbox/../config.cfg',
                         '.rockbox/config.cfg', '.rockbox/.playlist_control'):
                with self.subTest(name=name):
                    with zipfile.ZipFile(archive, 'w') as z:
                        z.writestr(name, b'private settings')
                    with self.assertRaises(ValueError):
                        package.asset_files(archive)
            with zipfile.ZipFile(archive, 'w') as z:
                info = zipfile.ZipInfo('.rockbox/codecs/link.codec')
                info.create_system = 3
                info.external_attr = (0o120000 | 0o777) << 16
                z.writestr(info, b'/outside')
            with self.assertRaisesRegex(ValueError, 'symlink asset'):
                package.asset_files(archive)
            with zipfile.ZipFile(archive, 'w') as z:
                z.writestr('.rockbox/codecs/test.codec', b'codec')
                z.writestr('.rockbox/wps/a theme.wps', b'theme')
            self.assertEqual(package.asset_files(archive), {
                '.rockbox/codecs/test.codec': b'codec', '.rockbox/wps/a theme.wps': b'theme'})

    def test_runtime_split_keeps_code_and_lua_private_and_plugin_data_shared(self):
        package = self.load('package-installer')
        files = package.split_assets({
            '.rockbox/codecs/mpa.codec': b'codec',
            '.rockbox/rocks/viewers/properties.rock': b'plugin',
            '.rockbox/rocks/viewers/jpeg.ovl': b'overlay',
            '.rockbox/rocks/viewers/lua/helper.lua': b'script',
            '.rockbox/rocks/games/levels.dat': b'levels',
            '.rockbox/themes/test.cfg': b'theme',
        })
        self.assertEqual(files, {
            'payload/runtime/codecs/mpa.codec': b'codec',
            'payload/runtime/rocks/viewers/properties.rock': b'plugin',
            'payload/runtime/rocks/viewers/jpeg.ovl': b'overlay',
            'payload/runtime/rocks/viewers/lua/helper.lua': b'script',
            'payload/runtime/rocks/games/levels.dat': b'levels',
            'payload/assets/.rockbox/rocks/games/levels.dat': b'levels',
            'payload/assets/.rockbox/themes/test.cfg': b'theme',
        })

    def test_package_validation_rejects_stale_modules_with_python_optimization(self):
        import zipfile
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            script = root / 'tools/m3x/check-package.py'
            script.parent.mkdir(parents=True)
            shutil.copyfile(ROOT / 'tools/m3x/check-package.py', script)
            build = root / 'build-m3x'
            module = build / 'lib/rbcodec/codecs/mpa.codec'
            module.parent.mkdir(parents=True)
            module.write_bytes(b'current codec')
            (build / 'rockbox-info.txt').write_bytes(b'current build info')
            archive = build / 'rockbox.zip'
            with zipfile.ZipFile(archive, 'w') as z:
                z.writestr('.rockbox/codecs/mpa.codec', b'stale codec')
                z.writestr('.rockbox/rockbox-info.txt', b'current build info')
            result = subprocess.run([sys.executable, '-O', str(script)],
                                    capture_output=True, text=True, timeout=15)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('stale packaged module', result.stderr)
            self.assertNotIn('"result": "PASS"', result.stdout)
            with zipfile.ZipFile(archive, 'w') as z:
                z.writestr('.rockbox/codecs/mpa.codec', module.read_bytes())
                z.writestr('.rockbox/rockbox-info.txt', b'current build info')
            result = subprocess.run([sys.executable, '-O', str(script)],
                                    capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('"result": "PASS"', result.stdout)
