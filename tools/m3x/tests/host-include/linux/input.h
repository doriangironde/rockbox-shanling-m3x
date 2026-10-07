/* Minimal evdev constants for pipe replay; not a device ABI replacement. */
#ifndef M3X_HOST_LINUX_INPUT_H
#define M3X_HOST_LINUX_INPUT_H
#include <linux/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
struct input_event { struct timeval time; __u16 type, code; __s32 value; };
#define EV_SYN 0
#define EV_KEY 1
#define EV_ABS 3
#define SYN_REPORT 0
#define SYN_DROPPED 3
#define ABS_MT_SLOT 0x2f
#define ABS_MT_POSITION_X 0x35
#define ABS_MT_POSITION_Y 0x36
#define ABS_MT_TRACKING_ID 0x39
#define KEY_VOLUMEDOWN 114
#define KEY_VOLUMEUP 115
#define KEY_POWER 116
#define KEY_NEXTSONG 163
#define KEY_PLAYPAUSE 164
#define KEY_PREVIOUSSONG 165
#define BTN_TOOL_FINGER 0x145
#define EVIOCGNAME(len) _IOC(2, 'E', 0x06, len)
#endif
