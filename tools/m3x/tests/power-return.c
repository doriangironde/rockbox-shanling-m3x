/* Exercise the real target exit boundary without issuing a hardware reboot. */
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#ifndef RB_POWER_OFF
#define RB_POWER_OFF 0x4321
#endif
static int fake_reboot(int command);
#define reboot fake_reboot
#include "../../../rockbox/firmware/target/hosted/ibasso/power-ibasso.c"

static bool closed, vold_exit;
void button_close_device(void) { closed = true; }
bool vold_monitor_forced_close_imminent(void) { return vold_exit; }
static int fake_reboot(int command)
{
    assert(command == RB_POWER_OFF && closed);
    _exit(90);
}
static void verify_closed(void) { assert(closed); }
int main(void)
{
    const int expected[] = {0, 90, 42};
    for (int mode = 0; mode < 3; ++mode)
    {
        pid_t child = fork();
        assert(child >= 0);
        if (child == 0)
        {
            atexit(verify_closed);
            m3x_set_android_return(true);
            if (mode != 0) m3x_set_android_return(false);
            vold_exit = mode == 2;
            power_off();
            abort();
        }
        int status;
        assert(waitpid(child, &status, 0) == child);
        assert(WIFEXITED(status) && WEXITSTATUS(status) == expected[mode]);
    }
    return 0;
}
