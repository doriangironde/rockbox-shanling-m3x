/***************************************************************************
 * Button handling for the Shanling M3X.
 *
 * Verified with getevent -pl: qpnp_pon reports power and next; gpio-keys
 * reports volume up/down, play/pause and previous. Goodix-CTP uses ten
 * protocol-B slots, with inclusive axis ranges X 0..720, Y 0..1280.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 ****************************************************************************/

#include <linux/input.h>
#include <string.h>
#include "config.h"
#include "button.h"
#include "button-ibasso.h"

#define M3X_TOUCH_SLOTS 10

static struct touch_slot {
    bool active, have_x, have_y;
    int x, y;
} slots[M3X_TOUCH_SLOTS];
static int selected_slot;
static int primary_slot = -1;
static bool suppress_contacts;
static bool dropped;
static bool touch_enabled = true;

static int scale_axis(int value, int maximum, int pixels)
{
    if (value < 0) value = 0;
    if (value > maximum) value = maximum;
    return (value * (pixels - 1) + maximum / 2) / maximum;
}

static void release_touch(void)
{
    handle_touchscreen_event(EVENT_CODE_TOUCHSCREEN,
                            EVENT_VALUE_TOUCHSCREEN_RELEASE);
    primary_slot = -1;
}

void touchscreen_enable_device(bool enable)
{
    touch_enabled = enable;
    release_touch();
    suppress_contacts = false;
    for (int i = 0; i < M3X_TOUCH_SLOTS; ++i)
        suppress_contacts |= slots[i].active;
}

/* Publish complete frames. Never transfer a held selection to a second finger. */
static void report_touch(void)
{
    bool any_active = false;
    for (int i = 0; i < M3X_TOUCH_SLOTS; ++i)
        any_active |= slots[i].active;

    if (primary_slot >= 0 &&
        (suppress_contacts || !slots[primary_slot].active))
    {
        release_touch();
        suppress_contacts = true;
    }
    if (!any_active)
        suppress_contacts = false;
    if (!touch_enabled)
        return;
    if (primary_slot < 0 && !suppress_contacts)
    {
        for (int i = 0; i < M3X_TOUCH_SLOTS; ++i)
            if (slots[i].active && slots[i].have_x && slots[i].have_y)
            {
                primary_slot = i;
                break;
            }
    }
    if (primary_slot >= 0)
    {
        struct touch_slot *slot = &slots[primary_slot];
        handle_touchscreen_event(EVENT_CODE_TOUCHSCREEN_X,
                                 scale_axis(slot->x, 720, LCD_WIDTH));
        handle_touchscreen_event(EVENT_CODE_TOUCHSCREEN_Y,
                                 scale_axis(slot->y, 1280, LCD_HEIGHT));
        handle_touchscreen_event(EVENT_CODE_TOUCHSCREEN,
                                 EVENT_VALUE_TOUCHSCREEN_PRESS);
    }
}

int handle_button_event(__u16 code, __s32 value, int last_btns)
{
    int button;
    switch (code)
    {
        case KEY_POWER:        button = BUTTON_POWER; break;
        case KEY_VOLUMEUP:     button = BUTTON_VOL_UP; break;
        case KEY_VOLUMEDOWN:   button = BUTTON_VOL_DOWN; break;
        case KEY_PLAYPAUSE:    button = BUTTON_PLAY; break;
        case KEY_PREVIOUSSONG: button = BUTTON_LEFT; break;
        case KEY_NEXTSONG:     button = BUTTON_RIGHT; break;
        default: return last_btns;
    }
    if (value == 1 || value == 2) /* press or Linux autorepeat */
        return last_btns | button;
    if (value == 0)
        return last_btns & ~button;
    return last_btns;
}

bool handle_touchscreen_target_event(__u16 type, __u16 code, __s32 value)
{
    if (type != EV_ABS && type != EV_SYN)
        return false; /* Goodix also reports KEY_POWER. */
    if (type == EV_SYN)
    {
        if (code == SYN_DROPPED)
        {
            /* Fail closed after evdev overflow; require a fresh contact. */
            release_touch();
            memset(slots, 0, sizeof(slots));
            selected_slot = 0;
            suppress_contacts = false;
            dropped = true;
        }
        else if (code == SYN_REPORT)
        {
            if (dropped) dropped = false;
            else report_touch();
        }
        return true;
    }
    if (dropped)
        return true;
    if (code == ABS_MT_SLOT)
    {
        selected_slot = value >= 0 && value < M3X_TOUCH_SLOTS ? value : -1;
        return true;
    }
    if (selected_slot < 0)
        return true;
    struct touch_slot *slot = &slots[selected_slot];
    switch (code)
    {
        case ABS_MT_TRACKING_ID:
            if (primary_slot == selected_slot && value < 0)
            {
                /* Delay release until the frame boundary, even if the slot
                 * is reused within this frame. */
                suppress_contacts = true;
            }
            slot->active = value >= 0;
            break;
        case ABS_MT_POSITION_X: slot->x = value; slot->have_x = true; break;
        case ABS_MT_POSITION_Y: slot->y = value; slot->have_y = true; break;
    }
    return true;
}
