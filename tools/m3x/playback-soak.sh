#!/system/bin/sh
# Run as root from Android. The real boot module remains disabled. Commands
# in OUT/command: tap X Y, capture LABEL, lock, unlock, duplicate, finish. X/Y are panel
# coordinates. The explicit 20-minute guard is independent of host/USB state.
MOD=/data/adb/modules/rockbox_m3x
OUT=${1:-/data/local/tmp/m3x-soak-20261009}
STAGE=$OUT/module
case "$OUT" in /data/local/tmp/m3x-soak-*) ;; *) exit 2;; esac
case "$OUT" in *..*) exit 2;; esac
if [ "${2:-}" != detached ]; then
    [ -e "$MOD/disable" ] && ! pidof rockbox rockbox-dbg >/dev/null || exit 1
    [ ! -e "$STAGE" ] || exit 1
    expected=$(cat /data/local/tmp/m3x-playback-observer.md5) || exit 1
    actual=$(md5sum "$MOD/rockbox") || exit 1
    [ "$expected" = "${actual%% *}" ] || {
        echo "Observer does not match installed ELF; rebuild it first." >&2
        exit 1
    }
    mkdir -p "$OUT" || exit 1
    nohup sh "$0" "$OUT" detached > "$OUT/runner.log" 2>&1 < /dev/null &
    exit 0
fi
sleep 10
umask 022
mkdir -p "$STAGE"
cp "$MOD/service.sh" "$STAGE/service.sh" || exit 1
ln -s "$MOD/rockbox" "$STAGE/rockbox" || exit 1
rm -f "$STAGE/disable"
cp /data/media/0/.rockbox/config.cfg "$OUT/config.before.cfg"
cp /data/media/0/.rockbox/.playlist_control "$OUT/playlist.before" 2>/dev/null
ls -l /data/tombstones > "$OUT/tombstones.before"
dmesg > "$OUT/kernel.before.txt"
LOG_START=$(wc -c < /data/local/tmp/rockbox-m3x.log)
for zone in /sys/class/thermal/thermal_zone*; do
    case "$(cat "$zone/type")" in
        tsens_tz_sensor0) CPU=$zone/temp;;
        battery) BATTERY=$zone/temp;;
    esac
done
snapshot() {
    echo "=== $1 ==="
    cat /proc/uptime /proc/stat
    echo "thermal cpu $(cat "$CPU") battery $(cat "$BATTERY")"
    echo "backlight $(cat /sys/class/leds/lcd-backlight/brightness)"
    echo "rockbox $(pidof rockbox)"
    for rb in $(pidof rockbox); do
        cat /proc/$rb/stat
        /data/local/tmp/m3x-playback-observer "$rb"
        echo "observer_status $?"
    done
    cat /proc/asound/card0/pcm0p/sub0/status /proc/asound/card0/pcm0p/sub0/hw_params
    for node in status capacity current_now voltage_now temp; do
        echo "battery $node $(cat /sys/class/power_supply/battery/$node)"
    done
    echo "usb_online $(cat /sys/class/power_supply/usb/online)"
    echo "thermal_service $(getprop init.svc.thermal-engine)"
}
key() {
    sendevent /dev/input/event0 1 116 1
    sendevent /dev/input/event0 0 0 0
    sleep 0.1
    sendevent /dev/input/event0 1 116 0
    sendevent /dev/input/event0 0 0 0
    sleep 1
}
set_lock() {
    actual=$(/data/local/tmp/m3x-playback-observer "$(pidof rockbox)" | sed -n 's/^screen_locked //p')
    case "$actual" in 0|1) ;; *) return 1;; esac
    [ "$actual" = "$1" ] || key
    LOCKED=$1
    LOCK_SAMPLES=0
}
capture() {
    /data/local/tmp/fbdump /dev/graphics/fb0 "$OUT/$1.rgba" >/dev/null 2>&1
    chmod 644 "$OUT/$1.rgba"
}
cleanup() {
    touch "$STAGE/disable"
    [ -n "$SVC" ] && kill -TERM "$SVC" 2>/dev/null
    [ -n "$GUARD" ] && kill "$GUARD" 2>/dev/null
}
trap 'exit 1' HUP INT TERM
trap cleanup EXIT
snapshot baseline > "$OUT/samples.txt"
sh "$STAGE/service.sh" &
SVC=$!
(sleep 1200; touch "$STAGE/disable"; kill -TERM "$SVC" 2>/dev/null) &
GUARD=$!
sleep 16
capture menu
touch "$OUT/ready"
LOCKED=0
LOCK_SAMPLES=0
STARTED=0
STOPPED=0
for sample in $(seq 1 580); do
    kill -0 "$SVC" 2>/dev/null || break
    if [ -f "$OUT/command" ]; then
        read -r verb a b < "$OUT/command"
        rm -f "$OUT/command"
        echo "command $sample $verb $a $b" >> "$OUT/events.txt"
        case "$verb" in
            tap)
                case "$a:$b" in *[!0-9:]*|:*|*:) continue;; esac
                [ "$a" -le 767 ] && [ "$b" -le 1279 ] || continue
                sendevent /dev/input/event1 3 47 0
                sendevent /dev/input/event1 3 57 101
                sendevent /dev/input/event1 3 53 $((a * 720 / 767))
                sendevent /dev/input/event1 3 54 "$b"
                sendevent /dev/input/event1 0 0 0
                sleep 0.12
                sendevent /dev/input/event1 3 57 -1
                sendevent /dev/input/event1 0 0 0
                sleep 1
                capture "tap-$sample";;
            lock) set_lock 1;;
            unlock) set_lock 0;;
            capture) case "$a" in ''|*[!a-zA-Z0-9-]*) ;; *) capture "$a";; esac;;
            duplicate)
                sh "$STAGE/service.sh"
                echo "duplicate_status $? zygote $(getprop init.svc.zygote) display $(getprop init.svc.surfaceflinger)" >> "$OUT/events.txt";;
            finish) break;;
        esac
    fi
    snapshot "$sample" >> "$OUT/samples.txt" 2>&1
    if [ "$LOCKED" = 1 ]; then
        LOCK_SAMPLES=$((LOCK_SAMPLES + 1))
        # Allow the configured fade-out to finish before flagging relighting.
        if [ "$LOCK_SAMPLES" -gt 4 ] && [ "$(cat /sys/class/leds/lcd-backlight/brightness)" != 0 ]; then
            echo "screen relit while locked $sample" >> "$OUT/events.txt"
        fi
    fi
    if cat /proc/asound/card0/pcm0p/sub0/status | /system/bin/grep -q RUNNING; then
        STARTED=1
        STOPPED=0
    elif [ "$STARTED" = 1 ]; then
        STOPPED=$((STOPPED + 1))
        if [ "$STOPPED" -ge 4 ]; then
            echo "playback ended sample $sample" >> "$OUT/events.txt"
            set_lock 0
            capture ended
            break
        fi
    fi
    sleep 2
done
snapshot before-stop >> "$OUT/samples.txt" 2>&1
cp /data/media/0/.rockbox/.playlist_control "$OUT/playlist.after" 2>/dev/null
cleanup
wait "$SVC" 2>/dev/null
SVC=
sleep 8
snapshot restored >> "$OUT/samples.txt" 2>&1
for service in zygote zygote_secondary surfaceflinger thermal-engine; do
    echo "$service $(getprop init.svc.$service)" >> "$OUT/restored.txt"
done
ls -l /data/tombstones > "$OUT/tombstones.after"
dmesg > "$OUT/kernel.after.txt"
tail -c +$((LOG_START + 1)) /data/local/tmp/rockbox-m3x.log > "$OUT/player.log"
chmod -R a+rX "$OUT"
echo complete > "$OUT/done"
trap - EXIT HUP INT TERM
