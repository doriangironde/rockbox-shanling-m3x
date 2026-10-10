#!/system/bin/sh
# Install a verified native bundle, or restore one of its private snapshots.
set -eu
umask 022
PACKAGE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
MOD="/data/adb/modules/rockbox_m3x"
ASSETS="/data/media/0/.rockbox"
HELPER="/data/local/tmp/fbpan"
STATE="/data/adb/rockbox-m3x-manual"
BACKUPS="/data/adb/rockbox-m3x-backups"
CARD="/data/media/0/microSD"
CARD_TARGET="/mnt/media_rw/external_sd1"
BB="/sbin/.magisk/busybox/busybox"
APP=org.rockbox.m3x.launcher
SAVE=
CHANGED=0
LOCKED=0

fail() { echo "$*" >&2; exit 1; }
preflight() {
    [ "$(id -u)" = 0 ] || fail 'Run this installer as root.'
    [ "$(getprop ro.product.manufacturer)" = Shanling ] &&
        [ "$(getprop ro.product.model)" = 'Shanling M3X' ] &&
        [ "$(getprop ro.build.version.sdk)" = 25 ] &&
        [ "$(getprop ro.build.display.id)" = 1.75 ] &&
        [ "$(uname -m)" = aarch64 ] || fail 'Requires the tested M3X firmware 1.75 / Android API 25.'
    [ -x "$BB" ] || fail 'The Magisk BusyBox runtime is required.'
    [ "$(getprop init.svc.zygote)" = running ] &&
        [ "$(getprop init.svc.surfaceflinger)" = running ] || fail 'Return to Android first.'
    ! pidof rockbox rockbox-dbg >/dev/null || fail 'Stop Rockbox first.'
    [ ! -L "$MOD" ] && [ ! -L "$ASSETS" ] || fail 'Module/assets must be real directories.'
    [ ! -e "$MOD" ] || [ -f "$MOD/disable" ] || fail 'Disable Rockbox boot autostart first.'
}
lock_install() {
    mkdir -p "$STATE" "$BACKUPS"
    chmod 700 "$STATE" "$BACKUPS"
    mkdir "$STATE/launch.lock" 2>/dev/null || fail 'A launch or installer is already pending.'
    LOCKED=1
    echo $$ > "$STATE/launch.lock/pid"
}
replace_tree() {
    local source=$1 target=$2
    [ ! -e "$target.m3x-next-$$" ] && [ ! -e "$target.m3x-old-$$" ] || return 1
    if [ -d "$source" ]; then
        "$BB" cp -a "$source" "$target.m3x-next-$$" || { rm -rf "$target.m3x-next-$$"; return 1; }
    fi
    if [ -e "$target" ]; then
        mv "$target" "$target.m3x-old-$$" || { rm -rf "$target.m3x-next-$$"; return 1; }
    fi
    if [ -d "$source" ]; then
        mv "$target.m3x-next-$$" "$target" || {
            rm -rf "$target.m3x-next-$$"
            [ ! -e "$target.m3x-old-$$" ] || mv "$target.m3x-old-$$" "$target"
            return 1
        }
    fi
    rm -rf "$target.m3x-old-$$"
}
restore_snapshot() {
    local snapshot=$1
    [ -f "$snapshot/snapshot-ready" ] || return 1
    replace_tree "$snapshot/module" "$MOD" || return 1
    [ ! -d "$MOD" ] || touch "$MOD/disable" || return 1
    replace_tree "$snapshot/assets" "$ASSETS" || return 1
    if [ -f "$snapshot/fbpan" ]; then
        cp -p "$snapshot/fbpan" "$HELPER.new-$$" && mv "$HELPER.new-$$" "$HELPER" || return 1
    else
        rm -f "$HELPER" || return 1
    fi
    if [ -f "$snapshot/card-link-created" ] &&
        [ "$(readlink "$CARD" 2>/dev/null)" = "$CARD_TARGET" ]; then
        rm "$CARD" || return 1
    fi
    if [ -f "$snapshot/apk-touched" ]; then
        if [ -f "$snapshot/launcher.apk" ]; then
            pm install -r "$snapshot/launcher.apk" | grep -q '^Success' || return 1
        else
            pm uninstall "$APP" >/dev/null 2>&1 || true
        fi
    fi
    sync
}
on_exit() {
    local status=$1
    trap - EXIT HUP INT TERM
    if [ "$status" -ne 0 ] && [ "$CHANGED" = 1 ]; then
        echo 'Update failed; restoring the saved installation.' >&2
        if restore_snapshot "$SAVE"; then
            echo 'Previous installation restored.' >&2
        else
            echo "Automatic restore failed. Snapshot retained: $SAVE" >&2
        fi
    fi
    [ "$LOCKED" = 0 ] || { rm -f "$STATE/launch.lock/pid"; rmdir "$STATE/launch.lock"; }
    exit "$status"
}
trap 'on_exit $?' EXIT
trap 'exit 130' INT
trap 'exit 143' HUP TERM
preflight
case "${1:-check}" in
    rollback)
        SAVE=${2:-}
        case "$SAVE" in "$BACKUPS"/*) ;; *) fail 'Choose a snapshot inside the Rockbox backup directory.';; esac
        case "$SAVE" in *..*) fail 'Invalid snapshot path.';; esac
        [ "$(dirname -- "$SAVE")" = "$BACKUPS" ] && [ ! -L "$SAVE" ] &&
            [ -f "$SAVE/snapshot-ready" ] || fail 'Snapshot is incomplete or invalid.'
        lock_install
        restore_snapshot "$SAVE" || fail "Restore failed; snapshot retained: $SAVE"
        echo "Restored snapshot: $SAVE"
        exit 0;;
    check|install) ;;
    *) fail 'Usage: install.sh check | install | rollback /data/adb/rockbox-m3x-backups/SNAPSHOT';;
esac
[ -f "$PACKAGE/SHA256SUMS" ] || fail 'Extract the complete bundle first.'
(cd "$PACKAGE"; "$BB" sha256sum -c SHA256SUMS >/dev/null) || fail 'Bundle checksum verification failed.'
[ -e "/dev/graphics/fb0" ] && [ -e "/dev/snd/pcmC0D0p" ] || fail 'Native display/audio devices are missing.'
[ -d "$PACKAGE/payload/assets/.rockbox" ] || fail 'Matching assets are missing.'
[ ! -e "$PACKAGE/payload/assets/.rockbox/config.cfg" ] &&
    [ ! -e "$PACKAGE/payload/assets/.rockbox/.playlist_control" ] || fail 'Bundle must not contain user settings or resume state.'
free_kb=$(df -k "/data/" | "$BB" awk 'NR==2 {print $4}')
need_kb=$(du -sk "$MOD" "$ASSETS" "$PACKAGE/payload" 2>/dev/null | "$BB" awk '{sum += $1} END {printf "%.0f", sum * 3 + 32768}')
case "$free_kb" in ''|*[!0-9]*) fail 'Cannot determine free space.';; esac
[ "$free_kb" -gt "$need_kb" ] || fail 'Insufficient free space for staging and backup.'
echo 'Preflight passed: M3X 1.75, Android active, checksums valid, sufficient space.'
[ "${1:-check}" = install ] || exit 0
lock_install
release=$(cat "$PACKAGE/payload/release-id")
case "$release" in ''|*[!a-zA-Z0-9._-]*) fail 'Invalid release identifier.';; esac
SAVE=$(mktemp -d "$BACKUPS/$release.XXXXXX")
chmod 700 "$SAVE"
[ ! -d "$MOD" ] || "$BB" cp -a "$MOD" "$SAVE/module"
[ ! -d "$ASSETS" ] || "$BB" cp -a "$ASSETS" "$SAVE/assets"
[ ! -f "$HELPER" ] || cp -p "$HELPER" "$SAVE/fbpan"
apk=$(pm path "$APP" 2>/dev/null || true)
if [ -n "$apk" ]; then
    case "$apk" in *'
'*) fail 'Split APK launcher installations are unsupported.';; package:/*) apk=${apk#package:};; *) fail 'Cannot locate the installed launcher.';; esac
    cp -p "$apk" "$SAVE/launcher.apk"
fi
cp "$0" "$SAVE/restore.sh"
touch "$SAVE/snapshot-ready"
echo "Backup: $SAVE"
mkdir "$SAVE/new-module" "$SAVE/new-assets"
[ ! -d "$MOD" ] || "$BB" cp -a "$MOD/." "$SAVE/new-module/"
[ ! -d "$ASSETS" ] || "$BB" cp -a "$ASSETS/." "$SAVE/new-assets/"
"$BB" cp -a "$PACKAGE/payload/module/." "$SAVE/new-module/"
cp "$PACKAGE/payload/rockbox" "$SAVE/new-module/rockbox"
touch "$SAVE/new-module/disable"
chown -R 0:0 "$SAVE/new-module"
chmod -R go-w "$SAVE/new-module"
chmod 755 "$SAVE/new-module" "$SAVE/new-module/rockbox" "$SAVE/new-module/launch.sh" "$SAVE/new-module/service.sh" "$SAVE/new-module/post-fs-data.sh"
"$BB" cp -a "$PACKAGE/payload/assets/.rockbox/." "$SAVE/new-assets/"
if [ ! -e "$SAVE/new-assets/config.cfg" ]; then
    cat > "$SAVE/new-assets/config.cfg" <<'CONFIG'
resume on startup: off
start in screen: root
voice menus: off
touchscreen mode: point
volume: -60
backlight timeout: on
font: /.rockbox/fonts/35-Adobe-Helvetica.fnt
CONFIG
fi
CHANGED=1
replace_tree "$SAVE/new-assets" "$ASSETS"
replace_tree "$SAVE/new-module" "$MOD"
cp "$PACKAGE/payload/fbpan" "$HELPER.new-$$"
chmod 755 "$HELPER.new-$$"
mv "$HELPER.new-$$" "$HELPER"
if [ ! -e "$CARD" ] && [ ! -L "$CARD" ]; then
    touch "$SAVE/card-link-created"
    ln -s "$CARD_TARGET" "$CARD"
fi
if [ ! -f "$SAVE/launcher.apk" ] || ! "$BB" cmp -s "$SAVE/launcher.apk" "$PACKAGE/payload/Rockbox-M3X.apk"; then
    touch "$SAVE/apk-touched"
    pm install -r "$PACKAGE/payload/Rockbox-M3X.apk" | grep -q '^Success' || fail 'Launcher APK installation failed.'
fi
sync
touch "$SAVE/install-complete"
CHANGED=0
rm -rf "$SAVE/new-assets" "$SAVE/new-module"
echo "Installed: $release"
echo 'Android remains the default boot. Tap Rockbox to launch.'
echo "Rollback: sh $SAVE/restore.sh rollback $SAVE"
