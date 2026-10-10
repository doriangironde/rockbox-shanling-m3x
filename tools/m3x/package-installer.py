#!/usr/bin/env python3
"""Create deterministic installer and corresponding-source ZIPs for the M3X."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import struct
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build-m3x'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def write_zip(path, files, modes=None):
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(files.items()):
            info = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
            info.create_system = 3
            info.compress_type = zipfile.ZIP_DEFLATED
            mode = 0o755 if name.endswith(('.py', '.sh')) or name in ('payload/rockbox', 'payload/fbpan') else 0o644
            mode = (modes or {}).get(name, mode)
            info.external_attr = (0o100000 | mode) << 16
            archive.writestr(info, data)
    with zipfile.ZipFile(path) as archive:
        if archive.testzip() is not None:
            raise ValueError('Archive integrity failure: ' + str(path))


def source_files():
    try:
        names = subprocess.check_output(['git', '-C', str(ROOT / 'rockbox'), 'ls-files',
                                         '-co', '--exclude-standard', '-z'],
                                        stderr=subprocess.DEVNULL).decode().split('\0')
    except subprocess.CalledProcessError:
        # The corresponding-source ZIP deliberately contains no Git metadata.
        names = [p.relative_to(ROOT / 'rockbox').as_posix()
                 for p in (ROOT / 'rockbox').rglob('*') if p.is_file() and '.git' not in p.parts]
    files = {}
    modes = {}

    def add(name, path):
        files[name] = path.read_bytes()
        modes[name] = 0o755 if path.stat().st_mode & 0o111 else 0o644

    for name in names:
        if not name:
            continue
        path = ROOT / 'rockbox' / name
        if path.is_file() and not path.is_symlink():
            add('rockbox/' + name, path)
    for directory in ('tools/m3x', 'm3x-module', 'android-launcher'):
        for path in (ROOT / directory).rglob('*'):
            if path.is_file() and path.suffix in ('.py', '.sh', '.c', '.h', '.md', '.xml', '.java', '.prop'):
                add(path.relative_to(ROOT).as_posix(), path)
    public = ROOT if (ROOT / 'README.md').is_file() else ROOT / 'publish/rockobx-shanling-m3x'
    for name in ('README.md', 'LICENSE', 'UPSTREAM.md'):
        add(name, public / name)
    add('M3X_OFFLINE_CHECKLIST.md', ROOT / 'M3X_OFFLINE_CHECKLIST.md')
    for directory in ('docs', 'media'):
        for path in (public / directory).rglob('*'):
            if path.is_file():
                add(path.relative_to(public).as_posix(), path)
    recorded_modes = ROOT / 'SOURCE-MODES.json'
    if recorded_modes.is_file():
        # Some ZIP extractors discard mode bits; retain archive reproducibility.
        recorded = json.loads(recorded_modes.read_text())
        modes = {name: recorded.get(name, mode) for name, mode in modes.items()}
    return files, modes


def asset_files(path):
    files = {}
    with zipfile.ZipFile(path) as archive:
        for info in archive.infolist():
            if info.is_dir():
                continue
            name = PurePosixPath(info.filename)
            if name.is_absolute() or '..' in name.parts or name.parts[0] != '.rockbox':
                raise ValueError('Unexpected asset path: ' + info.filename)
            if info.filename in files or (info.external_attr >> 16) & 0o170000 == 0o120000:
                raise ValueError('Duplicate or symlink asset: ' + info.filename)
            if info.filename in ('.rockbox/config.cfg', '.rockbox/.playlist_control'):
                raise ValueError('Assets contain private settings/resume state')
            files[info.filename] = archive.read(info)
    return files


def arm64(path):
    data = path.read_bytes()
    if data[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<H', data, 18)[0] != 183:
        raise ValueError('Expected little-endian ARM64 ELF: ' + str(path))
    return data


def main():
    subprocess.run([sys.executable, str(ROOT / 'tools/m3x/check-package.py')], check=True)
    sources, source_modes = source_files()
    source_hashes = {name: digest(data) for name, data in sorted(sources.items())}
    source_id = digest(json.dumps({'files': source_hashes, 'modes': source_modes}, sort_keys=True).encode())
    release = 'dev-' + source_id[:12]
    out = BUILD / 'release'
    out.mkdir(exist_ok=True)
    source_zip = out / ('Rockbox-M3X-' + release + '-source.zip')
    sources['SOURCE-SHA256.json'] = (json.dumps(source_hashes, indent=2) + '\n').encode()
    sources['SOURCE-MODES.json'] = (json.dumps(source_modes, indent=2, sort_keys=True) + '\n').encode()
    write_zip(source_zip, sources, source_modes)
    files = {
        'install.py': (ROOT / 'tools/m3x/install.py').read_bytes(),
        'install.sh': (ROOT / 'tools/m3x/install.sh').read_bytes(),
        'README.md': (ROOT / 'tools/m3x/INSTALL.md').read_bytes(),
        'LICENSE': sources['LICENSE'],
        'payload/rockbox': arm64(BUILD / 'rockbox'),
        'payload/fbpan': arm64(BUILD / 'm3x-fbpan'),
        'payload/Rockbox-M3X.apk': (ROOT / 'build-m3x-launcher/Rockbox-M3X.apk').read_bytes(),
        'payload/release-id': (release + '\n').encode(),
    }
    with zipfile.ZipFile(ROOT / 'build-m3x-launcher/Rockbox-M3X.apk') as apk:
        if apk.testzip() is not None or 'classes.dex' not in apk.namelist():
            raise ValueError('Invalid launcher APK')
    for name in ('service.sh', 'post-fs-data.sh', 'launch.sh', 'module.prop'):
        files['payload/module/' + name] = (ROOT / 'm3x-module' / name).read_bytes()
    for name, data in asset_files(BUILD / 'rockbox.zip').items():
        files['payload/assets/' + name] = data
    manifest = {
        'release': release,
        'source_id': source_id,
        'source_archive': source_zip.name,
        'source_archive_sha256': digest(source_zip.read_bytes()),
        'requirements': 'Rooted Shanling M3X, firmware 1.75, Android API 25, Magisk, existing I2C5 GPIO18/19 fix',
        'native_sha256': digest(files['payload/rockbox']),
        'launcher_apk_sha256': digest(files['payload/Rockbox-M3X.apk']),
        'framebuffer_helper_sha256': digest(files['payload/fbpan']),
        'boot_default': 'Android',
    }
    files['manifest.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
    files['SHA256SUMS'] = ''.join(digest(data) + '  ' + name + '\n'
                                for name, data in sorted(files.items())).encode()
    installer = out / ('Rockbox-M3X-' + release + '-installer.zip')
    write_zip(installer, files)
    print(json.dumps({'installer': str(installer), 'installer_sha256': digest(installer.read_bytes()),
                      'source': str(source_zip), 'source_sha256': manifest['source_archive_sha256'],
                      'release': release}, indent=2))


if __name__ == '__main__':
    main()
