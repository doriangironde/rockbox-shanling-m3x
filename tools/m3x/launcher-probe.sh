#!/system/bin/sh
# Bounded check of the actual module launcher, using a temporary module dir.
# Delay takeover until Magisk has finished logging the initiating su command.
MOD=/data/adb/modules/rockbox_m3x
STAGE=/data/local/tmp/m3x-launcher-probe-module
OUT=/data/local/tmp/m3x-launcher-probe
if [ ! -e "$MOD/disable" ] || pidof rockbox rockbox-dbg >/dev/null; then
    echo "Disable the boot module and stop Rockbox first." >&2
    exit 1
fi
if [ "$1" != detached ]; then
    nohup sh "$0" detached > "$OUT.runner.log" 2>&1 < /dev/null &
    exit 0
fi
sleep 10
mkdir -p "$STAGE"
cp "$MOD/service.sh" "$STAGE/service.sh" || exit 1
ln -sfn "$MOD/rockbox" "$STAGE/rockbox"
rm -f "$STAGE/disable"
snapshot() {
    echo "=== $1 ==="
    cat /proc/uptime /proc/loadavg /proc/stat
    for zone in /sys/class/thermal/thermal_zone*; do
        echo "thermal $(cat "$zone/type") $(cat "$zone/temp")"
    done
    for service in zygote zygote_secondary surfaceflinger thermal-engine; do
        echo "service $service $(getprop init.svc.$service)"
    done
    cat /proc/[0-9]*/stat 2>/dev/null
    for pid in $(pidof rockbox); do
        for task in /proc/$pid/task/*; do
            [ -e "$task/stat" ] || continue
            cat "$task/stat"
            echo "wchan $(cat "$task/wchan")"
        done
    done
}
cleanup() {
    touch "$STAGE/disable"
    [ -n "$SVC" ] && kill -TERM "$SVC" 2>/dev/null
    [ -n "$GUARD" ] && kill "$GUARD" 2>/dev/null
}
trap 'exit 1' HUP INT TERM
trap cleanup EXIT
snapshot baseline > "$OUT.txt"
sh "$STAGE/service.sh" &
SVC=$!
(sleep 100; touch "$STAGE/disable"; kill -TERM "$SVC" 2>/dev/null) &
GUARD=$!
sleep 15
snapshot start >> "$OUT.txt"
for sample in 1 2 3 4 5 6; do
    kill -0 "$SVC" 2>/dev/null || break
    sleep 10
    snapshot "sample-$sample" >> "$OUT.txt"
done
touch "$STAGE/disable"
wait "$SVC"
SVC=
sleep 3
snapshot restored >> "$OUT.txt"
cleanup
trap - EXIT HUP INT TERM
echo complete > "$OUT.done"
