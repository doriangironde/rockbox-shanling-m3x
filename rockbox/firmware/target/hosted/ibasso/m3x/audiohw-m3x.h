/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Shared declarations for the Shanling M3X audio hardware layer.
 ****************************************************************************/

#ifndef _AUDIOHW_M3X_H_
#define _AUDIOHW_M3X_H_

/* Opens card 0's mixer. Safe to call repeatedly. */
void audiohw_m3x_mixer_open(void);

/* Closes card 0's mixer. */
void audiohw_m3x_mixer_close(void);

/*
 * Opens the mixer and brings up the AK4497 path. Called from
 * sink_dma_init() in target/hosted/ibasso/pcm-ibasso.c, because hosted targets
 * never invoke the generic audiohw_init().
 */
void audiohw_m3x_init(void);

/*
 * Undoes the Quinary MI2S / AK4497 routing after a failed pcm_open.
 */
void audiohw_m3x_fallback_route(void);

/* AK4497 "mute control" = unmute. */
void audiohw_m3x_unmute(void);

/* AK4497 "mute control" = mute. */
void audiohw_m3x_mute(void);

/* Native PCM hotplug: callback only posts to the audio thread. */
void pcm_m3x_set_output_callback(void (*callback)(void));
void pcm_m3x_switch_output(void);

#endif /* _AUDIOHW_M3X_H_ */
