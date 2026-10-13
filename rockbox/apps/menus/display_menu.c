/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2007 Jonathan Gordon
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
#include <stddef.h>
#include <limits.h>
#include "config.h"
#include "appevents.h"
#include "lang.h"
#include "action.h"
#include "settings.h"
#include "menu.h"
#include "tree.h"
#include "list.h"
#include "peakmeter.h"
#include "talk.h"
#include "lcd.h"
#ifdef HAVE_REMOTE_LCD
#include "lcd-remote.h"
#endif
#ifdef HAVE_BACKLIGHT
#include "mask_select.h"
#include "splash.h"
#endif
#ifdef HAVE_TOUCHSCREEN
#include "screens.h"
#endif
#include "viewport.h"
#include "statusbar.h" /* statusbar_vals enum*/
#include "rbunicode.h"

#ifdef SHANLING_M3X
#include <stdio.h>
#include "backlight.h"
#include "exported_menus.h"
#include "kernel.h"
#include "misc.h"
#include "scrollbar.h"
#include "system.h"
#include "touchscreen.h"

enum
{
    BRIGHTNESS_MARGIN = LCD_WIDTH / 16,
    BRIGHTNESS_SLIDER_X = LCD_WIDTH / 12,
    BRIGHTNESS_SLIDER_W = LCD_WIDTH * 5 / 6,
    BRIGHTNESS_SLIDER_Y = LCD_HEIGHT / 3,
    BRIGHTNESS_TOUCH_H = LCD_HEIGHT / 12,
    BRIGHTNESS_BUTTON_W = LCD_WIDTH * 3 / 8,
    BRIGHTNESS_STEP_Y = LCD_HEIGHT / 2,
    BRIGHTNESS_APPLY_Y = LCD_HEIGHT * 17 / 20,
    BRIGHTNESS_STEP = (MAX_BRIGHTNESS_SETTING + 10) / 20,
};

struct m3x_brightness_state
{
    struct viewport vp;
    struct viewport *old_vp;
    enum touchscreen_mode old_mode;
    int original;
    bool closed;
};

static void m3x_brightness_cleanup(void *data)
{
    struct m3x_brightness_state *state = data;
    if (state->closed)
        return;
    state->closed = true;
    backlight_set_brightness(state->original);
    action_gesture_reset();
    touchscreen_set_mode(state->old_mode);
    lcd_set_viewport(state->old_vp);
    viewportmanager_theme_undo(SCREEN_MAIN, false);
    lcd_set_viewport(state->old_vp);
}

static void m3x_brightness_label(const char *text, int x, int y, int w, int h)
{
    int text_w, text_h;
    lcd_getstringsize(text, &text_w, &text_h);
    lcd_putsxy(x + (w - text_w) / 2, y + (h - text_h) / 2, text);
}

static void m3x_brightness_draw(int value)
{
    char percent[12];
    const int right = LCD_WIDTH - BRIGHTNESS_MARGIN - BRIGHTNESS_BUTTON_W;
    lcd_set_drawmode(DRMODE_SOLID);
    lcd_clear_viewport();
    m3x_brightness_label(str(LANG_BRIGHTNESS), 0, BRIGHTNESS_MARGIN,
                         LCD_WIDTH, BRIGHTNESS_TOUCH_H);
    snprintf(percent, sizeof(percent), "%d%%",
             (value * 100 + MAX_BRIGHTNESS_SETTING / 2) / MAX_BRIGHTNESS_SETTING);
    m3x_brightness_label(percent, 0, LCD_HEIGHT / 5,
                         LCD_WIDTH, BRIGHTNESS_TOUCH_H);

    lcd_set_foreground(LCD_RGBPACK(43, 106, 177));
    gui_scrollbar_draw(&screens[SCREEN_MAIN], BRIGHTNESS_SLIDER_X,
                       BRIGHTNESS_SLIDER_Y - 12, BRIGHTNESS_SLIDER_W, 24,
                       MAX_BRIGHTNESS_SETTING - MIN_BRIGHTNESS_SETTING,
                       0, value - MIN_BRIGHTNESS_SETTING, HORIZONTAL);
    int thumb_x = BRIGHTNESS_SLIDER_X +
        (value - MIN_BRIGHTNESS_SETTING) * BRIGHTNESS_SLIDER_W /
        (MAX_BRIGHTNESS_SETTING - MIN_BRIGHTNESS_SETTING);
    lcd_fillrect(thumb_x - 24, BRIGHTNESS_SLIDER_Y - 32, 48, 64);

    lcd_drawrect(BRIGHTNESS_MARGIN, BRIGHTNESS_STEP_Y,
                 BRIGHTNESS_BUTTON_W, BRIGHTNESS_TOUCH_H);
    lcd_drawrect(right, BRIGHTNESS_STEP_Y,
                 BRIGHTNESS_BUTTON_W, BRIGHTNESS_TOUCH_H);
    lcd_drawrect(BRIGHTNESS_MARGIN, BRIGHTNESS_APPLY_Y,
                 BRIGHTNESS_BUTTON_W, BRIGHTNESS_TOUCH_H);
    lcd_fillrect(right, BRIGHTNESS_APPLY_Y,
                 BRIGHTNESS_BUTTON_W, BRIGHTNESS_TOUCH_H);
    lcd_set_foreground(LCD_RGBPACK(32, 32, 32));
    m3x_brightness_label("-", BRIGHTNESS_MARGIN, BRIGHTNESS_STEP_Y,
                         BRIGHTNESS_BUTTON_W, BRIGHTNESS_TOUCH_H);
    m3x_brightness_label("+", right, BRIGHTNESS_STEP_Y,
                         BRIGHTNESS_BUTTON_W, BRIGHTNESS_TOUCH_H);
    m3x_brightness_label("Cancel", BRIGHTNESS_MARGIN, BRIGHTNESS_APPLY_Y,
                         BRIGHTNESS_BUTTON_W, BRIGHTNESS_TOUCH_H);
    lcd_set_foreground(LCD_RGBPACK(255, 255, 255));
    lcd_set_drawmode(DRMODE_FG);
    m3x_brightness_label("Apply", right, BRIGHTNESS_APPLY_Y,
                         BRIGHTNESS_BUTTON_W, BRIGHTNESS_TOUCH_H);
    lcd_set_drawmode(DRMODE_SOLID);
    lcd_set_foreground(LCD_RGBPACK(32, 32, 32));
    lcd_update_viewport();
}

static int m3x_brightness_screen(void)
{
    struct m3x_brightness_state state = {
        .old_mode = touchscreen_get_mode(),
        .original = global_settings.brightness,
        .closed = false,
    };
    int value = state.original;
    int result = 0;
    bool redraw = true;
    state.old_vp = lcd_set_viewport(NULL);
    viewportmanager_theme_enable(SCREEN_MAIN, false, &state.vp);
    state.vp.fg_pattern = LCD_RGBPACK(32, 32, 32);
    state.vp.bg_pattern = LCD_RGBPACK(238, 238, 238);
    state.vp.flags &= ~VP_FLAG_ALIGNMENT_MASK;
    lcd_set_viewport(&state.vp);
    touchscreen_set_mode(TOUCHSCREEN_POINT);
    action_wait_for_release();
    action_gesture_reset();
    if (global_settings.talk_menu)
    {
        talk_id(LANG_BRIGHTNESS, false);
        talk_value((value * 100 + MAX_BRIGHTNESS_SETTING / 2) /
                   MAX_BRIGHTNESS_SETTING, UNIT_PERCENT, true);
    }

    while (!state.closed)
    {
        if (redraw)
        {
            m3x_brightness_draw(value);
            redraw = false;
        }
        int action = get_action(CONTEXT_SETTINGS, TIMEOUT_BLOCK);
        int next = value;
        if (action == ACTION_TOUCHSCREEN)
        {
            struct gesture_event ev;
            if (!action_gesture_get_event_in_vp(&ev, &state.vp))
                continue;
            /* Capture by the press origin, including releases over buttons. */
            if (ev.ox >= BRIGHTNESS_SLIDER_X &&
                ev.ox <= BRIGHTNESS_SLIDER_X + BRIGHTNESS_SLIDER_W &&
                ev.oy >= BRIGHTNESS_SLIDER_Y - BRIGHTNESS_TOUCH_H / 2 &&
                ev.oy < BRIGHTNESS_SLIDER_Y + BRIGHTNESS_TOUCH_H / 2)
            {
                int x = MAX(0, MIN(BRIGHTNESS_SLIDER_W,
                                   ev.x - BRIGHTNESS_SLIDER_X));
                next = MIN_BRIGHTNESS_SETTING +
                    (x * (MAX_BRIGHTNESS_SETTING - MIN_BRIGHTNESS_SETTING) +
                     BRIGHTNESS_SLIDER_W / 2) / BRIGHTNESS_SLIDER_W;
            }
            else if (ev.id == GESTURE_TAP)
            {
                int right = LCD_WIDTH - BRIGHTNESS_MARGIN - BRIGHTNESS_BUTTON_W;
                bool left_button = ev.x >= BRIGHTNESS_MARGIN &&
                    ev.x < BRIGHTNESS_MARGIN + BRIGHTNESS_BUTTON_W;
                bool right_button = ev.x >= right &&
                    ev.x < right + BRIGHTNESS_BUTTON_W;
                if (ev.y >= BRIGHTNESS_STEP_Y &&
                    ev.y < BRIGHTNESS_STEP_Y + BRIGHTNESS_TOUCH_H)
                {
                    if (left_button) action = ACTION_SETTINGS_DEC;
                    if (right_button) action = ACTION_SETTINGS_INC;
                }
                else if (ev.y >= BRIGHTNESS_APPLY_Y &&
                         ev.y < BRIGHTNESS_APPLY_Y + BRIGHTNESS_TOUCH_H)
                {
                    if (left_button) action = ACTION_STD_CANCEL;
                    if (right_button) action = ACTION_STD_OK;
                }
            }
        }
        switch (action)
        {
            case ACTION_SETTINGS_INC:
            case ACTION_SETTINGS_INCREPEAT:
                next = MIN(MAX_BRIGHTNESS_SETTING, value + BRIGHTNESS_STEP);
                break;
            case ACTION_SETTINGS_DEC:
            case ACTION_SETTINGS_DECREPEAT:
                next = MAX(MIN_BRIGHTNESS_SETTING, value - BRIGHTNESS_STEP);
                break;
            case ACTION_STD_OK:
                global_settings.brightness = value;
                state.original = value;
                settings_save();
                goto exit;
            case ACTION_STD_CANCEL:
                goto exit;
            case ACTION_REDRAW:
                redraw = true;
                break;
            default:
                if (default_event_handler_ex(action, m3x_brightness_cleanup,
                                             &state) == SYS_USB_CONNECTED)
                    result = 1;
                break;
        }
        if (next != value)
        {
            value = next;
            backlight_set_brightness(value);
            redraw = true;
            if (global_settings.talk_menu &&
                (action != ACTION_TOUCHSCREEN || !action_gesture_is_pressed()))
                talk_value((value * 100 + MAX_BRIGHTNESS_SETTING / 2) /
                           MAX_BRIGHTNESS_SETTING, UNIT_PERCENT, false);
        }
    }
exit:
    m3x_brightness_cleanup(&state);
    return result;
}
#endif

#ifdef HAVE_BACKLIGHT
static int selectivebacklight_callback(int action,
                                       const struct menu_item_ex *this_item,
                                       struct gui_synclist *this_list)
{
    (void)this_item;
    (void)this_list;
    switch (action)
    {
        case ACTION_EXIT_MENUITEM:
        case ACTION_STD_MENU:
        case ACTION_STD_CANCEL:
            set_selective_backlight_actions(
                                       global_settings.bl_selective_actions,
                                       global_settings.bl_selective_actions_mask,
                                       global_settings.bl_filter_first_keypress);
            break;
    }

    return action;
}

static int filterfirstkeypress_callback(int action,
                                        const struct menu_item_ex *this_item,
                                        struct gui_synclist *this_list)
{
    /*(void)this_item;REMOVED*/
    switch (action)
    {
        case ACTION_EXIT_MENUITEM:
            set_backlight_filter_keypress(global_settings.bl_filter_first_keypress);
#ifdef HAVE_REMOTE_LCD
            set_remote_backlight_filter_keypress(
                                global_settings.remote_bl_filter_first_keypress);
#endif /* HAVE_REMOTE_LCD */
            selectivebacklight_callback(action,this_item, this_list);/*uses Filter First KP*/
            break;
    }

    return action;
}

static int selectivebacklight_set_mask(void* param)
{
    (void)param;
     int mask = global_settings.bl_selective_actions_mask;
            struct s_mask_items maskitems[]={
                                       {ID2P(LANG_ACTION_VOLUME)   , SEL_ACTION_VOL},
                                       {ID2P(LANG_ACTION_PLAY), SEL_ACTION_PLAY},
                                       {ID2P(LANG_ACTION_SEEK), SEL_ACTION_SEEK},
                                       {ID2P(LANG_ACTION_SKIP), SEL_ACTION_SKIP},
                     {ID2P(LANG_ACTION_DISABLE_UNMAPPED), SEL_ACTION_NOUNMAPPED}
#if CONFIG_CHARGING
                        ,{ID2P(LANG_ACTION_DISABLE_EXT_POWER), SEL_ACTION_NOEXT}
#endif
                                            };

            mask = mask_select(mask, ID2P(LANG_BACKLIGHT_SELECTIVE)
                                   , maskitems, ARRAYLEN(maskitems));

            if (mask == SEL_ACTION_NONE ||  mask == SEL_ACTION_NOEXT)
                global_settings.bl_selective_actions = false;
            else if (global_settings.bl_selective_actions_mask != mask)
                global_settings.bl_selective_actions = true;

            global_settings.bl_selective_actions_mask = mask;

    return true;
}

#endif /* HAVE_BACKLIGHT */
#ifdef HAVE_LCD_FLIP
static int flipdisplay_callback(int action,
                                const struct menu_item_ex *this_item,
                                struct gui_synclist *this_list)
{
    (void)this_item;
    (void)this_list;
    switch (action)
    {
        case ACTION_EXIT_MENUITEM:
            button_set_flip(global_settings.flip_display);
            lcd_set_flip(global_settings.flip_display);
            lcd_update();
#ifdef HAVE_REMOTE_LCD
            lcd_remote_set_flip(global_settings.remote_flip_display);
            lcd_remote_update();
#endif
            break;
    }
    return action;
}
#endif

/***********************************/
/*    LCD MENU                     */
#ifdef HAVE_BACKLIGHT
MENUITEM_SETTING(backlight_timeout, &global_settings.backlight_timeout, NULL);
#if CONFIG_CHARGING
MENUITEM_SETTING(backlight_timeout_plugged,
                &global_settings.backlight_timeout_plugged, NULL);
#endif

MENUITEM_SETTING(backlight_on_button_hold,
                &global_settings.backlight_on_button_hold, NULL);

MENUITEM_SETTING(caption_backlight, &global_settings.caption_backlight, NULL);
#if    defined(HAVE_BACKLIGHT_FADING_INT_SETTING) \
    || defined(HAVE_BACKLIGHT_FADING_BOOL_SETTING)
MENUITEM_SETTING(backlight_fade_in, &global_settings.backlight_fade_in, NULL);
MENUITEM_SETTING(backlight_fade_out, &global_settings.backlight_fade_out, NULL);
#endif
MENUITEM_SETTING(bl_filter_first_keypress,
                    &global_settings.bl_filter_first_keypress,
                    filterfirstkeypress_callback);

MENUITEM_SETTING(bl_selective_actions,
                 &global_settings.bl_selective_actions,
                                                    selectivebacklight_callback);

MENUITEM_FUNCTION(sel_backlight_mask, 0, ID2P(LANG_SETTINGS),
                  selectivebacklight_set_mask, selectivebacklight_callback,
                  Icon_Menu_setting);

MAKE_MENU(sel_backlight, ID2P(LANG_BACKLIGHT_SELECTIVE),
          NULL, Icon_Menu_setting, &bl_selective_actions, &sel_backlight_mask);

#ifdef HAVE_LCD_SLEEP_SETTING
MENUITEM_SETTING(lcd_sleep_after_backlight_off,
                &global_settings.lcd_sleep_after_backlight_off, NULL);
#endif
#ifdef HAVE_BACKLIGHT_BRIGHTNESS
#ifdef SHANLING_M3X
MENUITEM_FUNCTION(brightness_item, 0, ID2P(LANG_BRIGHTNESS),
                  m3x_brightness_screen, NULL, Icon_Menu_setting);
#else
MENUITEM_SETTING(brightness_item, &global_settings.brightness, NULL);
#endif
#endif
#ifdef HAVE_COMPOSITE_VIDEO_OUT
MENUITEM_SETTING(composite_video_output,
                 &global_settings.composite_video_output, NULL);
#endif
#endif /* HAVE_BACKLIGHT */
#ifdef HAVE_LCD_CONTRAST
MENUITEM_SETTING(contrast, &global_settings.contrast, NULL);
#endif
#ifdef HAVE_LCD_INVERT
MENUITEM_SETTING(invert, &global_settings.invert, NULL);
#endif
#ifdef HAVE_LCD_FLIP
MENUITEM_SETTING(flip_display, &global_settings.flip_display, flipdisplay_callback);
#endif

/* now the actual menu */
MAKE_MENU(lcd_settings,ID2P(LANG_LCD_MENU),
            NULL, Icon_Display_menu
#ifdef HAVE_BACKLIGHT
            ,&backlight_timeout
# if CONFIG_CHARGING
            ,&backlight_timeout_plugged
# endif
            ,&backlight_on_button_hold
            ,&caption_backlight
#if    defined(HAVE_BACKLIGHT_FADING_INT_SETTING) \
    || defined(HAVE_BACKLIGHT_FADING_BOOL_SETTING)
            ,&backlight_fade_in, &backlight_fade_out
#endif
            ,&bl_filter_first_keypress
            ,&sel_backlight
# ifdef HAVE_LCD_SLEEP_SETTING
            ,&lcd_sleep_after_backlight_off
# endif
# ifdef HAVE_BACKLIGHT_BRIGHTNESS
            ,&brightness_item
# endif
#endif /* HAVE_BACKLIGHT */
#ifdef HAVE_COMPOSITE_VIDEO_OUT
            ,&composite_video_output
#endif
#ifdef HAVE_LCD_CONTRAST
            ,&contrast
#endif
# ifdef HAVE_LCD_INVERT
            ,&invert
# endif
# ifdef HAVE_LCD_FLIP
            ,&flip_display
# endif
         );
/*    LCD MENU                    */
/***********************************/


/********************************/
/* Remote LCD settings menu     */
#ifdef HAVE_REMOTE_LCD
MENUITEM_SETTING(remote_backlight_timeout,
                    &global_settings.remote_backlight_timeout, NULL);

#if CONFIG_CHARGING
MENUITEM_SETTING(remote_backlight_timeout_plugged,
                    &global_settings.remote_backlight_timeout_plugged, NULL);
#endif

#ifdef HAS_REMOTE_BUTTON_HOLD
MENUITEM_SETTING(remote_backlight_on_button_hold,
                    &global_settings.remote_backlight_on_button_hold, NULL);
#endif

MENUITEM_SETTING(remote_caption_backlight,
                    &global_settings.remote_caption_backlight, NULL);
MENUITEM_SETTING(remote_bl_filter_first_keypress,
                    &global_settings.remote_bl_filter_first_keypress,
                    filterfirstkeypress_callback);
MENUITEM_SETTING(remote_contrast,
                    &global_settings.remote_contrast, NULL);
MENUITEM_SETTING(remote_invert,
                    &global_settings.remote_invert, NULL);

#ifdef HAVE_LCD_FLIP
MENUITEM_SETTING(remote_flip_display,
                    &global_settings.remote_flip_display, flipdisplay_callback);
#endif

#ifdef HAVE_REMOTE_LCD_TICKING
static int ticking_callback(int action,
                            const struct menu_item_ex *this_item,
                            struct gui_synclist *this_list)
{
    (void)this_item;
    (void)this_list;
    switch (action)
    {
        case ACTION_EXIT_MENUITEM:
            lcd_remote_emireduce(global_settings.remote_reduce_ticking);
            break;
    }
    return action;
}
MENUITEM_SETTING(remote_reduce_ticking,
                    &global_settings.remote_reduce_ticking, ticking_callback);
#endif

MAKE_MENU(lcd_remote_settings, ID2P(LANG_LCD_REMOTE_MENU),
            NULL, Icon_Remote_Display_menu,
            &remote_backlight_timeout,
#if CONFIG_CHARGING
            &remote_backlight_timeout_plugged,
#endif
#ifdef HAS_REMOTE_BUTTON_HOLD
            &remote_backlight_on_button_hold,
#endif
            &remote_caption_backlight, &remote_bl_filter_first_keypress,
            &remote_contrast, &remote_invert

#ifdef HAVE_LCD_FLIP
            ,&remote_flip_display
#endif
#ifdef HAVE_REMOTE_LCD_TICKING
            ,&remote_reduce_ticking
#endif
         );

#endif /* HAVE_REMOTE_LCD */
/* Remote LCD settings menu     */
/********************************/

/***********************************/
/*    SCROLL MENU                  */
MENUITEM_SETTING_W_TEXT(scroll_speed, &global_settings.scroll_speed,
                         ID2P(LANG_SCROLL), NULL);
MENUITEM_SETTING(scroll_delay, &global_settings.scroll_delay, NULL);
MENUITEM_SETTING_W_TEXT(scroll_step, &global_settings.scroll_step,
                        ID2P(LANG_SCROLL_STEP_EXAMPLE), NULL);
MENUITEM_SETTING(bidir_limit, &global_settings.bidir_limit, NULL);
#ifdef HAVE_REMOTE_LCD
MENUITEM_SETTING_W_TEXT(remote_scroll_speed, &global_settings.remote_scroll_speed,
                         ID2P(LANG_SCROLL), NULL);
MENUITEM_SETTING(remote_scroll_delay, &global_settings.remote_scroll_delay, NULL);
MENUITEM_SETTING_W_TEXT(remote_scroll_step, &global_settings.remote_scroll_step,
                        ID2P(LANG_SCROLL_STEP_EXAMPLE), NULL);
MENUITEM_SETTING(remote_bidir_limit, &global_settings.remote_bidir_limit, NULL);

MAKE_MENU(remote_scroll_sets, ID2P(LANG_REMOTE_SCROLL_SETS), 0, Icon_NOICON,
          &remote_scroll_speed, &remote_scroll_delay,
          &remote_scroll_step, &remote_bidir_limit);
#endif /* HAVE_REMOTE_LCD */

/* list acceleration */
#ifndef HAVE_WHEEL_ACCELERATION
MENUITEM_SETTING(list_accel_start_delay,
                 &global_settings.list_accel_start_delay, NULL);
MENUITEM_SETTING(list_accel_wait, &global_settings.list_accel_wait, NULL);
#endif /* HAVE_WHEEL_ACCELERATION */
MENUITEM_SETTING(offset_out_of_view, &global_settings.offset_out_of_view, NULL);
MENUITEM_SETTING(disable_mainmenu_scrolling, &global_settings.disable_mainmenu_scrolling, NULL);
MENUITEM_SETTING(screen_scroll_step, &global_settings.screen_scroll_step, NULL);
MENUITEM_SETTING(scroll_paginated, &global_settings.scroll_paginated, NULL);
MENUITEM_SETTING(list_wraparound, &global_settings.list_wraparound, NULL);
MENUITEM_SETTING(list_order, &global_settings.list_order, NULL);

MAKE_MENU(scroll_settings_menu, ID2P(LANG_SCROLL_MENU), 0, Icon_NOICON,
          &scroll_speed, &scroll_delay,
          &scroll_step,
          &bidir_limit,
#ifdef HAVE_REMOTE_LCD
          &remote_scroll_sets,
#endif
          &offset_out_of_view,
          &disable_mainmenu_scrolling,
          &screen_scroll_step,
          &scroll_paginated,
          &list_wraparound,
          &list_order,
#ifndef HAVE_WHEEL_ACCELERATION
          &list_accel_start_delay, &list_accel_wait
#endif
          );
/*    SCROLL MENU                  */
/***********************************/

/***********************************/
/*    PEAK METER MENU              */

static int peakmeter_callback(int action,
                              const struct menu_item_ex *this_item,
                              struct gui_synclist *this_list)
{
    (void)this_item;
    (void)this_list;
    switch (action)
    {
        case ACTION_EXIT_MENUITEM:
            peak_meter_init_times(global_settings.peak_meter_release,
                                    global_settings.peak_meter_hold,
                                    global_settings.peak_meter_clip_hold);
            break;
    }
    return action;
}
MENUITEM_SETTING(peak_meter_hold,
                 &global_settings.peak_meter_hold, peakmeter_callback);
MENUITEM_SETTING(peak_meter_clip_hold,
                 &global_settings.peak_meter_clip_hold, peakmeter_callback);
#ifdef HAVE_RECORDING
MENUITEM_SETTING(peak_meter_clipcounter,
                 &global_settings.peak_meter_clipcounter, NULL);
#endif
MENUITEM_SETTING(peak_meter_release,
                 &global_settings.peak_meter_release, peakmeter_callback);
/**
 * Menu to select wether the scale of the meter
 * displays dBfs of linear values.
 */
static int peak_meter_scale(void) {
    bool retval = false;
    bool use_dbfs = global_settings.peak_meter_dbfs;
    retval = set_bool_options(str(LANG_PM_SCALE),
        &use_dbfs,
        STR(LANG_PM_DBFS), STR(LANG_PM_LINEAR),
        NULL);

    /* has the user really changed the scale? */
    if (use_dbfs != global_settings.peak_meter_dbfs) {

        /* store the change */
        global_settings.peak_meter_dbfs = use_dbfs;
        peak_meter_set_use_dbfs(use_dbfs);

        /* If the user changed the scale mode the meaning of
           peak_meter_min (peak_meter_max) has changed. Thus we have
           to convert the values stored in global_settings. */
        if (use_dbfs) {

            /* we only store -dBfs */
            global_settings.peak_meter_min = -peak_meter_get_min() / 100;
            global_settings.peak_meter_max = -peak_meter_get_max() / 100;

            /* limit the returned value to the allowed range */
            if(global_settings.peak_meter_min > 89)
                global_settings.peak_meter_min = 89;
        } else {
            int max;

            /* linear percent */
            global_settings.peak_meter_min = peak_meter_get_min();

            /* converting dBfs -> percent results in a precision loss.
               I assume that the user doesn't bother that conversion
               dBfs <-> percent isn't symmetrical for odd values but that
               he wants 0 dBfs == 100%. Thus I 'correct' the percent value
               resulting from dBfs -> percent manually here */
            max = peak_meter_get_max();
            global_settings.peak_meter_max = max < 99 ? max : 100;
        }
        settings_apply_pm_range();
    }
    return retval;
}

/**
 * Adjust the min value of the value range that
 * the peak meter shall visualize.
 */
static int peak_meter_min(void) {
    bool retval = false;
    if (global_settings.peak_meter_dbfs) {

        /* for dBfs scale */
        int range_max = -global_settings.peak_meter_max;
        int min = -global_settings.peak_meter_min;

        retval =  set_int(str(LANG_PM_MIN), str(LANG_PM_DBFS), UNIT_DB,
            &min, NULL, 1, -89, range_max, NULL);

        global_settings.peak_meter_min = - min;
    }

    /* for linear scale */
    else {
        int min = global_settings.peak_meter_min;

        retval =  set_int(str(LANG_PM_MIN), "%", UNIT_PERCENT,
            &min, NULL,
            1, 0, global_settings.peak_meter_max - 1, NULL);

        global_settings.peak_meter_min = (unsigned char)min;
    }

    settings_apply_pm_range();
    return retval;
}


/**
 * Adjust the max value of the value range that
 * the peak meter shall visualize.
 */
static int peak_meter_max(void) {
    bool retval = false;
    if (global_settings.peak_meter_dbfs) {

        /* for dBfs scale */
        int range_min = -global_settings.peak_meter_min;
        int max = -global_settings.peak_meter_max;

        retval =  set_int(str(LANG_PM_MAX), str(LANG_PM_DBFS), UNIT_DB,
            &max, NULL, 1, range_min, 0, NULL);

        global_settings.peak_meter_max = - max;

    }

    /* for linear scale */
    else {
        int max = global_settings.peak_meter_max;

        retval =  set_int(str(LANG_PM_MAX), "%", UNIT_PERCENT,
            &max, NULL,
            1, global_settings.peak_meter_min + 1, 100, NULL);

        global_settings.peak_meter_max = (unsigned char)max;
    }

    settings_apply_pm_range();
    return retval;
}

#if defined(HAVE_HISTOGRAM)
static bool history_interval(void)
{
    static const struct opt_items names[] = {
        { "0s", TALK_ID(0, UNIT_SEC) },
        { "1s", TALK_ID(1, UNIT_SEC) },
        { "2s", TALK_ID(2, UNIT_SEC) },
        { "4s", TALK_ID(4, UNIT_SEC) }
    };

    /* reconfigure histogram settings here */

    return set_option(str(LANG_HISTOGRAM_INTERVAL),
                          &global_settings.histogram_interval,
                          RB_INT, names, 4, NULL );
}

MENUITEM_FUNCTION(histogram, 0, ID2P(LANG_HISTOGRAM_INTERVAL),
                  history_interval, NULL, Icon_Menu_setting);

#endif

MENUITEM_FUNCTION(peak_meter_scale_item, 0, ID2P(LANG_PM_SCALE),
                  peak_meter_scale, NULL, Icon_NOICON);
MENUITEM_FUNCTION(peak_meter_min_item, 0, ID2P(LANG_PM_MIN),
                  peak_meter_min, NULL, Icon_NOICON);
MENUITEM_FUNCTION(peak_meter_max_item, 0, ID2P(LANG_PM_MAX),
                  peak_meter_max, NULL, Icon_NOICON);
MAKE_MENU(peak_meter_menu, ID2P(LANG_PM_MENU), NULL, Icon_NOICON,
          &peak_meter_release, &peak_meter_hold,
          &peak_meter_clip_hold,
#ifdef HAVE_RECORDING
          &peak_meter_clipcounter,
#endif
#ifdef HAVE_HISTOGRAM
          &histogram,
#endif
          &peak_meter_scale_item, &peak_meter_min_item, &peak_meter_max_item);
/*    PEAK METER MENU              */
/***********************************/


#ifdef HAVE_TOUCHSCREEN
static int touch_mode_callback(int action,
                               const struct menu_item_ex *this_item,
                               struct gui_synclist *this_list)
{
    (void)this_item;
    (void)this_list;
    switch (action)
    {
        case ACTION_EXIT_MENUITEM: /* on exit */
            touchscreen_set_mode(global_settings.touch_mode);
            break;
    }
    return action;
}

static int line_padding_callback(int action,
                                 const struct menu_item_ex *this_item,
                                 struct gui_synclist *this_list)
{
    (void)this_item;
    (void)this_list;
    if (action == ACTION_EXIT_MENUITEM)
        viewportmanager_theme_changed(THEME_LISTS);
    return action;
}

MENUITEM_SETTING(touch_mode, &global_settings.touch_mode, touch_mode_callback);
MENUITEM_SETTING(touch_enable_flick_shortcuts,
                 &global_settings.touch_enable_flick_shortcuts, NULL);

MENUITEM_FUNCTION(touchscreen_menu_calibrate, 0,
	              ID2P(LANG_TOUCHSCREEN_CALIBRATE), calibrate, NULL, Icon_NOICON);
MENUITEM_FUNCTION(touchscreen_menu_reset_calibration, 0,
	              ID2P(LANG_TOUCHSCREEN_RESET_CALIBRATION),
	              reset_mapping, NULL, Icon_NOICON);
MENUITEM_SETTING(list_line_padding, &global_settings.list_line_padding, line_padding_callback);

MAKE_MENU(touchscreen_menu, ID2P(LANG_TOUCHSCREEN_SETTINGS), NULL, Icon_NOICON,
          &touch_mode,
          &touch_enable_flick_shortcuts,
          &list_line_padding,
          &touchscreen_menu_calibrate,
          &touchscreen_menu_reset_calibration);
#endif

static int codepage_callback(int action,
                             const struct menu_item_ex *this_item,
                             struct gui_synclist *this_list)
{
    (void)this_item;
    (void)this_list;
    static int old_codepage;
    int new_codepage = global_settings.default_codepage;
    switch (action)
    {
        case ACTION_ENTER_MENUITEM:
            old_codepage = new_codepage;
            break;
        case ACTION_EXIT_MENUITEM:
            if (new_codepage != old_codepage)
                set_codepage(new_codepage);
            break;
    }
    return action;
}

MENUITEM_SETTING(codepage_setting, &global_settings.default_codepage, codepage_callback);


MAKE_MENU(display_menu, ID2P(LANG_DISPLAY),
            NULL, Icon_Display_menu,
            &lcd_settings,
#ifdef HAVE_REMOTE_LCD
            &lcd_remote_settings,
#endif
            &scroll_settings_menu,
            &peak_meter_menu,
            &codepage_setting,
#ifdef HAVE_TOUCHSCREEN
            &touchscreen_menu,
#endif
            );
