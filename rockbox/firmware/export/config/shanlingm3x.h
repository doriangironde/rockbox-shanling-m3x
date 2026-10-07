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

/* This config file is for Rockbox as an application on Android without JVM. */

/* We don't run on hardware directly */
#ifndef SIMULATOR
#define CONFIG_PLATFORM PLATFORM_HOSTED
#define PIVOT_ROOT "/data/media/0"
#endif
#define HAVE_FPU

/* For Rolo and boot loader */
#define MODEL_NUMBER 126

#define MODEL_NAME "Shanling M3X"

#define HAVE_USB_POWER
#define HAVE_USB_ADB

/* define this if you have a colour LCD */
#define HAVE_LCD_COLOR

/* define this if you want album art for this target */
#define HAVE_ALBUMART

/* define this to enable bitmap scaling */
#define HAVE_BMP_SCALING

/* define this to enable JPEG decoding */
#define HAVE_JPEG

/* define this if you have access to the quickscreen */
#define HAVE_QUICKSCREEN

/* define this if you would like tagcache to build on this target */
#define HAVE_TAGCACHE

/* LCD dimensions */
/* The M3X panel is 768x1280. /dev/graphics/fb0 reports 32bpp with
 * red.offset == 0, i.e. RGBA8888 in memory (confirmed by SurfaceFlinger
 * reporting format=1 / PIXEL_FORMAT_RGBA_8888 for the 768x1280 buffer). */
#define LCD_WIDTH  768
#define LCD_HEIGHT 1280
#define LCD_DPI 160
#define LCD_DEPTH  32
#define LCD_PIXELFORMAT RGBA8888

#define HAVE_LCD_ENABLE
#define HAVE_LCD_SLEEP
#define HAVE_LCD_SLEEP_SETTING
/*#define HAVE_LCD_FLIP*/
#define HAVE_LCD_SHUTDOWN

/* define this to indicate your device's keypad */
#define HAVE_TOUCHSCREEN
#define HAVE_BUTTON_DATA

/* Define this if you have a software controlled poweroff */
#define HAVE_SW_POWEROFF

/* The number of bytes reserved for loadable codecs */
#define CODEC_SIZE 0x100000

/* The number of bytes reserved for loadable plugins */
#define PLUGIN_BUFFER_SIZE 0x200000

#define AB_REPEAT_ENABLE

/* Define this for LCD backlight available */
#define HAVE_BACKLIGHT
#define HAVE_BACKLIGHT_BRIGHTNESS

/* Main LCD backlight brightness range and defaults.
 * The M3X backlight is a LED-class device (/sys/class/leds/lcd-backlight) with
 * max_brightness = 255. Rockbox's 0..255 range is scaled onto that. */
#define MIN_BRIGHTNESS_SETTING      4
#define MAX_BRIGHTNESS_SETTING      255
#define DEFAULT_BRIGHTNESS_SETTING  200 /* matches Android's 200/255 default */

/* Which backlight fading type? */
#define CONFIG_BACKLIGHT_FADING BACKLIGHT_FADING_SW_SETTING

/*
    A DAP should not blank its own screen while idle, and this target's
    lcd_sleep() puts the whole system into /sys/power/state "mem".
    0 == "backlight always on".
*/
#define DEFAULT_BACKLIGHT_TIMEOUT 0

#define HAVE_SW_TONE_CONTROLS
#define HAVE_SW_VOLUME_CONTROL
#ifndef SIMULATOR
/* tinyalsa uses S32_LE; expand the mixer samples before sending them. */
#define PCM_NATIVE_BITDEPTH 32
/* Avoid a signed left shift when scaling negative samples to 32 bits. */
#define PCM_SW_VOLUME_FRACBITS 16
#endif
#define HW_SAMPR_CAPS SAMPR_CAP_ALL_192

/*#define HAVE_MULTIMEDIA_KEYS*/
/*
    The M3X's own key handling lives in target/hosted/ibasso/m3x/button-m3x.c.
    CONFIG_KEYPAD is still needed so the plugins (clix, reversi, ...) get a
    keymap; DX50_PAD reuses rockbox/apps/keymaps/keymap-dx50.c, whose button set
    matches the BUTTON_* values in
    firmware/target/hosted/ibasso/button-target.h.
*/
#define CONFIG_KEYPAD DX50_PAD

/* define this if the target has volume keys which can be used in the lists */
#define HAVE_VOLUME_IN_LIST

#define BATTERY_CAPACITY_DEFAULT 3300 /* Fuel gauge reports 3332 mAh design */
#define BATTERY_CAPACITY_MIN     1700 /* min. capacity selectable */
#define BATTERY_CAPACITY_MAX     7300 /* max. capacity selectable */
#define BATTERY_CAPACITY_INC       50 /* capacity increment */


#define CONFIG_BATTERY_MEASURE (VOLTAGE_MEASURE | PERCENTAGE_MEASURE)
#define CONFIG_CHARGING        CHARGING_MONITOR

/*
    The M3X ships with a 3332 mAh cell (as reported by
    /sys/class/power_supply/bms/charge_full_design). Values are guesses.
*/
#define CURRENT_NORMAL    330
#define CURRENT_BACKLIGHT  30 /* TBD */
#define CURRENT_RECORD      0 /* no recording */

/* define this if the hardware can be powered off while charging */
#define HAVE_POWEROFF_WHILE_CHARGING

#define CONFIG_LCD LCD_COWOND2

/* Define this if a programmable hotkey is mapped */
#define HAVE_HOTKEY

/* Root-hosted internal storage stays accessible when Android FUSE stops.
 * No microSD is currently detected; card mounting is still a bring-up gate. */
#define CONFIG_STORAGE (STORAGE_HOSTFS)
#define HOSTFS_VOL_DEC "Internal"
#define HAVE_STORAGE_FLUSH
#define NUM_DRIVES 1
