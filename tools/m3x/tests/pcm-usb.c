/* Exercise the real sink with an enumerated USB card and fake ALSA ownership. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static FILE *test_fopen(const char *path, const char *mode);
#ifdef M3X_TEST_USB_ENUM_DELAY
static unsigned int enumeration_reads;
#endif
#define fopen test_fopen
#include "../../../rockbox/firmware/target/hosted/ibasso/pcm-ibasso.c"
#undef fopen

struct pcm { bool alive; };
static unsigned int opens, closes, internal_init, internal_unmute, prepares;
static FILE *test_fopen(const char *path, const char *mode)
{
    assert(!strcmp(mode, "r"));
    int card = -1;
    assert(sscanf(path, "/proc/asound/card%d/usbid", &card) == 1 && card > 0);
    if (card != 7) return NULL;
#ifdef M3X_TEST_USB_ENUM_DELAY
    /* The real handover removed card 1 for ~400 ms at PCM initialization. */
    if (++enumeration_reads <= 4) return NULL;
#endif
#ifdef M3X_TEST_USB_REMOVAL
    if (opens) return NULL;
#endif
    FILE *file = tmpfile(); assert(file);
#ifdef M3X_TEST_OTHER_USB
    fputs("05ac:110b0\n", file); /* Prefix matches must not select another device. */
#else
    fputs("05ac:110b\n", file);
#endif
    rewind(file);
    return file;
}
void audiohw_m3x_init(void) { ++internal_init; }
void audiohw_m3x_unmute(void) { ++internal_unmute; }
void audiohw_m3x_mute(void) {}
void audiohw_m3x_fallback_route(void) { assert(CARD == 0); }
void panicf(const char *fmt, ...)
{ (void)fmt; abort(); }
struct pcm *pcm_open(unsigned int card, unsigned int device, unsigned int flags, struct pcm_config *config)
{
    assert(device == 0 && config->format == PCM_FORMAT_S16_LE && config->channels == 2);
    if (card == 0)
        assert(flags == PCM_OUT && internal_init == 1);
    else
    {
    assert(card == 7 && flags == (PCM_OUT | PCM_NORESTART) && internal_init == 0);
    assert(config->rate == 44100 && config->period_size == 480 && config->period_count == 4);
    assert(config->start_threshold == 480 && config->stop_threshold == 1920);
    }
    ++opens;
    struct pcm *p = malloc(sizeof(*p)); assert(p);
#if defined(M3X_TEST_USB_BUSY) || defined(M3X_TEST_USB_REMOVAL)
    p->alive = card == 0;
#elif defined(M3X_TEST_OTHER_USB)
    p->alive = true;
#else
    p->alive = opens > 2; /* Two busy opens followed by release from Android. */
#endif
    return p;
}
int pcm_is_ready(struct pcm *p) { return p && p->alive; }
int pcm_prepare(struct pcm *p) { assert(p && p->alive); ++prepares; return 0; }
const char *pcm_get_error(struct pcm *p) { (void)p; return "Device or resource busy"; }
int pcm_close(struct pcm *p) { assert(p); ++closes; free(p); return 0; }
int pcm_stop(struct pcm *p) { assert(p && p->alive); return 0; }
int pcm_write(struct pcm *p, const void *buffer, unsigned int bytes)
{
    assert(p && p->alive && buffer && bytes == 32);
    return -1;
}
bool pcm_play_dma_complete_callback(enum pcm_dma_status s, const void **data, size_t *size)
{ (void)s; (void)data; (void)size; return false; }
enum pcm_dma_status pcm_play_dma_status_callback(enum pcm_dma_status s) { return s; }
int main(void)
{
    builtin_pcm_sink.pending_freq = 1234;
    sink_dma_init(); sink_dma_postinit();
#if defined(M3X_TEST_OTHER_USB) || defined(M3X_TEST_USB_BUSY) || defined(M3X_TEST_USB_REMOVAL)
    assert(CARD == 0 && internal_init == 1);
    assert(builtin_pcm_sink.caps.num_samprs == HW_NUM_FREQ);
    assert(builtin_pcm_sink.caps.samprs == hw_freq_sampr);
#else
    assert(opens == 3 && closes == 2 && prepares == 1);
    assert(builtin_pcm_sink.caps.num_samprs == 1 && builtin_pcm_sink.caps.default_freq == 0);
    assert(builtin_pcm_sink.pending_freq == 0 && builtin_pcm_sink.caps.samprs[0] == 44100);
    sink_set_freq(1); sink_set_freq(0); sink_set_freq(65535);
    assert(opens == 3 && _config.rate == 44100); /* USB cannot select an internal rate. */
#endif
    sink_dma_stop(); pcm_close_device();
    pthread_cancel(_pcm_thread); pthread_join(_pcm_thread, NULL);
    assert(internal_unmute == 0);
    return 0;
}
