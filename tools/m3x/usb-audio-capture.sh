#!/system/bin/sh
# Bounded read-only capture that survives unplugging ADB and Android takeover.
# Run from Android as root; it never opens audio, changes routes or stops music.
OUT=${1:-/data/local/tmp/m3x-usb-audio-$(date +%s)}
CAPTURE_SECONDS=${2:-300}
case "$OUT" in /data/local/tmp/m3x-usb-audio-*) ;; *) exit 2;; esac
case "$OUT" in *..*) exit 2;; esac
case "$CAPTURE_SECONDS" in ''|*[!0-9]*) exit 2;; esac
[ "$CAPTURE_SECONDS" -ge 30 ] && [ "$CAPTURE_SECONDS" -le 600 ] || exit 2
[ "$(id -u)" = 0 ] || { echo 'Root access is required.' >&2; exit 1; }

if [ "$3" != detached ]; then
    [ ! -e "$OUT" ] || { echo 'Use a new capture directory.' >&2; exit 1; }
    umask 022
    mkdir -p "$OUT" || exit 1
    nohup setsid /system/bin/sh "$0" "$OUT" "$CAPTURE_SECONDS" detached \
        </dev/null >"$OUT/runner.log" 2>&1 &
    echo "Recording for ${CAPTURE_SECONDS}s in $OUT; USB may now be disconnected."
    exit 0
fi

umask 022
OBSERVER=/data/local/tmp/m3x-playback-observer
EXPECTED=$(cat "$OBSERVER.md5" 2>/dev/null)
OBSERVED_PID=
OBSERVER_OK=0
read_node() {
    [ -f "$1" ] || return 0
    echo "--- $1"
    cat "$1" 2>&1
}

snapshot() {
    echo "=== sample $1 ==="
    cat /proc/uptime
    for service in zygote zygote_secondary surfaceflinger thermal-engine; do
        echo "$service $(getprop "init.svc.$service")"
    done
    player=$(pidof rockbox rockbox-dbg)
    echo "rockbox $player"
    for rb in $player; do
        # Read process memory only with the observer built for this exact ELF.
        if [ "$OBSERVED_PID" != "$rb" ]; then
            OBSERVED_PID=$rb
            OBSERVER_OK=0
            actual=$(md5sum "/proc/$rb/exe" 2>/dev/null)
            [ -n "$EXPECTED" ] && [ "$EXPECTED" = "${actual%% *}" ] && OBSERVER_OK=1
        fi
        if [ "$OBSERVER_OK" = 1 ] && [ -x "$OBSERVER" ]; then
            "$OBSERVER" "$rb"
            echo "observer_status $?"
        fi
    done
    for zone in /sys/class/thermal/thermal_zone*; do
        case "$(cat "$zone/type" 2>/dev/null)" in
            tsens_tz_sensor0|battery) read_node "$zone/temp";;
        esac
    done
    read_node /sys/class/leds/lcd-backlight/brightness
    for node in /proc/asound/cards /proc/asound/pcm /proc/asound/card*/id /proc/asound/card*/usbid \
        /proc/asound/card*/stream* /proc/asound/card*/pcm*p/sub*/status \
        /proc/asound/card*/pcm*p/sub*/hw_params; do
        read_node "$node"
    done
    ls -l /dev/snd 2>&1
    for device in /sys/bus/usb/devices/*; do
        [ -d "$device" ] || continue
        for name in idVendor idProduct manufacturer product speed bDeviceClass \
            bInterfaceClass bInterfaceSubClass bInterfaceProtocol authorized; do
            read_node "$device/$name"
        done
        [ ! -L "$device/driver" ] || echo "$device driver $(readlink "$device/driver")"
    done
    for node in /sys/class/dual_role_usb/*/mode /sys/class/dual_role_usb/*/data_role \
        /sys/class/usb_role/*/role /sys/class/typec/*/data_role \
        /sys/class/power_supply/usb/present /sys/class/power_supply/usb/online \
        /sys/class/power_supply/usb/type /sys/class/power_supply/usb/otg; do
        read_node "$node"
    done
}

finish() {
    dmesg >"$OUT/kernel.after.txt" 2>&1
    tail -n 150 /data/local/tmp/rockbox-m3x.log >"$OUT/player-tail.txt" 2>&1
    echo "Capture finished; no audio or USB settings were changed." >"$OUT/done"
}
trap 'exit 1' HUP INT TERM
trap finish EXIT
date >"$OUT/start.txt"
dmesg >"$OUT/kernel.before.txt" 2>&1
snapshot 0 >"$OUT/samples.txt" 2>&1
count=0
while [ "$count" -lt "$(( (CAPTURE_SECONDS + 4) / 5 ))" ]; do
    sleep 5
    count=$((count + 1))
    snapshot "$count" >>"$OUT/samples.txt" 2>&1
done
