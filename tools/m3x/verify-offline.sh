#!/bin/sh
# Complete the repeatable host gates without invoking adb.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
make -C build-m3x -j4
python3 -m unittest discover -s tools/m3x/tests -v
sh tools/m3x/tests/build-device-tests.sh
python3 tools/m3x/tests/build-filesystem-test.py
sh -n m3x-module/service.sh m3x-module/post-fs-data.sh \
    tools/m3x/bounded-boot-service.sh tools/m3x/collect-state.sh
make -C build-m3x zip
python3 tools/m3x/check-package.py
build-m3x/android-toolchain/bin/aarch64-linux-android-gcc -std=gnu11 -O2 \
    -fPIE -pie -Wall -Wextra tools/m3x/fbpan.c -o build-m3x/m3x-fbpan
python3 tools/m3x/package-offline.py
