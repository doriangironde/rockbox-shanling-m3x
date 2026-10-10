/* Bounded, quiet USB PCM diagnostic. Only Apple USB-C EarPods 05ac:110b.
 * No mixer writes, internal DAC routing, service changes or firmware changes.
 * Build with bundled tinyalsa pcm.c/mixer.c, -fPIE -pie and -lm. */
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include "tinyalsa/asoundlib.h"

static void deadline(int signal_number)
{
    (void)signal_number;
    static const char message[] = "PROBE_TIMEOUT\n";
    write(STDOUT_FILENO, message, sizeof(message) - 1);
    _exit(124);
}

static int find_earpods(void)
{
    for (int card = 1; card < 32; ++card)
    {
        char path[96], id[64];
        snprintf(path, sizeof(path), "/proc/asound/card%d/usbid", card);
        FILE *file = fopen(path, "r");
        if (!file) continue;
        int found = fgets(id, sizeof(id), file) &&
                    strncmp(id, "05ac:110b", 9) == 0 &&
                    (id[9] == '\n' || id[9] == '\0');
        fclose(file);
        if (found) return card;
    }
    return -1;
}

static void print_mixer(unsigned int card)
{
    struct mixer *mixer = mixer_open(card);
    if (!mixer) { puts("USB mixer unavailable"); return; }
    printf("Mixer: %s\n", mixer_get_name(mixer));
    for (unsigned int i = 0; i < mixer_get_num_ctls(mixer); ++i)
    {
        struct mixer_ctl *ctl = mixer_get_ctl(mixer, i);
        if (!ctl) continue;
        printf("Control: %s type=%d", mixer_ctl_get_name(ctl), mixer_ctl_get_type(ctl));
        for (unsigned int v = 0; v < mixer_ctl_get_num_values(ctl); ++v)
            printf(" value[%u]=%d", v, mixer_ctl_get_value(ctl, v));
        puts("");
    }
    mixer_close(mixer);
}

static int play(unsigned int card, unsigned int rate)
{
    struct pcm_config config;
    memset(&config, 0, sizeof(config));
    config.channels = 2;
    config.rate = rate;
    config.period_size = 480;
    config.period_count = 4;
    config.format = PCM_FORMAT_S16_LE;
    config.start_threshold = 480;
    config.stop_threshold = 480 * 4;
    printf("OPEN card=%u device=0 rate=%u channels=2 format=S16_LE\n", card, rate);
    struct pcm *pcm = pcm_open(card, 0, PCM_OUT | PCM_NORESTART, &config);
    if (!pcm || !pcm_is_ready(pcm))
    {
        printf("OPEN_FAILED: %s\n", pcm ? pcm_get_error(pcm) : "no PCM handle");
        if (pcm) pcm_close(pcm);
        return 1;
    }
    if (pcm_prepare(pcm))
    {
        printf("PREPARE_FAILED: %s\n", pcm_get_error(pcm));
        pcm_close(pcm);
        return 1;
    }
    puts("PREPARE_OK");
    int16_t buffer[480 * 2];
    unsigned int total = rate * 3;
    for (unsigned int position = 0; position < total; position += 480)
    {
        unsigned int frames = total - position < 480 ? total - position : 480;
        for (unsigned int frame = 0; frame < frames; ++frame)
        {
            unsigned int sample = position + frame;
            /* 440 Hz at -50 dBFS, with a 50 ms fade at either end. */
            double fade = 1.0;
            if (sample < rate / 20) fade = (double)sample / (rate / 20);
            if (total - sample < rate / 20) fade = (double)(total - sample) / (rate / 20);
            int16_t value = (int16_t)(103.0 * fade * sin(2.0 * 3.141592653589793 * 440 * sample / rate));
            buffer[frame * 2] = buffer[frame * 2 + 1] = value;
        }
        if (pcm_write(pcm, buffer, frames * 2 * sizeof(int16_t)))
        {
            printf("WRITE_FAILED frame=%u: %s\n", position, pcm_get_error(pcm));
            pcm_close(pcm);
            return 1;
        }
    }
    /* Let the last period play before dropping and closing the stream. */
    usleep(100000);
    pcm_close(pcm);
    printf("STREAM_OK rate=%u frames=%u\n", rate, total);
    return 0;
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGALRM, deadline);
    alarm(120);
    puts("Waiting up to 90 seconds for Apple EarPods (05ac:110b). Stop Android playback.");
    int card = -1;
    for (int second = 0; second < 90 && card < 0; ++second)
    {
        card = find_earpods();
        if (card < 0) sleep(1);
    }
    if (card < 0) { puts("NO_EARPODS"); return 2; }
    printf("EARPODS_FOUND card=%d\n", card);
    /* Give enumeration and Android's connection notifications time to settle. */
    sleep(5);
    if (find_earpods() != card) { puts("EARPODS_REMOVED"); return 2; }
    print_mixer(card);
    int status = play(card, 48000);
    sleep(1);
    if (find_earpods() != card) { puts("EARPODS_REMOVED"); return 2; }
    status |= play(card, 44100);
    printf("PROBE_DONE status=%d\n", status);
    return status;
}
