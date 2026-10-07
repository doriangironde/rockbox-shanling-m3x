/* Exercise the actual hosted filesystem adapter against M3X storage. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "config.h"
#include "file.h"
#include "rbpaths.h"

int main(void)
{
    char actual[MAX_PATH];
    const char *control = PLAYLIST_CONTROL_FILE ".probe";
    const char *translated = handle_special_dirs(control, NEED_WRITE | IS_FILE,
                                                 actual, sizeof(actual));
    const char expected[] = PIVOT_ROOT ROCKBOX_DIR "/.playlist_control.probe";
    printf("control write path: %s\n", translated ? translated : "NULL");
    if (!translated || strcmp(translated, expected) != 0)
    {
        printf("FAIL: expected %s\n", expected);
        return 1;
    }
    int fd = open(control, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) { perror("create control probe"); return 2; }
    const char record[] = "P:6:/Music:\nA:0:0:/Music/Kevin MacLeod - Sneaky Snitch.mp3\n";
    char data[sizeof(record)] = {0};
    if (write(fd, record, sizeof(record)-1) != sizeof(record)-1 || fsync(fd) < 0 ||
        lseek(fd, 0, SEEK_SET) < 0 || read(fd, data, sizeof(record)-1) != sizeof(record)-1 ||
        strcmp(data, record) != 0)
    { perror("control round trip"); close(fd); remove(control); return 3; }
    close(fd);
    const char *rotated = PLAYLIST_CONTROL_FILE ".probe.old";
    if (rename(control, rotated) < 0) { perror("rotate control"); remove(control); return 4; }
    fd = open(rotated, O_RDONLY);
    if (fd < 0) { perror("reopen control"); remove(rotated); return 5; }
    close(fd);
    remove(rotated);
    const char *required[] = {CODECS_DIR "/mpa.codec", VIEWERS_DIR "/properties.rock",
                             CONFIGFILE, "/Music/Kevin MacLeod - Sneaky Snitch.mp3"};
    for (unsigned i = 0; i < sizeof(required)/sizeof(required[0]); ++i)
    {
        fd = open(required[i], O_RDONLY);
        if (fd < 0) { perror(required[i]); return 6; }
        close(fd);
        printf("readable: %s\n", required[i]);
    }
    puts("PASS: playlist create/write/sync/read/rotate/reopen; codec, plugin, config and song paths");
    return 0;
}
