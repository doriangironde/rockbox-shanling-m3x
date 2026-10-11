"""Check archive integrity and ensure it contains the current ARM64 modules."""
from pathlib import Path
import hashlib
import json
import zipfile

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build-m3x"
archive = BUILD / "rockbox.zip"
with zipfile.ZipFile(archive) as z:
    if z.testzip() is not None:
        raise ValueError("ZIP integrity failure")
    modules = list((BUILD / "lib/rbcodec/codecs").rglob("*.codec"))
    modules += list((BUILD / "apps/plugins").rglob("*.rock"))
    if not modules:
        raise ValueError("No modules built")
    for module in modules:
        paths = [p for p in z.namelist() if p.endswith("/" + module.name)]
        if len(paths) != 1 or z.read(paths[0]) != module.read_bytes():
            raise ValueError("Missing, duplicate or stale packaged module: " + module.name)
    if z.read(".rockbox/rockbox-info.txt") != (BUILD / "rockbox-info.txt").read_bytes():
        raise ValueError("Stale packaged rockbox-info.txt")
print(json.dumps({"result": "PASS", "modules": len(modules),
                  "sha256": hashlib.sha256(archive.read_bytes()).hexdigest()}))
