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


#include <stdio.h>
#include <string.h>

#include "config.h"
#include "debug.h"

#include "debug-ibasso.h"
#include "sysfs-ibasso.h"

const char* const sysfs_paths[] =
{
#ifdef SHANLING_M3X
    /*
        Shanling M3X. The codec is reached over ALSA mixer controls (see
        m3x/audiohw-m3x.c) instead of the iBasso sysfs nodes, so the codec
        related entries below point at nodes that exist on this device but are
        not used for volume/mute.

        Volume / mute / filter: "AK4497 Lch/Rch Digital Volume",
        "AK4497 mute control", "AK4497 filter control" (ALSA, card 0).
    */

    /* SYSFS_DX50_CODEC_VOLUME (unused on M3X) */
    "/dev/null",

    /* SYSFS_HOLDKEY - the M3X has no hold switch */
    "/dev/null",

    /* SYSFS_DX90_ES9018_VOLUME (unused on M3X) */
    "/dev/null",

    /* SYSFS_ES9018_FILTER (unused on M3X) */
    "/dev/null",

    /* SYSFS_MUTE (unused on M3X; see AK4497 mute control) */
    "/dev/null",

    /* SYSFS_WM8740_MUTE (unused on M3X) */
    "/dev/null",
#else
    /* SYSFS_DX50_CODEC_VOLUME */
    "/dev/codec_volume",

    /* SYSFS_HOLDKEY */
    "/sys/class/axppower/holdkey",

    /* SYSFS_DX90_ES9018_VOLUME */
    "/sys/class/codec/es9018_volume",

    /* SYSFS_ES9018_FILTER */
    "/sys/class/codec/es9018_filter",

    /* SYSFS_MUTE */
    "/sys/class/codec/mute",

    /* SYSFS_WM8740_MUTE */
    "/sys/class/codec/wm8740_mute",
#endif

    /* SYSFS_BATTERY_CAPACITY */
    "/sys/class/power_supply/battery/capacity",

    /* SYSFS_BATTERY_CURRENT_NOW */
    "/sys/class/power_supply/battery/current_now",

    /* SYSFS_BATTERY_ENERGY_FULL_DESIGN */
#ifdef SHANLING_M3X
    /* Not present; the fuel gauge exposes this under bms/. */
    "/sys/class/power_supply/bms/charge_full_design",
#else
    "/sys/class/power_supply/battery/energy_full_design",
#endif

    /* SYSFS_BATTERY_HEALTH */
    "/sys/class/power_supply/battery/health",

    /* SYSFS_BATTERY_MODEL_NAME */
#ifdef SHANLING_M3X
    "/sys/class/power_supply/bms/battery_type",
#else
    "/sys/class/power_supply/battery/model_name",
#endif

    /* SYSFS_BATTERY_ONLINE */
    "/sys/class/power_supply/battery/online",

    /* SYSFS_BATTERY_PRESENT */
    "/sys/class/power_supply/battery/present",

    /* SYSFS_BATTERY_STATUS */
    "/sys/class/power_supply/battery/status",

    /* SYSFS_BATTERY_TECHNOLOGY */
    "/sys/class/power_supply/battery/technology",

    /* SYSFS_BATTERY_TEMP */
    "/sys/class/power_supply/battery/temp",

    /* SYSFS_BATTERY_TYPE */
    "/sys/class/power_supply/battery/type",

    /* SYSFS_BATTERY_VOLTAGE_MAX_DESIGN */
    "/sys/class/power_supply/battery/voltage_max_design",

    /* SYSFS_BATTERY_VOLTAGE_MIN_DESIGN */
#ifdef SHANLING_M3X
    "/sys/class/power_supply/bms/voltage_min",
#else
    "/sys/class/power_supply/battery/voltage_min_design",
#endif

    /* SYSFS_BATTERY_VOLTAGE_NOW */
    "/sys/class/power_supply/battery/voltage_now",

    /* SYSFS_USB_POWER_CURRENT_NOW */
    "/sys/class/power_supply/usb/current_now",

    /* SYSFS_USB_POWER_ONLINE */
    "/sys/class/power_supply/usb/online",

    /* SYSFS_USB_POWER_PRESENT */
    "/sys/class/power_supply/usb/present",

    /* SYSFS_USB_POWER_VOLTAGE_NOW */
    "/sys/class/power_supply/usb/voltage_now",

#ifdef SHANLING_M3X
    /*
        The M3X drives its panel backlight through the LED class, not a
        Rockchip backlight platform device: /sys/class/leds/lcd-backlight/
        exposes brightness (0..255) and max_brightness.
        There is no bl_power node; brightness 0 is "off".
    */

    /* SYSFS_BACKLIGHT_POWER (unused on M3X; brightness is used instead) */
    "/dev/null",

    /* SYSFS_BACKLIGHT_BRIGHTNESS */
    "/sys/class/leds/lcd-backlight/brightness",

    /* SYSFS_BACKLIGHT_MAX_BRIGHTNESS */
    "/sys/class/leds/lcd-backlight/max_brightness",
#else
    /* SYSFS_BACKLIGHT_POWER */
    "/sys/devices/platform/rk29_backlight/backlight/rk28_bl/bl_power",

    /* SYSFS_BACKLIGHT_BRIGHTNESS */
    "/sys/devices/platform/rk29_backlight/backlight/rk28_bl/brightness",

    /* SYSFS_BACKLIGHT_MAX_BRIGHTNESS (not needed off the M3X) */
    "/dev/null",
#endif

#ifdef SHANLING_M3X
    /*
        /sys/class/graphics/fb0/blank - 0 unblanks, 1 blanks.

        Needed because the MDP driver puts the panel into
        msm_fb_panel_status=suspend when Android's SurfaceFlinger exits, and it
        does not come back on its own.
    */

    /* SYSFS_FB_BLANK */
    "/sys/class/graphics/fb0/blank",
#else
    /* SYSFS_FB_BLANK (not needed off the M3X) */
    "/dev/null",
#endif

    /* SYSFS_POWER_STATE */
    "/sys/power/state",

    /* SYSFS_POWER_WAKE_LOCK */
    "/sys/power/wake_lock"
};
