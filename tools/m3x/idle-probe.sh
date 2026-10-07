#!/system/bin/sh
# Bounded idle diagnostic; leaves the boot module disabled and plays no audio.
BIN=${1:-/data/local/tmp/rockbox-dbg}
OUT=${2:-/data/local/tmp/m3x-idle-probe}
DISPLAY=${3:-android}
DURATION=${4:-5}
case "$DISPLAY" in android|takeover|native) ;; *) exit 1 ;; esac
case "$DURATION" in ''|*[!0-9]*) exit 1 ;; esac
[ "$DURATION" -ge 1 ] && [ "$DURATION" -le 300 ] || exit 1
if [ ! -e /data/adb/modules/rockbox_m3x/disable ] || pidof rockbox rockbox-dbg >/dev/null; then
    echo "Disable the boot module and stop Rockbox before probing." >&2
    exit 1
fi
snapshot() {
    echo "=== $1 ==="
    cat /proc/uptime /proc/loadavg /proc/stat
    for z in /sys/class/thermal/thermal_zone*; do
        echo "thermal $(cat "$z/type") $(cat "$z/temp")"
    done
    for service in zygote zygote_secondary surfaceflinger thermal-engine; do
        echo "service $service $(getprop init.svc.$service)"
    done
    cat /proc/[0-9]*/stat 2>/dev/null
    if [ -n "$RB" ]; then
        for t in /proc/$RB/task/*; do
            [ -e "$t/stat" ] || continue
            cat "$t/stat"
            echo "wchan $(cat "$t/wchan")"
        done
    fi
}
cleanup() {
    [ -n "$RB" ] && kill -9 "$RB" 2>/dev/null
    [ -n "$GUARD" ] && kill "$GUARD" 2>/dev/null
    echo rockbox > /sys/power/wake_unlock
    if [ "$DISPLAY" != android ]; then
        start surfaceflinger
        if [ "$DISPLAY" = native ]; then
            start zygote
            start zygote_secondary
        fi
        echo "$BRIGHTNESS" > /sys/class/leds/lcd-backlight/brightness
    fi
}
trap 'exit 1' HUP INT TERM
trap cleanup EXIT
snapshot baseline > "$OUT.txt"
BRIGHTNESS=$(cat /sys/class/leds/lcd-backlight/brightness)
if [ "$DISPLAY" = native ]; then
    stop zygote
    stop zygote_secondary
    sleep 2
    [ "$(getprop init.svc.zygote)" = stopped ] &&
        [ "$(getprop init.svc.zygote_secondary)" = stopped ] || exit 1
    pidof system_server >/dev/null && exit 1
fi
if [ "$DISPLAY" != android ]; then
    stop surfaceflinger
fi
"$BIN" > "$OUT.log" 2>&1 &
RB=$!
(sleep $((DURATION + 15)); kill -9 "$RB" 2>/dev/null
 echo rockbox > /sys/power/wake_unlock
 if [ "$DISPLAY" != android ]; then
     start surfaceflinger
     if [ "$DISPLAY" = native ]; then start zygote; start zygote_secondary; fi
     echo "$BRIGHTNESS" > /sys/class/leds/lcd-backlight/brightness
 fi) &
GUARD=$!
sleep 3
if [ "$DISPLAY" != android ]; then
    /data/local/tmp/fbpan --blank 0 >> "$OUT.log" 2>&1
    /data/local/tmp/fbpan --pan 0 >> "$OUT.log" 2>&1
fi
snapshot start >> "$OUT.txt"
elapsed=0
read uptime rest < /proc/uptime
deadline=$((${uptime%.*} + DURATION))
while read uptime rest < /proc/uptime && [ "${uptime%.*}" -lt "$deadline" ]; do
    sleep 1
    elapsed=$((elapsed + 1))
    kill -0 "$RB" 2>/dev/null || break
    for z in /sys/class/thermal/thermal_zone*; do
        case "$(cat "$z/type")" in
            tsens_tz_sensor*)
                if ! [ "$(cat "$z/temp")" -lt 55 ]; then
                    snapshot cutoff >> "$OUT.txt"
                    exit 2
                fi ;;
            battery)
                if ! [ "$(cat "$z/temp")" -lt 40000 ]; then
                    snapshot cutoff >> "$OUT.txt"
                    exit 2
                fi ;;
        esac
    done
    if [ $((elapsed % 10)) -eq 0 ]; then
        snapshot "sample-$elapsed" >> "$OUT.txt"
    fi
done
snapshot end >> "$OUT.txt"
cleanup
trap - EXIT HUP INT TERM
RB=
GUARD=
sleep 2
snapshot restored >> "$OUT.txt"
