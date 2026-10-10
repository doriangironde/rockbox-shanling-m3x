#!/usr/bin/env python3
"""Build the small M3X launch icon with Android SDK tools and a retained signing key."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--sdk', type=Path, default=Path(os.environ.get('ANDROID_SDK_ROOT',
    str(Path.home() / 'Library/Android/sdk'))))
p.add_argument('--build-tools', default='36.0.0')
p.add_argument('--platform', default='android-37.0')
p.add_argument('--output', type=Path, default=ROOT / 'build-m3x-launcher')
args = p.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
sdk = args.sdk.resolve()
bt = sdk / 'build-tools' / args.build_tools
android = sdk / 'platforms' / args.platform / 'android.jar'
source = ROOT / 'android-launcher'
classes, dex = out / 'classes', out / 'dex'
for directory in (classes, dex):
    if directory.exists():
        shutil.rmtree(directory)
    directory.mkdir()
res = out / 'res/drawable'
res.mkdir(parents=True, exist_ok=True)
shutil.copy2(ROOT / 'rockbox/android/res/drawable-hdpi/launcher.png', res / 'icon.png')

def run(*command):
    subprocess.run(list(map(str, command)), check=True)

run('javac', '-source', '8', '-target', '8', '-bootclasspath', android,
    '-d', classes, *sorted(source.rglob('*.java')))
run(bt / 'd8', '--min-api', '25', '--lib', android, '--output', dex,
    *sorted(classes.rglob('*.class')))
unsigned = out / 'unsigned.apk'
run(bt / 'aapt', 'package', '-f', '-M', source / 'AndroidManifest.xml',
    '-S', out / 'res', '-I', android, '-F', unsigned)
with zipfile.ZipFile(unsigned, 'a') as archive:
    info = zipfile.ZipInfo('classes.dex', (2026, 1, 1, 0, 0, 0))
    info.compress_type = zipfile.ZIP_DEFLATED
    archive.writestr(info, (dex / 'classes.dex').read_bytes())
aligned = out / 'aligned.apk'
run(bt / 'zipalign', '-f', '4', unsigned, aligned)
# This is a local development identity. Keep this file to sign future updates.
# The password protects neither user data nor a production signing identity.
key = out / 'launcher-signing.jks'
password = 'm3x-development'
if not key.exists():
    run('keytool', '-genkeypair', '-keystore', key, '-storepass', password,
        '-keypass', password, '-alias', 'm3x', '-keyalg', 'RSA', '-keysize', '2048',
        '-validity', '10000', '-dname', 'CN=Rockbox M3X Development', '-noprompt')
    key.chmod(0o600)
apk = out / 'Rockbox-M3X.apk'
run(bt / 'apksigner', 'sign', '--ks', key, '--ks-pass', 'pass:' + password,
    '--key-pass', 'pass:' + password, '--out', apk, aligned)
run(bt / 'apksigner', 'verify', '--verbose', apk)
print(apk)
