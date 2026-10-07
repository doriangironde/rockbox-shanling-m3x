/***************************************************************************
 *             __________               __   ___
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2014 by Ilia Sergachev: Initial Rockbox port to iBasso DX50
 * Copyright (C) 2014 by Mario Basister: iBasso DX90 port
 * Copyright (C) 2014 by Simon Rothen: Initial Rockbox repository submission, additional features
 * Copyright (C) 2014 by Udo Schläpfer: Code clean up, additional features
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/


#include <stdbool.h>

#include "config.h"
#include "debug.h"
#include "lcd.h"
#include "panic.h"

#include "debug-ibasso.h"
#include "sysfs-ibasso.h"


/*
    Prevent excessive backlight_hw_on usage.
    Required for proper seeking.
*/
static bool _backlight_enabled = false;


#ifdef SHANLING_M3X
/* Backlight level Rockbox turns the panel on at. */
#define M3X_BACKLIGHT_DEFAULT DEFAULT_BRIGHTNESS_SETTING
/* Hardware max_brightness, read in backlight_hw_init(). */
static int _backlight_max = 0;
#endif

/*
    Prevent excessive backlight_hw_brightness usage.
    Required for proper seeking.
*/
static int _current_brightness = -1;


bool backlight_hw_init(void)
{
    TRACE;

#ifdef SHANLING_M3X
    /*
        The M3X panel backlight is a LED-class device. Android defaults it to
        200/255; do the same and remember the hardware maximum so
        backlight_hw_brightness() can scale Rockbox' 0..255 request.
    */
    _backlight_max = 0;
    if(! sysfs_get_int(sysfs_paths[SYSFS_BACKLIGHT_MAX_BRIGHTNESS], &_backlight_max))
    {
        _backlight_max = 0;
    }
    if(_backlight_max <= 0)
    {
        _backlight_max = 255;
    }
    DEBUGF("DEBUG %s: hardware max_brightness: %d.", __func__, _backlight_max);

    /* Round (M3X_BACKLIGHT_DEFAULT * _backlight_max) / MAX_BRIGHTNESS_SETTING. */
    int brightness = _current_brightness >= 0 ? _current_brightness
                                            : M3X_BACKLIGHT_DEFAULT;
    int value = (brightness * _backlight_max + MAX_BRIGHTNESS_SETTING / 2)
                / MAX_BRIGHTNESS_SETTING;

    if(value <= 0)
        value = 1;

    if(! sysfs_set_int(sysfs_paths[SYSFS_BACKLIGHT_BRIGHTNESS], value))
    {
        DEBUGF("ERROR %s: Can not enable backlight.", __func__);
        panicf("ERROR %s: Can not enable backlight.", __func__);
        return false;
    }

    _current_brightness = brightness;
#else
    /*
        /sys/devices/platform/rk29_backlight/backlight/rk28_bl/bl_power
        0: backlight on
    */
    if(! sysfs_set_int(sysfs_paths[SYSFS_BACKLIGHT_POWER], 0))
    {
        DEBUGF("ERROR %s: Can not enable backlight.", __func__);
        panicf("ERROR %s: Can not enable backlight.", __func__);
        return false;
    }
#endif

    _backlight_enabled = true;

    return true;
}


void backlight_hw_on(void)
{
    if(! _backlight_enabled)
    {
        backlight_hw_init();
        lcd_enable(true);
    }
}


void backlight_hw_off(void)
{
    TRACE;

#ifdef SHANLING_M3X
    if(! sysfs_set_int(sysfs_paths[SYSFS_BACKLIGHT_BRIGHTNESS], 0))
    {
        DEBUGF("ERROR %s: Can not disable backlight.", __func__);
        return;
    }
#else
    /*
        /sys/devices/platform/rk29_backlight/backlight/rk28_bl/bl_power
        1: backlight off
    */
    if(! sysfs_set_int(sysfs_paths[SYSFS_BACKLIGHT_POWER], 1))
    {
        DEBUGF("ERROR %s: Can not disable backlight.", __func__);
        return;
    }
#endif

    lcd_enable(false);

    _backlight_enabled = false;
}


/*
    Prevent excessive backlight_hw_brightness usage.
    Required for proper seeking.
*/


void backlight_hw_brightness(int brightness)
{
    if(brightness > MAX_BRIGHTNESS_SETTING)
    {
        DEBUGF("DEBUG %s: Adjusting brightness from %d to MAX.", __func__, brightness);
        brightness = MAX_BRIGHTNESS_SETTING;
    }
    if(brightness < MIN_BRIGHTNESS_SETTING)
    {
        DEBUGF("DEBUG %s: Adjusting brightness from %d to MIN.", __func__, brightness);
        brightness = MIN_BRIGHTNESS_SETTING;
    }

    if(_current_brightness == brightness)
    {
        return;
    }

    TRACE;

#ifdef SHANLING_M3X
    /* Save changes while dark without switching the backlight on. */
    if (!_backlight_enabled)
    {
        _current_brightness = brightness;
        return;
    }
    /*
        /sys/class/leds/lcd-backlight/brightness
        0 ... _backlight_max (255 on this device)
    */
    if(_backlight_max <= 0)
        _backlight_max = 255;

    int value = (brightness * _backlight_max + MAX_BRIGHTNESS_SETTING / 2)
                / MAX_BRIGHTNESS_SETTING;

    if(! sysfs_set_int(sysfs_paths[SYSFS_BACKLIGHT_BRIGHTNESS], value))
    {
        DEBUGF("ERROR %s: Can not set brightness.", __func__);
        return;
    }
    /* A failed write must remain retryable on the next call. */
    _current_brightness = brightness;
#else
    _current_brightness = brightness;
    /*
        /sys/devices/platform/rk29_backlight/backlight/rk28_bl/max_brightness
        0 ... 255
    */
    if(! sysfs_set_int(sysfs_paths[SYSFS_BACKLIGHT_BRIGHTNESS], _current_brightness))
    {
        DEBUGF("ERROR %s: Can not set brightness.", __func__);
        return;
    }
#endif
}
