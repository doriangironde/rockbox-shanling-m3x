#!/system/bin/sh
# Read-only runtime/storage diagnostics for the next device session.
# Run as root. This never starts/stops services, mounts cards or plays audio.
OUT=${1:-/data/local/tmp/m3x-state}
mkdir -p "$OUT" || exit 1
{
    date
    uname -a
    for service in zygote zygote_secondary surfaceflinger thermal-engine; do
        echo "$service: $(getprop "init.svc.$service")"
    done
    pidof rockbox rockbox-dbg
    cat /proc/meminfo /proc/partitions /proc/mounts
    for zone in /sys/class/thermal/thermal_zone*; do
        [ -f "$zone/type" ] || continue
        echo "$zone: $(cat "$zone/type") $(cat "$zone/temp")"
    done
    for name in capacity status voltage_now temp; do
        node=/sys/class/power_supply/battery/$name
        [ -f "$node" ] || continue
        echo "$node"; cat "$node"
    done
    for node in /sys/class/block/mmcblk*/uevent; do
        [ -f "$node" ] || continue
        echo "$node"; cat "$node"
    done
    for host in /sys/class/mmc_host/mmc1/mmc1:*; do
        [ -d "$host" ] || continue
        for name in type name uevent; do
            [ -f "$host/$name" ] && cat "$host/$name"
        done
    done
} > "$OUT/state.txt" 2>&1
tail -n 250 /data/local/tmp/rockbox-m3x.log > "$OUT/launcher.txt" 2>&1
logcat -b crash -d -t 200 > "$OUT/crash-log.txt" 2>&1
for tomb in /data/tombstones/tombstone*; do
    [ -f "$tomb" ] || continue
    if head -n 20 "$tomb" | grep -q '>>> .*rockbox'; then
        cp "$tomb" "$OUT/${tomb##*/}"
    fi
done
echo "Saved diagnostics to $OUT"
