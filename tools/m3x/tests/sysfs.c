#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/resource.h>
#include "sysfs.h"
int main(int argc, char **argv)
{
    assert(argc == 2);
    const char *path = argv[1];
    int value;
    assert(!sysfs_get_int(path, &value));
    const char *invalid[] = {"", "unavailable\n"};
    for (unsigned i = 0; i < 2; ++i) {
        FILE *f = fopen(path, "w"); assert(f);
        fputs(invalid[i], f); fclose(f);
        assert(!sysfs_get_int(path, &value));
    }
    assert(sysfs_set_int(path, 75));
    assert(sysfs_get_int(path, &value) && value == 75);
    assert(sysfs_set_string(path, "Charging\n"));
    char status[32];
    assert(sysfs_get_string(path, status, sizeof(status)) && !strcmp(status, "Charging"));
    assert(sysfs_set_char(path, 'Q'));
    char c; assert(sysfs_get_char(path, &c) && c == 'Q');
    /* A buffered write can succeed before the kernel rejects its flush. */
    struct rlimit saved, blocked;
    assert(getrlimit(RLIMIT_FSIZE, &saved) == 0);
    blocked = saved; blocked.rlim_cur = 0;
    signal(SIGXFSZ, SIG_IGN);
    assert(setrlimit(RLIMIT_FSIZE, &blocked) == 0);
    assert(!sysfs_set_int(path, 1));
    assert(!sysfs_set_char(path, 'A'));
    assert(!sysfs_set_string(path, "invalid"));
    assert(setrlimit(RLIMIT_FSIZE, &saved) == 0);
    unlink(path);
    return 0;
}
