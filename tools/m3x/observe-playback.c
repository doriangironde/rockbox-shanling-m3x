/* Read-only diagnostics for one exact M3X ELF build. The generated layout
 * comes from its compiler flags, metadata headers, tinyalsa source and nm.
 * Never attach with ptrace, stop the player, or write its memory. */
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "observe-layout.h"

static int read_at(int fd, uintptr_t address, void *value, size_t size)
{
    return pread(fd, value, size, (off_t)address) == (ssize_t)size;
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    char file[128], line[1024];
    snprintf(file, sizeof(file), "/proc/%s/maps", argv[1]);
    FILE *maps = fopen(file, "r");
    if (!maps) return 3;
    unsigned long base = 0, begin, end, offset;
    char permissions[5];
    while (fgets(line, sizeof(line), maps))
        if (sscanf(line, "%lx-%lx %4s %lx", &begin, &end, permissions, &offset) == 4 &&
            offset == 0 && strstr(line, "/rockbox")) { base = begin; break; }
    fclose(maps);
    if (!base) return 4;
    snprintf(file, sizeof(file), "/proc/%s/mem", argv[1]);
    int fd = open(file, O_RDONLY | O_CLOEXEC);
    if (fd < 0) { perror("observer memory"); return 5; }
    unsigned char id3[OBS_SIZE], again[OBS_SIZE];
    unsigned char locked;
    uintptr_t pcm = 0, pcm_again = 0;
    int track, underruns = -1;
    unsigned long frequency, length, elapsed;
    if (!read_at(fd, base + OBS_ID3, id3, sizeof(id3)) ||
        !read_at(fd, base + OBS_ID3, again, sizeof(again)) ||
        memcmp(id3 + OBS_PATH, again + OBS_PATH, OBS_PATH_SIZE) ||
        memcmp(id3 + OBS_TRACK, again + OBS_TRACK, sizeof(track))) {
        close(fd); return 6;
    }
    memcpy(&track, id3 + OBS_TRACK, sizeof(track));
    memcpy(&frequency, id3 + OBS_FREQUENCY, sizeof(frequency));
    memcpy(&length, id3 + OBS_LENGTH, sizeof(length));
    memcpy(&elapsed, id3 + OBS_ELAPSED, sizeof(elapsed));
    if (!read_at(fd, base + OBS_PCM, &pcm, sizeof(pcm)) ||
        (pcm && !read_at(fd, pcm + OBS_UNDERRUNS, &underruns, sizeof(underruns))) ||
        !read_at(fd, base + OBS_PCM, &pcm_again, sizeof(pcm_again)) || pcm != pcm_again) {
        close(fd); return 7;
    }
    if (!read_at(fd, base + OBS_LOCKED, &locked, sizeof(locked))) {
        close(fd); return 8;
    }
    close(fd);
    id3[OBS_PATH + OBS_PATH_SIZE - 1] = 0;
    printf("metadata track=%d frequency=%lu length_ms=%lu elapsed_ms=%lu path=%s\n",
           track, frequency, length, elapsed, id3 + OBS_PATH);
    printf("tinyalsa handle=%lx underruns=%d\n", (unsigned long)pcm, underruns);
    printf("screen_locked %u\n", (unsigned int)locked);
    return 0;
}
