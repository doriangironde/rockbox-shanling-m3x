/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2010 by Thomas Martitz
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

#include <string.h> /* size_t */
#include <dlfcn.h>
#include "file.h"
#include "debug.h"
#include "load_code.h"

#if defined(SHANLING_M3X) && !defined(SIMULATOR)
#include <sys/stat.h>
#include "rbpaths.h"

/* dlopen runs constructors before Rockbox can validate a module header.
 * Reject untrusted paths here, including plugin/overlay API callers. */
static bool m3x_module_path_is_trusted(const char *path)
{
    const size_t root_len = sizeof(M3X_RUNTIME_ROOT) - 1;
    if (strncmp(path, M3X_RUNTIME_ROOT "/", root_len + 1))
        return false;

    const char *relative = path + root_len + 1;
    if (strncmp(relative, "codecs/", 7) && strncmp(relative, "rocks/", 6))
        return false;

    /* Do not let '..', '.', empty components or symlinks escape the root. */
    for (const char *part = relative; *part; )
    {
        const char *end = strchr(part, '/');
        size_t len = end ? (size_t)(end - part) : strlen(part);
        if (!len || (len == 1 && part[0] == '.') ||
            (len == 2 && part[0] == '.' && part[1] == '.'))
            return false;
        if (!end)
            break;
        part = end + 1;
        if (!*part)
            return false;
    }

    char component[MAX_PATH];
    if (strlen(path) >= sizeof(component))
        return false;
    strcpy(component, path);
    /* /data belongs to Android's system user. /data/adb is the root-owned
     * Magisk trust anchor; verify it and every component below it. */
    char *end = component + sizeof("/data/adb") - 1;
    for (;;)
    {
        char saved = *end;
        *end = '\0';
        struct stat st;
        int result = lstat(component, &st);
        *end = saved;
        if (result || st.st_uid != 0 || (st.st_mode & (S_IWGRP | S_IWOTH)) ||
            (saved ? !S_ISDIR(st.st_mode) : !S_ISREG(st.st_mode)))
            return false;
        if (!saved)
            return true;
        end = strchr(end + 1, '/');
        if (!end)
            end = component + strlen(component);
    }
}
#endif

void *lc_open(const char *filename, unsigned char *buf, size_t buf_size)
{
    (void)buf;
    (void)buf_size;
    char path[MAX_PATH];

    const char *fpath = handle_special_dirs(filename, 0, path, sizeof(path));

    if (!fpath)
        return NULL;
#if defined(SHANLING_M3X) && !defined(SIMULATOR)
    if (!m3x_module_path_is_trusted(fpath))
    {
        DEBUGF("refusing untrusted module %s\n", filename);
        return NULL;
    }
#endif

    void *handle = dlopen(fpath, RTLD_NOW);
    if (handle == NULL)
    {
        DEBUGF("failed to load %s\n", filename);
        DEBUGF("lc_open(%s): %s\n", filename, dlerror());
    }
    return handle;
}

void *lc_get_header(void *handle)
{
    char *ret = dlsym(handle, "__header");
    if (ret == NULL)
        ret = dlsym(handle, "___header");

    return ret;
}

void lc_close(void *handle)
{
    dlclose(handle);
}
