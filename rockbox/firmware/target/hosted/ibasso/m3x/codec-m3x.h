/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Audio hardware settings for the Shanling M3X hosted port.
 ****************************************************************************/

#ifndef _CODEC_M3X_H_
#define _CODEC_M3X_H_

#define AUDIOHW_CAPS FILTER_ROLL_OFF_CAP

/*
 * Software attenuation supplies the dB scale and independent channel balance.
 * The vendor's uncalibrated hardware volume is held at the captured OEM level.
 * Values displayed here are whole dB; sound.c passes centibels to the driver.
 */
AUDIOHW_SETTING(VOLUME, "dB", 0, 1, -128, 0, -30)

/*
 * "AK4497 filter control" offers:
 *   sharp, slow, delay-sharp, delay-slow, supp-slow, delay-sslow
 * exposed in the order used by audiohw_set_filter_roll_off().
 */
AUDIOHW_SETTING(FILTER_ROLL_OFF, "", 0, 1, 0, 5, 0)

#endif /* _CODEC_M3X_H_ */
