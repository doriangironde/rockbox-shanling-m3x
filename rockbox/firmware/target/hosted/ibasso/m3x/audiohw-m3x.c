/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Audio hardware layer for the Shanling M3X.
 *
 * The M3X (MSM8937 / MTP, stock firmware V1.75) does NOT use the iBasso
 * /sys/class/codec interface. Its headphone and balanced outputs are driven
 * by two AKM AK4497 DACs on i2c bus 5 (slaves 0x10 and 0x12, see
 * soc/i2c@7af5000/ak4497-i2c-codec@10 and @12 in the device tree), fed from
 * the Quinary MI2S. Everything the codec needs is reachable through ordinary
 * ALSA mixer controls on card 0 ("msm8952sndcardm"), so we drive it with the
 * tinyalsa mixer API rather than sysfs.
 *
 * Routing (from /system/etc/mixer_paths_mtp.xml, the file the vendor audio HAL
 * loads):
 *   deep-buffer-playback headphones:
 *       PRI/QUIN_MI2S_RX Audio Mixer MultiMedia1 = 1
 *       MI2S_RX Channels = Two
 * i.e. PCM card 0 device 0 (MultiMedia1) -> Quinary MI2S -> AK4497.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 ****************************************************************************/

#include <stdbool.h>
#include <stdlib.h>

#include "config.h"
#include "debug.h"

#include "audiohw.h"
#include "audiohw-m3x.h"
#include "pcm_sw_volume.h"

#include "tinyalsa/asoundlib.h"

#include "debug-ibasso.h"


/* ALSA card holding the WCD9335 + the AK4497 DACs. */
#define M3X_CARD 0

/* ALSA control names as exported by the vendor ak4497 / msm asoc drivers. */
#define M3X_CTL_VOL_L        "AK4497 Lch Digital Volume"
#define M3X_CTL_VOL_R        "AK4497 Rch Digital Volume"
#define M3X_CTL_MUTE         "AK4497 mute control"
#define M3X_CTL_POLO         "AK4497 POLO mode control"
#define M3X_CTL_POLO_DAC     "AK4497 polo dac number"
#define M3X_CTL_FILTER       "AK4497 filter control"
#define M3X_CTL_GAIN         "AK4497 gain control"
#define M3X_CTL_FORMAT       "AK4497 Format"
#define M3X_CTL_BITMODE      "Bit mode control"
#define M3X_CTL_OUT_MUX      "Output Mux"
#define M3X_CTL_ROUTE_MM1    "QUIN_MI2S_RX Audio Mixer MultiMedia1"
#define M3X_CTL_MI2S_CHAN    "MI2S_RX Channels"
#define M3X_CTL_QUIN_FORMAT  "QUIN MI2S RX Format"

/*
 * The vendor ak4497 driver maps whatever we write into a non-linear,
 * non-monotonic curve and overflows its own byte for inputs above ~215
 * (measured with tools/m3x/m3xtest sweep: send 1..100 walks monotonically
 * from roughly -106 dB to 0 dB, then wraps). Sending 0 is the only way to get
 * a guaranteed-silent state, so the usable window is kept well below the wrap.
 */
/* Sending 50 reproduced the captured OEM applied value 205. Do not extrapolate
 * the vendor's wrapping curve into a dB scale; use software attenuation. */
#define M3X_VOL_REFERENCE 50

static struct mixer *_mixer = NULL;


static struct mixer_ctl *ctl(const char *name)
{
    if(! _mixer)
        return NULL;
    return mixer_get_ctl_by_name(_mixer, name);
}


static int ctl_set_int(const char *name, int value)
{
    struct mixer_ctl *c = ctl(name);
    if(! c)
    {
        DEBUGF("ERROR %s: no such mixer control: %s", __func__, name);
        return -1;
    }
    if(mixer_ctl_set_value(c, 0, value) < 0)
    {
        DEBUGF("ERROR %s: could not set %s = %d", __func__, name, value);
        return -1;
    }
    DEBUGF("DEBUG %s: %s = %d", __func__, name, value);
    return 0;
}


static int ctl_set_enum(const char *name, const char *value)
{
    struct mixer_ctl *c = ctl(name);
    if(! c)
    {
        DEBUGF("ERROR %s: no such mixer control: %s", __func__, name);
        return -1;
    }
    if(mixer_ctl_set_enum_by_string(c, value) < 0)
    {
        DEBUGF("ERROR %s: could not set %s = %s", __func__, name, value);
        return -1;
    }
    DEBUGF("DEBUG %s: %s = %s", __func__, name, value);
    return 0;
}


/* True when the control exists. Used to keep startup resilient. */
static bool ctl_present(const char *name)
{
    return ctl(name) != NULL;
}


void audiohw_m3x_mixer_open(void)
{
    if(_mixer)
        return;

    _mixer = mixer_open(M3X_CARD);
    if(! _mixer)
    {
        DEBUGF("ERROR %s: mixer_open(%d) failed", __func__, M3X_CARD);
        return;
    }

    DEBUGF("DEBUG %s: opened mixer '%s' on card %d (%d controls)",
           __func__, mixer_get_name(_mixer), M3X_CARD, mixer_get_num_ctls(_mixer));
}


void audiohw_m3x_mixer_close(void)
{
    if(_mixer)
    {
        mixer_close(_mixer);
        _mixer = NULL;
    }
}


/*
    See audiohw-m3x.h.
    Called from sink_dma_init() because hosted targets never call
    audiohw_init() - only native targets implement and invoke it.
*/
void audiohw_m3x_init(void)
{
    TRACE;

    audiohw_m3x_mixer_open();
    audiohw_m3x_mute();
    ctl_set_int(M3X_CTL_VOL_L, M3X_VOL_REFERENCE);
    ctl_set_int(M3X_CTL_VOL_R, M3X_VOL_REFERENCE);

    /*
        Get the DAC out of "Low Power Bypass" and route MultiMedia1 to the
        Quinary MI2S that feeds the AK4497s. The vendor HAL does the same via
        its "headphones" mixer path.
    */
    if(ctl_present(M3X_CTL_OUT_MUX))
        ctl_set_enum(M3X_CTL_OUT_MUX, "HiFi 2V Mode");

    if(ctl_present(M3X_CTL_POLO_DAC))
        ctl_set_enum(M3X_CTL_POLO_DAC, "two");

    if(ctl_present(M3X_CTL_POLO))
        ctl_set_enum(M3X_CTL_POLO, "pcm");

    if(ctl_present(M3X_CTL_FILTER))
        ctl_set_enum(M3X_CTL_FILTER, "slow");

    if(ctl_present(M3X_CTL_GAIN))
        ctl_set_enum(M3X_CTL_GAIN, "high");

    if(ctl_present(M3X_CTL_QUIN_FORMAT))
        ctl_set_enum(M3X_CTL_QUIN_FORMAT, "LPCM");

    if(ctl_present(M3X_CTL_MI2S_CHAN))
        ctl_set_enum(M3X_CTL_MI2S_CHAN, "Two");

    if(ctl_present(M3X_CTL_ROUTE_MM1))
        ctl_set_int(M3X_CTL_ROUTE_MM1, 1);

    /*
        "AK4497 Format" selects the codec's word width, and it must NOT be
        S32_LE. Earlier open-only probes with the Quinary MI2S route enabled
        reported:

            AK4497 Format = S16_LE -> pcm_open OK
            AK4497 Format = S24_LE -> pcm_open OK
            AK4497 Format = S32_LE -> pcm_open fails "cannot set hw params"

        Native PCM S32_LE also fails at DSP PREPARE despite passing hw_params;
        the production stream and software-volume output now use S16_LE.
        S16_LE is the OEM's own default and is what Android leaves
        the control at, so leave it alone; it is set explicitly only to undo a
        previous run that may have set S32_LE.
    */
    if(ctl_present(M3X_CTL_FORMAT))
        ctl_set_enum(M3X_CTL_FORMAT, "S16_LE");

    /*
        "Bit mode control" likewise has to stay at 16. It looks like it should
        track the sample format, but it does not: any value other than 16 makes
        the Quinary MI2S reject its hw params.

        bit mode=16, PCM S16_LE -> open OK; PREPARE verified separately
        bit mode=16, PCM S32_LE -> open OK but PREPARE fails
        bit mode=16, PCM S24_LE -> open OK only; playback not established
        bit mode=24 or 32, any PCM -> open FAIL

        The PCM data is unaffected, so this only constrains the codec side.
        It is set explicitly to undo a previous run that moved it.
    */
    if(ctl_present(M3X_CTL_BITMODE))
        ctl_set_enum(M3X_CTL_BITMODE, "16");

    /* sink_dma_start() unmutes after Rockbox has applied its volume setting. */
}


/*
    Undo the DAC routing so the WCD9335 stays in its idle state and nothing
    reaches the headphone amplifier or the speaker.

    This used to be a hard requirement: the AK4497s sit behind I2C5, which could
    not claim GPIO_18 because soc:gpio_keys held that pin, so the Quinary MI2S
    always refused to start. That pin conflict is now fixed in the device tree
    (tools/m3x/patch-dtb-gpio18.py), so the Quinary route works and this is only
    a safety net if pcm_open ever fails again. Note it deliberately leaves
    "Bit mode control" alone - that control must stay at 16 for the route to
    come up at all, so resetting it here would only make a retry fail too.
*/
void audiohw_m3x_fallback_route(void)
{
    TRACE;

    if(ctl_present(M3X_CTL_MUTE))
        ctl_set_enum(M3X_CTL_MUTE, "mute");

    if(ctl_present(M3X_CTL_ROUTE_MM1))
        ctl_set_int(M3X_CTL_ROUTE_MM1, 0);

    if(ctl_present(M3X_CTL_MI2S_CHAN))
        ctl_set_enum(M3X_CTL_MI2S_CHAN, "One");

    if(ctl_present(M3X_CTL_OUT_MUX))
        ctl_set_enum(M3X_CTL_OUT_MUX, "Low Power Bypass");

    if(ctl_present(M3X_CTL_FORMAT))
        ctl_set_enum(M3X_CTL_FORMAT, "S16_LE");
}


/* See audiohw.h. */
void audiohw_m3x_unmute(void)
{
    if(ctl_present(M3X_CTL_MUTE))
        ctl_set_enum(M3X_CTL_MUTE, "unmute");
}


/* See audiohw.h. */
void audiohw_m3x_mute(void)
{
    if(ctl_present(M3X_CTL_MUTE))
        ctl_set_enum(M3X_CTL_MUTE, "mute");
}


/* See audiohw.h. */
void audiohw_set_volume(int vol_l, int vol_r)
{
    TRACE;
    /* The minimum setting is true silence; other settings are centibels.
     * Stereo volume lets sound.c implement balance without resetting gain. */
    if (vol_l > 0) vol_l = 0;
    if (vol_r > 0) vol_r = 0;
    pcm_set_master_volume(vol_l <= -1280 ? PCM_MUTE_LEVEL : vol_l,
                          vol_r <= -1280 ? PCM_MUTE_LEVEL : vol_r);
}


/* See audiohw.h. */
void audiohw_set_filter_roll_off(int val)
{
    static const char * const filters[] =
    {
        "sharp",      /* 0 - the driver default */
        "slow",       /* 1 */
        "delay-sharp",
        "delay-slow",
        "supp-slow",
        "delay-sslow",
    };

    TRACE;

    if(val < 0 || val >= (int)(sizeof(filters) / sizeof(filters[0])))
        val = 0;

    DEBUGF("DEBUG %s: val: %d", __func__, val);

    ctl_set_enum(M3X_CTL_FILTER, filters[val]);
}


#ifdef HAVE_SPDIF_POWER_ON
void spdif_power_on(int on)
{
    (void)on;
}

void spdif_set_mute(bool mute)
{
    (void)mute;
}
#endif
