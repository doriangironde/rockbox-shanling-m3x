"""Bundle the current executable, assets, launcher and diagnostics for review."""
from pathlib import Path
import hashlib
import json
import zipfile

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build-m3x"
FILES = {
    "rockbox": BUILD / "rockbox",
    "rockbox-assets.zip": BUILD / "rockbox.zip",
    "module/service.sh": ROOT / "m3x-module/service.sh",
    "module/post-fs-data.sh": ROOT / "m3x-module/post-fs-data.sh",
    "helpers/fbpan": BUILD / "m3x-fbpan",
    "helpers/collect-state.sh": ROOT / "tools/m3x/collect-state.sh",
    "OFFLINE_CHECKLIST.md": ROOT / "M3X_OFFLINE_CHECKLIST.md",
}
manifest = {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in FILES.items()}
output = BUILD / "m3x-offline-bundle.zip"
with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as z:
    for name, path in FILES.items():
        z.write(path, name)
    z.writestr("SHA256.json", json.dumps(manifest, indent=2) + "\n")
    z.writestr("README.txt", """M3X development bundle for a later device test.

rockbox is the native ARM64 executable; rockbox-assets.zip contains its matching
/.rockbox assets. module/ contains the updated scripts for the existing Rockbox
module. helpers/fbpan is installed at /data/local/tmp/fbpan by the current setup.
Run collect-state.sh as root to gather diagnostics when the M3X is available.

This is a manual development update bundle, not a Magisk installer. Preserve the
existing executable, module scripts and user settings before applying an update.
Preserve config and music while updating assets; executable, codecs and plugins
must be updated together. Keep the module disabled until a bounded device test
is ready. Read OFFLINE_CHECKLIST.md for validation and remaining hardware checks.
No boot image is included, and creating this bundle does not deploy anything.
""")
with zipfile.ZipFile(output) as z:
    assert z.testzip() is None
    for name, expected in manifest.items():
        assert hashlib.sha256(z.read(name)).hexdigest() == expected
print(json.dumps({"bundle": str(output), "sha256": hashlib.sha256(output.read_bytes()).hexdigest()}))
