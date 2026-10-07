#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "../../../rockbox/firmware/target/hosted/ibasso/backlight-ibasso.c"

static int level, writes;
static bool fail_write;
void panicf(const char *fmt, ...) { (void)fmt; abort(); }
void lcd_enable(bool on) { (void)on; }
bool sysfs_get_int(const char *path, int *value)
{ (void)path; *value = 255; return true; }
bool sysfs_set_int(const char *path, int value)
{ (void)path; ++writes; if (fail_write) return false; level = value; return true; }
int main(void)
{
    assert(backlight_hw_init() && level == 200);
    backlight_hw_brightness(40); assert(level == 40);
    backlight_hw_off(); assert(level == 0);
    backlight_hw_on(); assert(level == 40);
    backlight_hw_off();
    backlight_hw_brightness(60); assert(level == 0);
    backlight_hw_on(); assert(level == 60);
    fail_write = true; backlight_hw_brightness(80);
    int failed = writes;
    fail_write = false; backlight_hw_brightness(80);
    assert(writes == failed + 1 && level == 80);
    backlight_hw_brightness(-100); assert(level == 4);
    backlight_hw_brightness(1000); assert(level == 255);
    return 0;
}
