#!/system/bin/sh
# Runs from Magisk late_start. Keep Android thermal services running.
MODDIR=${0%/*}
LOG=/data/local/tmp/rockbox-m3x.log
FB=/data/local/tmp/fbpan

[ -e "$MODDIR/disable" ] && exit 0

log() {
    echo "$(date): $*" >> "$LOG"
}

# The vendor CPU sensor reports degrees C; battery reports millidegrees C.
for zone in /sys/class/thermal/thermal_zone*; do
    case "$(cat "$zone/type")" in
        tsens_tz_sensor0) CPU_TEMP=$zone/temp ;;
        battery) BATTERY_TEMP=$zone/temp ;;
    esac
done

temperature_ok() {
    cpu=$(cat "$CPU_TEMP" 2>/dev/null)
    battery=$(cat "$BATTERY_TEMP" 2>/dev/null)
    case "$cpu:$battery" in
        *[!0-9:]*|:*|*:) log "temperature sensors unavailable"; return 1 ;;
    esac
    if [ "$cpu" -ge 55 ] || [ "$battery" -ge 40000 ]; then
        log "thermal stop: CPU ${cpu}C, battery ${battery}mC"
        return 2
    fi
    return 0
}

wait_for_cool_start() {
    # Android's boot burst can exceed the test cutoff before Rockbox starts.
    # Let it settle with its display and native thermal services still active.
    # Bad sensor reads abort immediately; heat waits at most 115 seconds.
    attempt=0
    while :; do
        [ -e "$MODDIR/disable" ] && return 1
        if temperature_ok; then
            return 0
        else
            status=$?
        fi
        [ "$status" -eq 2 ] || return 1
        attempt=$((attempt + 1))
        [ "$attempt" -ge 24 ] && return 1
        [ "$attempt" -eq 1 ] && log "waiting for device to cool before startup"
        sleep 5
    done
}

restore_android() {
    # An exit must never automatically relaunch the process on this boot or
    # the next one. The operator can enable the module after investigating.
    touch "$MODDIR/disable"
    [ -n "$MONITOR" ] && kill "$MONITOR" 2>/dev/null
    [ -n "$RB" ] && kill -9 "$RB" 2>/dev/null
    echo rockbox > /sys/power/wake_unlock
    start surfaceflinger
    start zygote
    start zygote_secondary
    echo "$BRIGHTNESS" > /sys/class/leds/lcd-backlight/brightness
    log "Rockbox stopped; module disabled; Android display restored"
}

BRIGHTNESS=$(cat /sys/class/leds/lcd-backlight/brightness)
trap 'exit 1' HUP INT TERM
trap restore_android EXIT
wait_for_cool_start || exit 1
if pidof rockbox rockbox-dbg >/dev/null; then
    log "Rockbox already running; refusing a second instance"
    exit 1
fi

# Leaving system_server alive without SurfaceFlinger blocks its UI thread,
# triggers the watchdog and repeatedly restarts the Android framework. Stop
# both app runtimes first; native services (including thermal-engine) stay up.
stop zygote
stop zygote_secondary
for attempt in 1 2 3 4 5; do
    [ "$(getprop init.svc.zygote)" = stopped ] &&
        [ "$(getprop init.svc.zygote_secondary)" = stopped ] && break
    sleep 1
done
if [ "$(getprop init.svc.zygote)" != stopped ] ||
   [ "$(getprop init.svc.zygote_secondary)" != stopped ] || pidof system_server >/dev/null; then
    log "Android framework did not stop; refusing display takeover"
    exit 1
fi

# SurfaceFlinger must exit to release its overlay pipes.
stop surfaceflinger
for attempt in 1 2 3 4 5; do
    [ "$(getprop init.svc.surfaceflinger)" = stopped ] && break
    sleep 1
done
if [ "$(getprop init.svc.surfaceflinger)" != stopped ]; then
    log "SurfaceFlinger did not stop; refusing display takeover"
    exit 1
fi
"$MODDIR/rockbox" >> "$LOG" 2>&1 &
RB=$!
log "rockbox pid $RB"

# Watch the child from a separate process, including during initial display
# setup. The disable marker also stops an already running session.
(
    while kill -0 "$RB" 2>/dev/null; do
        if [ -e "$MODDIR/disable" ] || ! temperature_ok; then
            touch "$MODDIR/disable"
            kill -9 "$RB" 2>/dev/null
            break
        fi
        sleep 5
    done
) &
MONITOR=$!

# Keep the proven initial presentation sequence. Continuous video scanout
# then reflects writes to page 0 without repeated pan ioctls.
sleep 12
kill -0 "$RB" 2>/dev/null || exit 1
"$FB" --blank 0 >> "$LOG" 2>&1 || exit 1
"$FB" --pan 0 >> "$LOG" 2>&1 || exit 1
echo 200 > /sys/class/leds/lcd-backlight/brightness
log "initial blank+pan done"

wait "$RB"
STATUS=$?
RB=
log "rockbox exited ($STATUS)"
