/***************************************************************************
 *             __________               __   ___
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2014 by Ilia Sergachev: Initial Rockbox port to iBasso DX50
 * Copyright (C) 2014 by Mario Basister: iBasso DX90 port
 * Copyright (C) 2014 by Simon Rothen: Initial Rockbox repository submission, additional features
 * Copyright (C) 2014 by Udo Schläpfer: Code clean up, additional features
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


#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <unistd.h>

#include "config.h"
#include "debug.h"
#include "panic.h"
#include "pcm.h"
#include "pcm-internal.h"
#include "pcm_sampr.h"
#include "pcm_sink.h"
#include "audiohw.h"

#include "tinyalsa/asoundlib.h"

#include "debug-ibasso.h"
#include "sysfs-ibasso.h"

#ifdef SHANLING_M3X
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "audiohw-m3x.h"
static void m3x_check_hotplug_locked(void);
static bool m3x_usb_write_failed_locked(void);
static bool _output_change_pending;
#endif


/* Tiny alsa handle. */
static struct pcm* _alsa_handle = NULL;


/* Bytes left in the Rockbox PCM frame buffer. */
static size_t _pcm_buffer_size = 0;


/* Rockbox PCM frame buffer. */
static const void  *_pcm_buffer = NULL;


/*
    1: PCM thread suspended.
    0: PCM thread running.
    These are used by pcm_play_[lock|unlock] or pcm_play_dma_[start|stop|pause]. These need to be
    separated because of nested calls for locking and stopping.
*/
static volatile sig_atomic_t _dma_stopped = 1;
static volatile sig_atomic_t _dma_locked  = 1;

#ifdef SHANLING_M3X
/* Android's mutex can let the writing thread reacquire ahead of UI waiters.
 * Publish a pending control request before waiting for the write lock. */
static unsigned int _dma_control_waiters;
#endif


/* Mutex for PCM thread suspend/unsuspend. */
static pthread_mutex_t _dma_suspended_mtx = PTHREAD_MUTEX_INITIALIZER;


/* Signal condition for PCM thread suspend/unsuspend. */
static pthread_cond_t _dma_suspended_cond = PTHREAD_COND_INITIALIZER;


static void* pcm_thread_run(void* nothing)
{
    (void) nothing;

    DEBUGF("DEBUG %s: Thread start.", __func__);

    while(true)
    {
        pthread_mutex_lock(&_dma_suspended_mtx);
#ifdef SHANLING_M3X
        m3x_check_hotplug_locked();
#endif
        while((_dma_stopped == 1) || (_dma_locked == 1)
#ifdef SHANLING_M3X
              || _output_change_pending
              || __atomic_load_n(&_dma_control_waiters, __ATOMIC_RELAXED) != 0
#endif
             )
        {
            DEBUGF("DEBUG %s: Playback suspended.", __func__);
#ifdef SHANLING_M3X
            struct timespec deadline;
            clock_gettime(CLOCK_REALTIME, &deadline);
            deadline.tv_nsec += 250000000;
            if (deadline.tv_nsec >= 1000000000)
            {
                ++deadline.tv_sec;
                deadline.tv_nsec -= 1000000000;
            }
            pthread_cond_timedwait(&_dma_suspended_cond, &_dma_suspended_mtx, &deadline);
            m3x_check_hotplug_locked();
#else
            pthread_cond_wait(&_dma_suspended_cond, &_dma_suspended_mtx);
#endif
            DEBUGF("DEBUG %s: Playback resumed.", __func__);
        }
#ifndef SHANLING_M3X
        pthread_mutex_unlock(&_dma_suspended_mtx);
#endif

        if(_pcm_buffer_size == 0)
        {
            /* Retrive a new PCM buffer from Rockbox. */
            if(! pcm_play_dma_complete_callback(PCM_DMAST_OK, &_pcm_buffer, &_pcm_buffer_size))
            {
                DEBUGF("DEBUG %s: No new buffer.", __func__);
#ifdef SHANLING_M3X
                pthread_mutex_unlock(&_dma_suspended_mtx);
#endif
                usleep( 10000 );
                continue;
            }
        }
        pcm_play_dma_status_callback(PCM_DMAST_STARTED);

        /* This relies on Rockbox PCM frame buffer size == ALSA PCM frame buffer size. */
        int write_status = pcm_write(_alsa_handle, _pcm_buffer, _pcm_buffer_size);
        if(write_status != 0)
        {
            DEBUGF("ERROR %s: pcm_write failed: %s.", __func__, pcm_get_error(_alsa_handle));
#ifdef SHANLING_M3X
            /* A failed PREPARE can free the kernel's DSP client. Retrying the
             * same handle then floods the kernel log instead of recovering. */
            fprintf(stderr, "M3X PCM write failed (%d, %lu bytes): %s\n",
                    write_status, (unsigned long)_pcm_buffer_size,
                    pcm_get_error(_alsa_handle));
            if (m3x_usb_write_failed_locked())
            {
                pthread_mutex_unlock(&_dma_suspended_mtx);
                continue;
            }
            /* A control thread may replace the handle as soon as we unlock. */
            char error[128];
            snprintf(error, sizeof(error), "%s", pcm_get_error(_alsa_handle));
            _dma_stopped = 1;
            pthread_mutex_unlock(&_dma_suspended_mtx);
            panicf("M3X PCM write failed: %s", error);
#endif
            usleep( 10000 );
            continue;
        }

        _pcm_buffer_size = 0;
#ifdef SHANLING_M3X
        pthread_mutex_unlock(&_dma_suspended_mtx);
#endif

        /*DEBUGF("DEBUG %s: Thread running.", __func__);*/
    }

    DEBUGF("DEBUG %s: Thread end.", __func__);

    return 0;
}


#ifdef DEBUG

/* https://github.com/tinyalsa/tinyalsa/blob/master/tinypcminfo.c */

static const char* const format_lookup[] =
{
    /*[0] =*/ "S8",
    "U8",
    "S16_LE",
    "S16_BE",
    "U16_LE",
    "U16_BE",
    "S24_LE",
    "S24_BE",
    "U24_LE",
    "U24_BE",
    "S32_LE",
    "S32_BE",
    "U32_LE",
    "U32_BE",
    "FLOAT_LE",
    "FLOAT_BE",
    "FLOAT64_LE",
    "FLOAT64_BE",
    "IEC958_SUBFRAME_LE",
    "IEC958_SUBFRAME_BE",
    "MU_LAW",
    "A_LAW",
    "IMA_ADPCM",
    "MPEG",
    /*[24] =*/ "GSM",
    [31] = "SPECIAL",
    "S24_3LE",
    "S24_3BE",
    "U24_3LE",
    "U24_3BE",
    "S20_3LE",
    "S20_3BE",
    "U20_3LE",
    "U20_3BE",
    "S18_3LE",
    "S18_3BE",
    "U18_3LE",
    /*[43] =*/ "U18_3BE"
};


static const char* pcm_get_format_name(unsigned int bit_index)
{
    return(bit_index < 43 ? format_lookup[bit_index] : NULL);
}

#endif


/* Thread that copies the Rockbox PCM buffer to ALSA. */
static pthread_t _pcm_thread;


/* ALSA card and device. */
#ifdef SHANLING_M3X
static unsigned int CARD = 0;
static unsigned int _pcm_flags = PCM_OUT;
static bool _usb_earpods;
/* Keep stale peak-meter indices in bounds during a capability transition.
 * Only entry 0 is advertised while USB is selected. */
static const unsigned long ear_pods_samprs[HW_NUM_FREQ] = { [0 ... HW_NUM_FREQ-1] = 44100 };

/* Select only the USB device whose stream was verified on this player.
 * Card numbers are assigned at enumeration, so never assume card 1. */
static int find_earpods_card(void)
{
    for (int card = 1; card < 32; ++card)
    {
        char path[64], id[32];
        snprintf(path, sizeof(path), "/proc/asound/card%d/usbid", card);
        FILE *file = fopen(path, "r");
        if (!file)
            continue;
        bool found = fgets(id, sizeof(id), file) &&
                     strncmp(id, "05ac:110b", 9) == 0 &&
                     (id[9] == '\n' || id[9] == '\0');
        fclose(file);
        if (found)
            return card;
    }
    return -1;
}
#else
static const unsigned int CARD   = 0;
#endif
static const unsigned int DEVICE = 0;


/* ALSA config. */
static struct pcm_config _config;

#ifdef SHANLING_M3X
static void (*_output_callback)(void);
static int _ignored_usb_card = -1;
static bool _output_fault;
static struct timespec _next_hotplug_check;

static void m3x_request_output_change(void)
{
    if (!_output_change_pending)
    {
        _output_change_pending = true;
        if (_output_callback)
            _output_callback();
    }
}

static void m3x_check_hotplug_locked(void)
{
    if (!_alsa_handle)
        return;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (now.tv_sec < _next_hotplug_check.tv_sec ||
        (now.tv_sec == _next_hotplug_check.tv_sec && now.tv_nsec < _next_hotplug_check.tv_nsec))
        return;
    _next_hotplug_check = now;
    _next_hotplug_check.tv_nsec += 250000000;
    if (_next_hotplug_check.tv_nsec >= 1000000000)
    {
        ++_next_hotplug_check.tv_sec;
        _next_hotplug_check.tv_nsec -= 1000000000;
    }
    int card = find_earpods_card();
    if (card < 0)
        _ignored_usb_card = -1;
    unsigned int desired = card >= 0 && card != _ignored_usb_card ? card : 0;
    if (desired != CARD)
        m3x_request_output_change();
}

static bool m3x_usb_write_failed_locked(void)
{
    if (!_usb_earpods)
        return false;
    /* Keep the current buffer and stop touching the removed PCM. The audio
     * thread will stop decoding, switch endpoints and resume this position. */
    _output_fault = true;
    m3x_request_output_change();
    return true;
}

static void m3x_configure_output(unsigned int card)
{
    CARD = card;
    _usb_earpods = card != 0;
    _pcm_flags = PCM_OUT | (_usb_earpods ? PCM_NORESTART : 0);
    builtin_pcm_sink.caps.samprs = _usb_earpods ? ear_pods_samprs : hw_freq_sampr;
    builtin_pcm_sink.caps.num_samprs = _usb_earpods ? 1 : HW_NUM_FREQ;
    builtin_pcm_sink.caps.default_freq = _usb_earpods ? 0 : HW_FREQ_DEFAULT;
    builtin_pcm_sink.pending_freq = builtin_pcm_sink.caps.default_freq;
    builtin_pcm_sink.configured_freq = -1U;
    memset(&_config, 0, sizeof(_config));
    _config.channels = 2;
    _config.rate = builtin_pcm_sink.caps.samprs[builtin_pcm_sink.pending_freq];
    _config.period_size = _usb_earpods ? 480 : 256;
    _config.period_count = 4;
    _config.format = PCM_FORMAT_S16_LE;
    if (_usb_earpods)
    {
        _config.start_threshold = 480;
        _config.stop_threshold = 1920;
    }
    else
        audiohw_m3x_init();
}

static bool m3x_try_open_output(void)
{
    for (int retry = 0; retry <= (_usb_earpods ? 10 : 1); ++retry)
    {
        _alsa_handle = pcm_open(CARD, DEVICE, _pcm_flags, &_config);
        if (pcm_is_ready(_alsa_handle) && pcm_prepare(_alsa_handle) == 0)
            return true;
        fprintf(stderr, "M3X output open/prepare failed card=%u: %s\n",
                CARD, pcm_get_error(_alsa_handle));
        pcm_close(_alsa_handle);
        _alsa_handle = NULL;
        if (_usb_earpods)
        {
            if (find_earpods_card() != (int)CARD)
                break;
            if (retry < 10)
                usleep(200000);
        }
        else
            audiohw_m3x_fallback_route();
    }
    return false;
}

static void m3x_open_output(void)
{
    if (!m3x_try_open_output())
    {
        if (!_usb_earpods)
        {
            panicf("M3X internal PCM unavailable");
            return;
        }
        _ignored_usb_card = CARD;
        m3x_configure_output(0);
        if (!m3x_try_open_output())
        {
            panicf("M3X internal PCM unavailable after USB removal");
            return;
        }
    }
    fprintf(stderr, "M3X output: %s card=%u device=0 rate=%u\n",
            _usb_earpods ? "Apple USB-C EarPods" : "internal DAC", CARD, _config.rate);
}

void pcm_m3x_set_output_callback(void (*callback)(void))
{
    pthread_mutex_lock(&_dma_suspended_mtx);
    _output_callback = callback;
    if (_output_change_pending && callback)
        callback();
    pthread_mutex_unlock(&_dma_suspended_mtx);
}

/* Called by the audio thread after halting the codec and stopping PCM. */
void pcm_m3x_switch_output(void)
{
    pthread_mutex_lock(&_dma_suspended_mtx);
    int usb = find_earpods_card();
    unsigned int desired = usb >= 0 && usb != _ignored_usb_card ? usb : 0;
    if (_alsa_handle && (desired != CARD || _output_fault))
    {
        _dma_stopped = 1;
        if (!_usb_earpods)
        {
            audiohw_m3x_mute();
            audiohw_m3x_fallback_route();
        }
        pcm_close(_alsa_handle);
        _alsa_handle = NULL;
        _pcm_buffer = NULL;
        _pcm_buffer_size = 0;
        m3x_configure_output(desired);
        m3x_open_output();
    }
    _output_fault = false;
    _output_change_pending = false;
    pthread_cond_signal(&_dma_suspended_cond);
    pthread_mutex_unlock(&_dma_suspended_mtx);
}
#endif


static void sink_dma_init(void)
{
    TRACE;

#ifdef SHANLING_M3X
    /* Keep PCM writes, callbacks, stop and rate changes under one lock.
     * Callbacks may re-enter the sink on this thread, so it must recurse. */
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_destroy(&_dma_suspended_mtx);
    pthread_mutex_init(&_dma_suspended_mtx, &attr);
    pthread_mutexattr_destroy(&attr);
    /*
        Bring up the AK4497s and the mixer controls they need. Hosted targets
        never call the generic audiohw_init(), so this is done here.
    */
    int usb_card = find_earpods_card();
    /* The M3X briefly removes and re-enumerates USB audio during Android
     * takeover. A single scan in that gap selected the internal DAC even
     * with EarPods attached. Allow a bounded enumeration grace period. */
    for (int retry = 0; usb_card < 0 && retry < 20; ++retry)
    {
        usleep(100000);
        usb_card = find_earpods_card();
    }
    m3x_configure_output(usb_card >= 0 ? usb_card : 0);
#endif

#ifdef DEBUG
#ifdef SHANLING_M3X
    if (!_usb_earpods)
    {
#endif

    /*
        DEBUG sink_dma_init: Access: 0x000009
        DEBUG sink_dma_init: Format[0]: 0x000044
        DEBUG sink_dma_init: Format[1]: 0x000010
        DEBUG sink_dma_init: Format: S16_LE
        DEBUG sink_dma_init: Format: S24_LE
        DEBUG sink_dma_init: Format: S20_3LE
        DEBUG sink_dma_init: Subformat: 0x000001
        DEBUG sink_dma_init: Rate: min = 8000Hz, max = 192000Hz
        DEBUG sink_dma_init: Channels: min = 2, max = 2
        DEBUG sink_dma_init: Sample bits: min=16, max=32
        DEBUG sink_dma_init: Period size: min=8, max=10922
        DEBUG sink_dma_init: Period count: min=3, max=128
        DEBUG sink_dma_init: 0 mixer controls.
    */

    struct pcm_params* params = pcm_params_get(CARD, DEVICE, PCM_OUT);
    if(params == NULL)
    {
        DEBUGF("ERROR %s: Card/device does not exist.", __func__);
        panicf("ERROR %s: Card/device does not exist.", __func__);
        return;
    }

    struct pcm_mask* m = pcm_params_get_mask(params, PCM_PARAM_ACCESS);
    if(m)
    {
        DEBUGF("DEBUG %s: Access: %#08x", __func__, m->bits[0]);
    }

    m = pcm_params_get_mask(params, PCM_PARAM_FORMAT);
    if(m)
    {
        DEBUGF("DEBUG %s: Format[0]: %#08x", __func__, m->bits[0]);
        DEBUGF("DEBUG %s: Format[1]: %#08x", __func__, m->bits[1]);

        unsigned int j;
        unsigned int k;
        const unsigned int bitcount = sizeof(m->bits[0]) * 8;
        for(k = 0; k < 2; ++k)
        {
            for(j = 0; j < bitcount; ++j)
            {
                const char* name;
                if(m->bits[k] & (1 << j))
                {
                    name = pcm_get_format_name(j + (k * bitcount));
                    if(name)
                    {
                        DEBUGF("DEBUG %s: Format: %s", __func__, name);
                    }
                }
            }
        }
    }

    m = pcm_params_get_mask(params, PCM_PARAM_SUBFORMAT);
    if(m)
    {
        DEBUGF("DEBUG %s: Subformat: %#08x", __func__, m->bits[0]);
    }

    unsigned int min = pcm_params_get_min(params, PCM_PARAM_RATE);
    unsigned int max = pcm_params_get_max(params, PCM_PARAM_RATE) ;
    DEBUGF("DEBUG %s: Rate: min = %uHz, max = %uHz", __func__, min, max);

    min = pcm_params_get_min(params, PCM_PARAM_CHANNELS);
    max = pcm_params_get_max(params, PCM_PARAM_CHANNELS);
    DEBUGF("DEBUG %s: Channels: min = %u, max = %u", __func__, min, max);

    min = pcm_params_get_min(params, PCM_PARAM_SAMPLE_BITS);
    max = pcm_params_get_max(params, PCM_PARAM_SAMPLE_BITS);
    DEBUGF("DEBUG %s: Sample bits: min=%u, max=%u", __func__, min, max);

    min = pcm_params_get_min(params, PCM_PARAM_PERIOD_SIZE);
    max = pcm_params_get_max(params, PCM_PARAM_PERIOD_SIZE);
    DEBUGF("DEBUG %s: Period size: min=%u, max=%u", __func__, min, max);

    min = pcm_params_get_min(params, PCM_PARAM_PERIODS);
    max = pcm_params_get_max(params, PCM_PARAM_PERIODS);
    DEBUGF("DEBUG %s: Period count: min=%u, max=%u", __func__, min, max);

    pcm_params_free(params);

    struct mixer* mixer = mixer_open(CARD);
    if(! mixer)
    {
        DEBUGF("ERROR %s: Failed to open mixer.", __func__);
    }
    else
    {
        int num_ctls = mixer_get_num_ctls(mixer);

        DEBUGF("DEBUG %s: %d mixer controls.", __func__, num_ctls);

        mixer_close(mixer);
    }

#ifdef SHANLING_M3X
    }
#endif

#endif

    if(_alsa_handle != NULL)
    {
        DEBUGF("ERROR %s: Allready initialized.", __func__);
        panicf("ERROR %s: Allready initialized.", __func__);
        return;
    }

#ifdef SHANLING_M3X
    m3x_open_output();
#else
    /*
        Rockbox outputs 16 Bit/44.1kHz stereo by default.

        ALSA frame buffer size = config.period_count * config.period_size * config.channels * (16 \ 8)
                               = 4 * 256 * 2 * 2
                               = 4096
                               = Rockbox PCM buffer size
        pcm_thread_run relies on this size match. See pcm_mixer.h.
    */
    _config.channels          = 2;
    _config.rate              = hw_freq_sampr[HW_FREQ_DEFAULT];
    _config.period_size       = 256;
    _config.period_count      = 4;
    /* The M3X DSP rejects S32_LE at PREPARE even though hw_params accepts it.
     * Use the same 16-bit layout as its software-volume output. */
    _config.format            = PCM_FORMAT_S16_LE;
    _config.start_threshold   = 0;
    _config.stop_threshold    = 0;
    _config.silence_threshold = 0;
    DEBUGF("DEBUG %s: pcm_open(card=%d dev=%d flags=%u rate=%u ch=%u fmt=%d "
           "period_size=%u period_count=%u)", __func__, CARD, DEVICE, PCM_OUT,
           _config.rate, _config.channels, (int)_config.format,
           _config.period_size, _config.period_count);

    _alsa_handle = pcm_open(CARD, DEVICE, PCM_OUT, &_config);

    if(! pcm_is_ready(_alsa_handle))
    {
        DEBUGF("ERROR %s: pcm_open failed: %s.", __func__, pcm_get_error(_alsa_handle));
        panicf("ERROR %s: pcm_open failed: %s.", __func__, pcm_get_error(_alsa_handle));
        return;
    }

    DEBUGF("DEBUG %s: ALSA PCM frame buffer size: %d.", __func__, pcm_frames_to_bytes(_alsa_handle, pcm_get_buffer_size(_alsa_handle)));
#endif

    /* Create pcm thread in the suspended state. */
    pthread_mutex_lock(&_dma_suspended_mtx);
    _dma_stopped = 1;
#ifdef SHANLING_M3X
    _dma_locked  = 0;
#else
    _dma_locked  = 1;
#endif
    pthread_create(&_pcm_thread, NULL, pcm_thread_run, NULL);
    pthread_mutex_unlock(&_dma_suspended_mtx);
}


static void sink_dma_start(const void *addr, size_t size)
{
    TRACE;

    #ifdef SHANLING_M3X
    /*
        The M3X codec is an AK4497 pair reached over ALSA mixer controls, not
        an iBasso /sys/class/codec node.
    */
    /* Unmute under the PCM lock below. */
#else
    /*
        DX50
        /sys/class/codec/mute
        Mute:   echo 'A' > /sys/class/codec/mute
        Unmute: echo 'B' > /sys/class/codec/mute

        DX90?
    */
    if(! sysfs_set_char(sysfs_paths[SYSFS_MUTE], 'B'))
    {
        DEBUGF("ERROR %s: Could not unmute.", __func__);
        panicf("ERROR %s: Could not unmute.", __func__);
    }
#endif

    pthread_mutex_lock(&_dma_suspended_mtx);
#ifdef SHANLING_M3X
    if (!_usb_earpods)
        audiohw_m3x_unmute();
#endif
    _pcm_buffer      = addr;
    _pcm_buffer_size = size;
    _dma_stopped = 0;
    pthread_cond_signal(&_dma_suspended_cond);
    pthread_mutex_unlock(&_dma_suspended_mtx);
}

static void sink_dma_stop(void)
{
    TRACE;

    pthread_mutex_lock(&_dma_suspended_mtx);
    _dma_stopped = 1;
    pcm_stop(_alsa_handle);
    pthread_mutex_unlock(&_dma_suspended_mtx);
}


/* Unessecary play locks before sink_dma_postinit. */
static int _play_lock_recursion_count = -10000;


static void sink_dma_postinit(void)
{
    TRACE;

    _play_lock_recursion_count = 0;
}


static void sink_lock(void)
{
    TRACE;
#ifdef SHANLING_M3X
    if (_play_lock_recursion_count >= 0)
    {
        __atomic_add_fetch(&_dma_control_waiters, 1, __ATOMIC_RELAXED);
        pthread_mutex_lock(&_dma_suspended_mtx);
        __atomic_sub_fetch(&_dma_control_waiters, 1, __ATOMIC_RELAXED);
    }
#else
    ++_play_lock_recursion_count;

    if(_play_lock_recursion_count == 1)
    {
        pthread_mutex_lock(&_dma_suspended_mtx);
        _dma_locked = 1;
        pthread_mutex_unlock(&_dma_suspended_mtx);
    }
#endif
}


static void sink_unlock(void)
{
    TRACE;
#ifdef SHANLING_M3X
    if (_play_lock_recursion_count >= 0)
    {
        pthread_cond_signal(&_dma_suspended_cond);
        pthread_mutex_unlock(&_dma_suspended_mtx);
    }
#else
    --_play_lock_recursion_count;

    if(_play_lock_recursion_count == 0)
    {
        pthread_mutex_lock(&_dma_suspended_mtx);
        _dma_locked = 0;
        pthread_cond_signal(&_dma_suspended_cond);
        pthread_mutex_unlock(&_dma_suspended_mtx);
    }
#endif
}


static void sink_set_freq(uint16_t freq)
{
#ifdef SHANLING_M3X
    pthread_mutex_lock(&_dma_suspended_mtx);
    if (freq >= builtin_pcm_sink.caps.num_samprs)
    {
        pthread_mutex_unlock(&_dma_suspended_mtx);
        return;
    }
    unsigned int rate = builtin_pcm_sink.caps.samprs[freq];
#else
    unsigned int rate = hw_freq_sampr[freq];
#endif

    DEBUGF("DEBUG %s: Current sample rate: %u, next sampe rate: %u.", __func__, _config.rate, rate);

#ifdef SHANLING_M3X
    /* MultiMedia1 on the M3X accepts anything up to 1411200 Hz. */
    if(( _config.rate != rate) && (rate >= 8000) && (rate <= 384000))
#else
    if(( _config.rate != rate) && (rate >= 8000) && (rate <= 192000))
#endif
    {
        _config.rate = rate;

        pcm_close(_alsa_handle);
#ifdef SHANLING_M3X
        _alsa_handle = pcm_open(CARD, DEVICE, _pcm_flags, &_config);
#else
        _alsa_handle = pcm_open(CARD, DEVICE, PCM_OUT, &_config);
#endif

        if(! pcm_is_ready(_alsa_handle))
        {
            DEBUGF("ERROR %s: pcm_open failed: %s.", __func__, pcm_get_error(_alsa_handle));
            panicf("ERROR %s: pcm_open failed: %s.", __func__, pcm_get_error(_alsa_handle));
        }
#ifdef SHANLING_M3X
        if (pcm_prepare(_alsa_handle) != 0)
        {
            fprintf(stderr, "M3X PCM prepare failed after rate change: %s\n",
                    pcm_get_error(_alsa_handle));
            panicf("M3X PCM prepare failed: %s", pcm_get_error(_alsa_handle));
        }
#endif
    }
#ifdef SHANLING_M3X
    pthread_mutex_unlock(&_dma_suspended_mtx);
#endif
}

void pcm_close_device(void)
{
    TRACE;

    pthread_mutex_lock(&_dma_suspended_mtx);
    _dma_stopped = 1;
    pcm_close(_alsa_handle);
    _alsa_handle = NULL;
    pthread_mutex_unlock(&_dma_suspended_mtx);
}

struct pcm_sink builtin_pcm_sink = {
    .caps = {
        .samprs       = hw_freq_sampr,
        .num_samprs   = HW_NUM_FREQ,
        .default_freq = HW_FREQ_DEFAULT,
        .volume_type  = PCM_NATIVE_VOLUME_TYPE,
    },
    .ops = {
        .init     = sink_dma_init,
        .postinit = sink_dma_postinit,
        .set_freq = sink_set_freq,
        .lock     = sink_lock,
        .unlock   = sink_unlock,
        .play     = sink_dma_start,
        .stop     = sink_dma_stop,
    },
};
