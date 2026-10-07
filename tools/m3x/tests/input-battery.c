/* Run on the M3X. Includes the real input loop, fed through Linux pipes.
 * This exercises frame draining, device routing and legacy hold exclusion,
 * as well as the target decoder. No events reach Android or physical inputs. */
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include "../../../rockbox/firmware/target/hosted/ibasso/button-ibasso.c"

long current_tick;
static int capacity;
static bool capacity_readable = true;
static bool live_sysfs;
static int hold_calls;
void panicf(const char *fmt, ...) { (void)fmt; abort(); }
void backlight_hold_changed(bool held) { (void)held; ++hold_calls; }
int touchscreen_to_pixels(int x, int y, int *data)
{
    *data = (x << 16) | y;
    return BUTTON_TOUCHSCREEN;
}
bool sysfs_get_int(const char *path, int *value)
{
    if (live_sysfs)
    {
        FILE *file = fopen(path, "r");
        if (!file) return false;
        bool ok = fscanf(file, "%d", value) == 1;
        fclose(file);
        return ok;
    }
    if (strcmp(path, sysfs_paths[SYSFS_BATTERY_CAPACITY]) == 0)
    {
        *value = capacity;
        return capacity_readable;
    }
    *value = 1;
    return true;
}
bool sysfs_get_string(const char *path, char *value, int size)
{ (void)path; (void)value; (void)size; return false; }
int _battery_level(void);
void touchscreen_enable_device(bool);

static int touch_write, key_write;
static void event(int fd, int type, int code, int value)
{
    struct input_event e;
    memset(&e, 0, sizeof(e));
    e.type = type; e.code = code; e.value = value;
    assert(write(fd, &e, sizeof(e)) == sizeof(e));
}
static void abs_event(int code, int value) { event(touch_write, EV_ABS, code, value); }
static void sync_touch(void) { event(touch_write, EV_SYN, SYN_REPORT, 0); }
static void contact(int slot, int id, int x, int y)
{
    abs_event(ABS_MT_SLOT, slot);
    abs_event(ABS_MT_TRACKING_ID, id);
    abs_event(ABS_MT_POSITION_X, x);
    abs_event(ABS_MT_POSITION_Y, y);
}
static int read_buttons(int x, int y)
{
    int data;
    int buttons = button_read_device(&data);
    assert(data == ((x << 16) | y));
    return buttons;
}
int main(void)
{
    int touch_pipe[2], key_pipe[2];
    assert(pipe(touch_pipe) == 0 && pipe(key_pipe) == 0);
    struct pollfd fds[2] = {{touch_pipe[0], POLLIN, 0}, {key_pipe[0], POLLIN, 0}};
    _fds = fds; _nfds = 2; _touch_fd = touch_pipe[0];
    touch_write = touch_pipe[1]; key_write = key_pipe[1];

    /* Partial frame plus another device's SYN must not publish a touch. */
    contact(0, 45, 720, 1280);
    event(key_write, EV_SYN, SYN_REPORT, 0);
    assert(read_buttons(0, 0) == 0);
    sync_touch();
    assert(read_buttons(767, 1279) == BUTTON_TOUCHSCREEN);

    /* A secondary finger's movement and release leave the primary intact. */
    contact(1, 46, 0, 0); sync_touch();
    assert(read_buttons(767, 1279) == BUTTON_TOUCHSCREEN);
    abs_event(ABS_MT_TRACKING_ID, -1); sync_touch();
    assert(read_buttons(767, 1279) == BUTTON_TOUCHSCREEN);

    /* X-only motion retains Y; clamp malformed coordinates. */
    abs_event(ABS_MT_SLOT, 0); abs_event(ABS_MT_POSITION_X, -50); sync_touch();
    assert(read_buttons(0, 1279) == BUTTON_TOUCHSCREEN);
    abs_event(ABS_MT_POSITION_X, 9999); sync_touch();
    assert(read_buttons(767, 1279) == BUTTON_TOUCHSCREEN);

    /* Lifting the primary must not transfer selection to a held second finger. */
    contact(1, 47, 360, 640); sync_touch();
    assert(read_buttons(767, 1279) == BUTTON_TOUCHSCREEN);
    abs_event(ABS_MT_SLOT, 0); abs_event(ABS_MT_TRACKING_ID, -1); sync_touch();
    assert(read_buttons(767, 1279) == 0);
    abs_event(ABS_MT_SLOT, 1); abs_event(ABS_MT_POSITION_X, 0); sync_touch();
    assert(read_buttons(767, 1279) == 0);
    abs_event(ABS_MT_TRACKING_ID, -1); sync_touch();
    assert(read_buttons(767, 1279) == 0);

    /* Queued tap frames must yield press then release on separate ticks. */
    contact(0, 48, 0, 0); sync_touch();
    abs_event(ABS_MT_TRACKING_ID, -1); sync_touch();
    assert(read_buttons(0, 0) == BUTTON_TOUCHSCREEN);
    assert(read_buttons(0, 0) == 0);

    /* An invalid slot cannot write over the valid slot. */
    contact(0, 49, 360, 640); sync_touch();
    assert(read_buttons(384, 640) == BUTTON_TOUCHSCREEN);
    contact(10, 50, 0, 0); sync_touch();
    assert(read_buttons(384, 640) == BUTTON_TOUCHSCREEN);

    /* Overflow releases the pointer and ignores events to the next boundary. */
    event(touch_write, EV_SYN, SYN_DROPPED, 0);
    contact(0, 51, 0, 0); sync_touch();
    assert(read_buttons(384, 640) == 0);
    contact(0, 52, 0, 0); sync_touch();
    assert(read_buttons(0, 0) == BUTTON_TOUCHSCREEN);
    abs_event(ABS_MT_TRACKING_ID, -1); sync_touch();
    assert(read_buttons(0, 0) == 0);

    /* Software touch lock releases the current selection. Unlocking with a
     * finger still down must not synthesize a new selection. */
    contact(0, 53, 0, 0); sync_touch();
    assert(read_buttons(0, 0) == BUTTON_TOUCHSCREEN);
    touchscreen_enable_device(false);
    abs_event(ABS_MT_POSITION_X, 720); sync_touch();
    assert(read_buttons(0, 0) == 0);
    touchscreen_enable_device(true); sync_touch();
    assert(read_buttons(0, 0) == 0);
    abs_event(ABS_MT_TRACKING_ID, -1); sync_touch();
    assert(read_buttons(0, 0) == 0);
    contact(0, 54, 720, 1280); sync_touch();
    assert(read_buttons(767, 1279) == BUTTON_TOUCHSCREEN);
    abs_event(ABS_MT_TRACKING_ID, -1); sync_touch();
    assert(read_buttons(767, 1279) == 0);
    /* Reset the last coordinate for the key-only assertions below. */
    contact(0, 55, 0, 0); sync_touch();
    assert(read_buttons(0, 0) == BUTTON_TOUCHSCREEN);
    abs_event(ABS_MT_TRACKING_ID, -1); sync_touch();
    assert(read_buttons(0, 0) == 0);

    /* The shared core must not intercept KEY_VOLUMEUP as an iBasso hold key. */
    event(key_write, EV_KEY, KEY_VOLUMEUP, 1);
    assert(read_buttons(0, 0) == BUTTON_VOL_UP && hold_calls == 0);
    event(key_write, EV_KEY, BTN_TOOL_FINGER, 0);
    assert(read_buttons(0, 0) == BUTTON_VOL_UP);
    event(key_write, EV_KEY, KEY_VOLUMEUP, 2);
    assert(read_buttons(0, 0) == BUTTON_VOL_UP);
    event(key_write, EV_KEY, KEY_VOLUMEUP, 0);
    assert(read_buttons(0, 0) == 0);
    const int codes[] = {KEY_POWER, KEY_VOLUMEDOWN, KEY_PLAYPAUSE, KEY_PREVIOUSSONG, KEY_NEXTSONG};
    const int masks[] = {BUTTON_POWER, BUTTON_VOL_DOWN, BUTTON_PLAY, BUTTON_LEFT, BUTTON_RIGHT};
    for (unsigned i = 0; i < sizeof(codes)/sizeof(codes[0]); ++i)
    {
        event(key_write, EV_KEY, codes[i], 1);
        assert(read_buttons(0, 0) == masks[i]);
        event(key_write, EV_KEY, codes[i], 0);
        assert(read_buttons(0, 0) == 0);
    }

    /* Queued key frames must not disappear while touch drains a long frame. */
    contact(0, 56, 0, 0); sync_touch();
    event(key_write, EV_KEY, KEY_PLAYPAUSE, 1);
    event(key_write, EV_SYN, SYN_REPORT, 0);
    event(key_write, EV_KEY, KEY_PLAYPAUSE, 0);
    event(key_write, EV_SYN, SYN_REPORT, 0);
    assert(read_buttons(0, 0) == (BUTTON_TOUCHSCREEN | BUTTON_PLAY));
    abs_event(ABS_MT_TRACKING_ID, -1); sync_touch();
    assert(read_buttons(0, 0) == 0);

    for (capacity = 0; capacity <= 100; ++capacity)
        assert(_battery_level() == capacity);
    capacity = -1; assert(_battery_level() == -1);
    capacity = 101; assert(_battery_level() == -1);
    capacity = 75; capacity_readable = false; assert(_battery_level() == -1);
#ifndef M3X_OFFLINE_TEST
    live_sysfs = true;
    int actual;
    assert(sysfs_get_int(sysfs_paths[SYSFS_BATTERY_CAPACITY], &actual));
    assert(_battery_level() == actual);
    printf("PASS: input routing, touch frames, multitouch, keys, gauge validation; live battery %d%%\n", actual);
#else
    puts("PASS: offline input routing, touch frames, multitouch, queued keys, gauge validation");
#endif
    return 0;
}
