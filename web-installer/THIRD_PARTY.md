# Third-party code and non-distributed rooting prerequisites

The web installer source uses the repository's GPL-2.0-or-later license.

## Bundled browser dependencies

Runtime: `@yume-chan/adb` (Tango), `@yume-chan/adb-daemon-webusb`,
`@yume-chan/stream-extra` and their lockfile-resolved `@yume-chan` dependencies,
plus `fflate`. These packages are MIT-licensed. Vite/Rolldown and Playwright are
build/test dependencies, not an external script service. Exact versions/integrity
are pinned in `package-lock.json`. Full runtime package notices are copied into
`public/third-party-licenses.txt` and must remain in the static deployment.

- Tango/ADB: https://github.com/yume-chan/ya-webadb
- fflate: https://github.com/101arrowz/fflate
- Vite: https://github.com/vitejs/vite
- Playwright: https://github.com/microsoft/playwright

## Sahara / Firehose reference

Wire formats and state transitions were informed by `linux-msm/qdl`'s
Sahara/Firehose implementation (BSD-3-Clause). Original notices are retained in
`LICENSES/qdl.txt`, also included in the deployed notices file. QDL native code or
binaries are not linked/shipped by this site.

- https://github.com/linux-msm/qdl

## User-supplied assets, not shipped

- **Magisk 30.7:** official release APK is GPL-3.0 software. The browser accepts
  only the pinned official APK, extracts its existing file-patching tools in
  memory and stages them on the user's own device. No modified/pre-rooted APK or
  Magisk binary is redistributed here. Source:
  https://github.com/topjohnwu/Magisk/tree/v30.7
- **Shanling M3X 1.75 stock boot image / firmware:** downloaded from the OEM:
  https://en.shanling.com/download/77
  The browser extracts and authenticates `boot.img` from the user's firmware ZIP.
  OEM firmware and generated rooted boot images are not part of public source/site
  or Rockbox installer ZIPs.
- **Qualcomm MSM8937 Firehose programmer:** **Get programmer** downloads directly
  from its pinned original upstream, only after an explicit click:
  https://github.com/thantoeaungat/firehose/blob/56bd8a3d54f9dc69ddf59ee486c557b8cb25cd05/Dongle%20Firehose/Qcom/BOOT/MSM8937.mbn
  The file's exact size and SHA-256 match the port's working local copy. Its
  redistribution rights remain unresolved: **do not mirror/package it** merely
  because it appeared in a public repository. A hash does not grant redistribution
  rights or establish that the programmer itself is a sandbox.

Profile hashes are public metadata, not included firmware/programmer bytes.
Private recovery backups, signing identity, device captures, ADB credentials and
user data must never be copied into this directory or a deployment.
