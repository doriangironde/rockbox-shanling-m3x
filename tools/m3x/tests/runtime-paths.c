/* Replay real M3X filesystem translation and dlopen gate, without root/ADB. */
#define RB_FILESYSTEM_OS
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include "config.h"
#include "file.h"
#include "rbpaths.h"

void panicf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    abort();
}

static const char *bad_component;
static int bad_kind;
static int opens;
static char opened[MAX_PATH];

static int mock_lstat(const char *path, struct stat *st)
{
    memset(st, 0, sizeof(*st));
    st->st_uid = 0;
    st->st_mode = strchr(strrchr(path, '/') + 1, '.') ? S_IFREG | 0644 : S_IFDIR | 0755;
    if (bad_component && !strcmp(path, bad_component))
    {
        switch (bad_kind)
        {
            case 0: return -1;
            case 1: st->st_uid = 1000; break;
            case 2: st->st_mode |= S_IWGRP; break;
            case 3: st->st_mode |= S_IWOTH; break;
            case 4: st->st_mode = S_IFLNK | 0777; break;
            case 5: st->st_mode = S_IFIFO | 0600; break;
        }
    }
    return 0;
}

static void *mock_dlopen(const char *path, int flags)
{
    assert(flags == RTLD_NOW);
    opens++;
    snprintf(opened, sizeof(opened), "%s", path);
    return &opens;
}
static void *mock_dlsym(void *handle, const char *name) { (void)name; return handle; }
static int mock_dlclose(void *handle) { (void)handle; return 0; }

#define lstat mock_lstat
#define dlopen mock_dlopen
#define dlsym mock_dlsym
#define dlclose mock_dlclose
#include "lc-unix.c"
#undef lstat

static void expect_path(const char *logical, unsigned flags, const char *expected)
{
    char buffer[MAX_PATH];
    const char *actual = handle_special_dirs(logical, flags, buffer, sizeof(buffer));
    assert(actual && !strcmp(actual, expected));
}

static void expect_rejected(const char *path)
{
    int before = opens;
    assert(lc_open(path, NULL, 0) == NULL);
    assert(opens == before); /* No constructors can have run. */
}

int main(void)
{
    expect_path(CODECS_DIR "/mpa.codec", 0, M3X_RUNTIME_ROOT "/codecs/mpa.codec");
    expect_path(VIEWERS_DIR "/properties.rock", 0, M3X_RUNTIME_ROOT "/rocks/viewers/properties.rock");
    expect_path(PLUGIN_DIR, 0, M3X_RUNTIME_ROOT "/rocks");
    expect_path(M3X_RUNTIME_DIR, 0, M3X_RUNTIME_ROOT);
    expect_path(CONFIGFILE, NEED_WRITE, PIVOT_ROOT ROCKBOX_DIR "/config.cfg");
    expect_path(PLUGIN_GAMES_DATA_DIR "/reversi.rev", NEED_WRITE,
                PIVOT_ROOT ROCKBOX_DIR "/rocks/games/reversi.rev");
    expect_path("/.rockbox-runtime-copy/evil.rock", 0,
                PIVOT_ROOT "/.rockbox-runtime-copy/evil.rock");
    char tiny[4];
    assert(handle_special_dirs(CODECS_DIR "/mpa.codec", 0, tiny, sizeof(tiny)) == NULL);

    assert(lc_open(CODECS_DIR "/mpa.codec", NULL, 0));
    assert(!strcmp(opened, M3X_RUNTIME_ROOT "/codecs/mpa.codec"));
    assert(lc_open(VIEWERS_DIR "/properties.rock", NULL, 0));
    assert(lc_open(VIEWERS_DIR "/jpeg.ovl", NULL, 0));
    const char *untrusted[] = {
        ROCKBOX_DIR "/codecs/mpa.codec", ROCKBOX_DIR "/rocks/viewers/evil.rock",
        "/Music/evil.rock", "/microSD/evil.rock", "/data/local/tmp/evil.rock",
        M3X_RUNTIME_DIR "/other/evil.rock", M3X_RUNTIME_DIR "/rocks-copy/evil.rock",
        CODECS_DIR "/../evil.codec", CODECS_DIR "/../../evil.codec",
        CODECS_DIR "/./mpa.codec", CODECS_DIR "//mpa.codec", CODECS_DIR "/mpa.codec/",
        M3X_RUNTIME_DIR "//codecs/mpa.codec", M3X_RUNTIME_DIR "/codecs",
        M3X_RUNTIME_DIR "-copy/codecs/mpa.codec",
    };
    for (unsigned i = 0; i < sizeof(untrusted) / sizeof(untrusted[0]); i++)
        expect_rejected(untrusted[i]);

    const char *components[] = {
        "/data/adb", "/data/adb/modules", "/data/adb/modules/rockbox_m3x",
        M3X_RUNTIME_ROOT, M3X_RUNTIME_ROOT "/codecs", M3X_RUNTIME_ROOT "/codecs/mpa.codec",
    };
    for (unsigned i = 0; i < sizeof(components) / sizeof(components[0]); i++)
    {
        bad_component = components[i];
        for (bad_kind = 0; bad_kind < 6; bad_kind++)
            expect_rejected(CODECS_DIR "/mpa.codec");
    }
    bad_component = NULL;
    assert(lc_open(CODECS_DIR "/mpa.codec", NULL, 0));
    puts("PASS: private paths, shared data, loader trust gate and no untrusted dlopen");
    return 0;
}
