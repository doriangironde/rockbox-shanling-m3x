#!/system/bin/sh
# One-time launch from the Android icon. Boot autostart always remains disabled.
MODDIR=${0%/*}
STATE=/data/adb/rockbox-m3x-manual
LOG=/data/local/tmp/rockbox-m3x-manual.log
umask 077

fail() { echo "$*" >&2; exit 1; }

if [ "$1" = run ]; then
    trap 'rm -f "$STATE/launch.lock/pid"; rmdir "$STATE/launch.lock"' EXIT
    # Allow the requesting app's su transaction and Magisk logging to finish
    # while Android is still running. All subsequent work runs as root already.
    sleep 10
    mkdir -p "$STATE/session" || exit 1
    ln -sf "$MODDIR/rockbox" "$STATE/session/rockbox" || exit 1
    ln -sf "$MODDIR/service.sh" "$STATE/session/service.sh" || exit 1
    rm -f "$STATE/session/disable"
    /system/bin/sh "$STATE/session/service.sh"
    exit $?
fi

[ "$(id -u)" = 0 ] || fail "Root access is required."
[ -x "$MODDIR/rockbox" ] && [ -f "$MODDIR/service.sh" ] &&
    [ -x /data/local/tmp/fbpan ] || fail "The native Rockbox installation is incomplete."
pidof rockbox rockbox-dbg >/dev/null && fail "Rockbox is already running."
mkdir -p "$STATE" || fail "Cannot create the launch directory."
chmod 700 "$STATE"
if ! mkdir "$STATE/launch.lock" 2>/dev/null; then
    owner=$(cat "$STATE/launch.lock/pid" 2>/dev/null)
    case "$owner" in ''|*[!0-9]*) fail "Rockbox is already starting." ;; esac
    kill -0 "$owner" 2>/dev/null && fail "Rockbox is already starting."
    rm -f "$STATE/launch.lock/pid"
    rmdir "$STATE/launch.lock" || fail "Cannot clear the previous launch."
    mkdir "$STATE/launch.lock" || fail "Rockbox is already starting."
fi
touch "$MODDIR/disable" || fail "Cannot disable boot autostart."
# Redirect every inherited fd and detach from the Android app's process group.
nohup setsid /system/bin/sh "$0" run </dev/null >>"$LOG" 2>&1 &
echo $! > "$STATE/launch.lock/pid"
echo "Rockbox launch requested."
