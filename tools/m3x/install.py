#!/usr/bin/env python3
"""Verify and install an extracted M3X bundle, or restore an on-device snapshot."""
import argparse
import hashlib
from pathlib import Path
import re
import shlex
import shutil
import subprocess


def verify_bundle(root):
    expected = {}
    for line in (root / 'SHA256SUMS').read_text().splitlines():
        digest, name = line.split('  ', 1)
        path = root / name
        if not re.fullmatch('[0-9a-f]{64}', digest) or name in expected:
            raise ValueError('Invalid or duplicate checksum entry')
        if path.is_symlink() or not path.resolve().is_relative_to(root.resolve()):
            raise ValueError('Invalid bundle path: ' + name)
        if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise ValueError('Checksum mismatch: ' + name)
        expected[name] = digest
    actual = {p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()
              and p.name != '.DS_Store' and p != root / 'SHA256SUMS'}
    if actual != set(expected):
        raise ValueError('Bundle has missing or unexpected files')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['check', 'install', 'rollback'])
    parser.add_argument('--backup', help='Snapshot name or full path printed by install')
    parser.add_argument('--adb', default=shutil.which('adb') or 'adb')
    parser.add_argument('--serial', help='ADB serial when multiple devices are attached')
    args = parser.parse_args()
    adb = [args.adb] + (['-s', args.serial] if args.serial else [])

    def run(*command, capture=False):
        return subprocess.run([*adb, *command], check=True, text=True,
                              stdout=subprocess.PIPE if capture else None).stdout

    def root_shell(*command):
        return run('shell', 'su -c ' + shlex.quote(shlex.join(command)))

    if not args.serial:
        devices = [line.split()[0] for line in run('devices', capture=True).splitlines()[1:]
                   if len(line.split()) == 2 and line.split()[1] == 'device']
        if len(devices) != 1:
            parser.error('Connect exactly one authorized device, or use --serial.')
        adb.extend(['-s', devices[0]])
    if args.action == 'rollback':
        if not args.backup:
            parser.error('rollback requires --backup from the install output.')
        name = args.backup.removeprefix('/data/adb/rockbox-m3x-backups/')
        if not re.fullmatch('[A-Za-z0-9_-][A-Za-z0-9._-]*', name) or '..' in name:
            parser.error('Invalid snapshot name.')
        backup = '/data/adb/rockbox-m3x-backups/' + name
        root_shell('sh', backup + '/restore.sh', 'rollback', backup)
        return
    root = Path(__file__).resolve().parent
    verify_bundle(root)
    stage = run('shell', 'mktemp -d /data/local/tmp/m3x-install.XXXXXX', capture=True).strip()
    if not re.fullmatch(r'/data/local/tmp/m3x-install\.[A-Za-z0-9]+', stage):
        raise RuntimeError('Unexpected staging path')
    try:
        files = [str(p) for p in sorted(root.iterdir()) if p.name != '.DS_Store']
        run('push', *files, stage + '/')
        root_shell('sh', stage + '/install.sh', args.action)
    finally:
        run('shell', 'rm -rf ' + shlex.quote(stage))


if __name__ == '__main__':
    main()
