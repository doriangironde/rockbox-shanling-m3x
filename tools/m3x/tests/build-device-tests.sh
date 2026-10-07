#!/bin/sh
# Compile the actual drivers and pipe-fed input loop for the Android device.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT"
CC=build-m3x/android-toolchain/bin/aarch64-linux-android-gcc
"$CC" -std=gnu99 -O2 -fPIE -pie -ffunction-sections -fdata-sections \
    -Wl,--gc-sections -Wall -Wextra -Werror \
    -Itools/m3x/tests/include -Irockbox/firmware/target/hosted/ibasso \
    -Irockbox/firmware/target/hosted -o "${1:-/tmp/m3x-input-battery-test}" \
    tools/m3x/tests/input-battery.c \
    rockbox/firmware/target/hosted/ibasso/m3x/button-m3x.c \
    rockbox/firmware/target/hosted/ibasso/powermgmt-ibasso.c \
    rockbox/firmware/target/hosted/ibasso/sysfs-ibasso.c

# Ensure the shared input driver's legacy iBasso branch still compiles.
"$CC" -std=gnu99 -Wall -Wextra -Werror -DM3X_TEST_LEGACY \
    -Itools/m3x/tests/include -Irockbox/firmware/target/hosted/ibasso \
    -Irockbox/firmware/target/hosted -c \
    rockbox/firmware/target/hosted/ibasso/button-ibasso.c \
    -o "${1:-/tmp/m3x-input-battery-test}.legacy.o"
