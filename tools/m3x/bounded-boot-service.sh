#!/system/bin/sh
# One boot only: restore the normal launcher, then limit this run to 180 s.
# Install after saving service.sh as service-normal.sh in the same module.
MODDIR=${0%/*}
OUT=/data/local/tmp/m3x-boot-probe.txt
[ -e "$MODDIR/disable" ] && exit 0
# Replace the directory entry, not the inode the shell is still reading.
# Overwriting this running script can make ash execute the replacement tail.
if ! cp "$MODDIR/service-normal.sh" "$MODDIR/.service-next.sh" ||
   ! mv -f "$MODDIR/.service-next.sh" "$MODDIR/service.sh"; then
    touch "$MODDIR/disable"
    start surfaceflinger
    start zygote
    start zygote_secondary
    exit 1
fi
snapshot() {
    echo "=== $1 ==="
    cat /proc/uptime /proc/loadavg /proc/stat
    for zone in /sys/class/thermal/thermal_zone*; do
        echo "thermal $(cat "$zone/type") $(cat "$zone/temp")"
    done
    for service in zygote zygote_secondary surfaceflinger thermal-engine; do
        echo "service $service $(getprop init.svc.$service)"
    done
    echo "battery_capacity $(cat /sys/class/power_supply/battery/capacity)"
    echo "internal_assets $(ls /data/media/0/.rockbox/config.cfg)"
    cat /proc/[0-9]*/stat 2>/dev/null
    for pid in $(pidof rockbox); do
        for task in /proc/$pid/task/*; do
            [ -e "$task/stat" ] || continue
            cat "$task/stat"
            echo "wchan $(cat "$task/wchan")"
        done
    done
    if [ -z "$FRAME_CAPTURED" ] && pidof rockbox >/dev/null &&
       [ -x /data/local/tmp/fbdump ]; then
        if /data/local/tmp/fbdump /dev/graphics/fb0 /data/local/tmp/m3x-boot-frame.rgba \
                > /data/local/tmp/m3x-boot-frame.log 2>&1; then
            FRAME_CAPTURED=1
        fi
    fi
}
snapshot baseline > "$OUT"
(sleep 180; touch "$MODDIR/disable") &
GUARD=$!
sh "$MODDIR/service-normal.sh" &
SVC=$!
sleep 15
snapshot start >> "$OUT"
for sample in 1 2 3 4 5 6 7 8 9 10; do
    kill -0 "$SVC" 2>/dev/null || break
    sleep 15
    snapshot "sample-$sample" >> "$OUT"
done
wait "$SVC"
kill "$GUARD" 2>/dev/null
touch "$MODDIR/disable"
