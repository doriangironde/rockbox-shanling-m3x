/* Exercise the real kinetic controller without a display or timer thread. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "../../../rockbox/apps/gui/bitmap/list.c"

struct user_settings global_settings;
volatile long current_tick;

int list_get_nb_lines(struct gui_synclist *list, enum screen_type screen)
{
    (void)list; (void)screen;
    return 10;
}
void button_queue_post(long id, intptr_t data) { (void)id; (void)data; }
void timeout_cancel(struct timeout *tmo) { (void)tmo; }
void panicf(const char *fmt, ...) { (void)fmt; abort(); }
void timeout_register(struct timeout *tmo, timeout_cb_type callback,
                      int ticks, intptr_t data)
{
    tmo->callback = callback;
    tmo->data = data;
    tmo->expires = current_tick + ticks;
}

static struct gui_synclist scrollable_list(void)
{
    struct gui_synclist list = {0};
    list.nb_items = 200;
    list.selected_size = 1;
    list.line_height[SCREEN_MAIN] = 104;
    list.y_pos = list.scroll_base_y = 1000;
    list.start_item[SCREEN_MAIN] = 9;
    list.selected_item = 14;
    list.scroll_mode = SCROLL_SWIPE;
    return list;
}

static void fling_up(struct gui_synclist *list)
{
    gesture_vel_reset(&list_gvel);
    for (int sample = 0; sample < 4; ++sample)
    {
        struct touchevent event = {
            .type = TOUCHEVENT_CONTACT,
            .x = 384, .y = 1000 - sample * 200,
            .tick = current_tick + sample,
        };
        gesture_vel_process(&list_gvel, &event);
    }
    assert(kinetic_start_scrolling(&kinetic, list));
}

static void assert_scroll_equal(const struct gui_synclist *actual,
                                const struct gui_synclist *expected)
{
    assert(actual->selected_item == expected->selected_item);
    assert(actual->start_item[SCREEN_MAIN] == expected->start_item[SCREEN_MAIN]);
    assert(actual->y_pos == expected->y_pos);
    assert(actual->scroll_base_y == expected->scroll_base_y);
    assert(actual->scroll_mode == expected->scroll_mode);
}

int main(void)
{
    global_settings.kinetic_scroll_decel = list_kinetic_scroll_decel_default;
    global_settings.kinetic_scroll_press = list_kinetic_scroll_press_default;
    global_settings.kinetic_scroll_accel = list_kinetic_scroll_accel_default;
    global_settings.kinetic_scroll_brake = list_kinetic_scroll_brake_default;
    struct gui_synclist list = scrollable_list();
    fling_up(&list);
    struct gui_synclist before = list;
    current_tick += RELOAD_INTERVAL;
    kinetic_callback(&kinetic.tmo);
    assert_scroll_equal(&list, &before);
    _gui_synclist_kinetic_scroll(&list);
    assert(list.y_pos > before.y_pos);
    assert(list.selected_item >= 0 && list.selected_item < list.nb_items);
    before = list;
    _gui_synclist_kinetic_scroll(&list);
    assert_scroll_equal(&list, &before);
    _gui_synclist_stop_kinetic_scrolling(&list);
    assert(list.scroll_mode == SCROLL_NONE);
    current_tick += RELOAD_INTERVAL;
    before = list;
    kinetic_callback(&kinetic.tmo);
    _gui_synclist_kinetic_scroll(&list);
    assert_scroll_equal(&list, &before);

    struct gui_synclist *departed = malloc(sizeof(*departed));
    assert(departed);
    *departed = scrollable_list();
    fling_up(departed);
    _gui_synclist_stop_kinetic_scrolling(departed);
    free(departed);
    kinetic_callback(&kinetic.tmo);
    struct gui_synclist destination = scrollable_list();
    destination.scroll_mode = SCROLL_NONE;
    before = destination;
    _gui_synclist_kinetic_scroll(&destination);
    assert_scroll_equal(&destination, &before);

    departed = malloc(sizeof(*departed));
    assert(departed);
    *departed = scrollable_list();
    fling_up(departed);
    free(departed);
    _gui_synclist_stop_kinetic_scrolling(NULL);
    kinetic_callback(&kinetic.tmo);
    _gui_synclist_kinetic_scroll(&destination);
    assert_scroll_equal(&destination, &before);
    return 0;
}
