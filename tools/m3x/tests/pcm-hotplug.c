/* Real target worker and route transitions, including unplug during write. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static FILE *test_fopen(const char *, const char *);
#define fopen test_fopen
#include "../../../rockbox/firmware/target/hosted/ibasso/pcm-ibasso.c"
#undef fopen

struct pcm { unsigned int card; bool ready; };
static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
static int present = -1, busy, prepare_failures, writes, notifications, closes;
static int force_write_error;
static bool writing, block_write, release_write, switching;
static int internal_inits, mutes, unrouted;
static const int16_t samples[16] = {0};
static FILE *test_fopen(const char *path, const char *mode)
{
    (void)mode;
    int card = -1; assert(sscanf(path, "/proc/asound/card%d/usbid", &card) == 1);
    if (card != __atomic_load_n(&present, __ATOMIC_RELAXED)) return NULL;
    FILE *f = tmpfile(); assert(f); fputs("05ac:110b\n", f); rewind(f); return f;
}
void panicf(const char *fmt, ...) { fprintf(stderr, "Unexpected panic: %s\n", fmt); abort(); }
void audiohw_m3x_init(void) { ++internal_inits; }
void audiohw_m3x_unmute(void) { assert(CARD == 0); }
void audiohw_m3x_mute(void) { ++mutes; }
void audiohw_m3x_fallback_route(void) { ++unrouted; }
struct pcm *pcm_open(unsigned int card, unsigned int device, unsigned int flags, struct pcm_config *c)
{
    assert(device == 0 && c->channels == 2 && c->format == PCM_FORMAT_S16_LE);
    assert(flags == (card ? PCM_OUT | PCM_NORESTART : PCM_OUT));
    if (card) assert(c->rate == 44100 && c->period_size == 480);
    struct pcm *p = malloc(sizeof(*p)); assert(p); p->card = card;
    p->ready = !card || busy-- <= 0;
    return p;
}
int pcm_is_ready(struct pcm *p) { return p && p->ready; }
int pcm_prepare(struct pcm *p)
{ assert(p && p->ready); return p->card && prepare_failures-- > 0 ? -1 : 0; }
const char *pcm_get_error(struct pcm *p) { (void)p; return "simulated USB loss/busy"; }
int pcm_close(struct pcm *p)
{ assert(p && !writing); ++closes; free(p); return 0; }
int pcm_stop(struct pcm *p) { assert(p && !writing); return 0; }
int pcm_write(struct pcm *p, const void *data, unsigned int bytes)
{
    assert(data && bytes == sizeof(samples));
    pthread_mutex_lock(&gate);
    writing = true; ++writes; pthread_cond_broadcast(&condition);
    while (block_write && !release_write) pthread_cond_wait(&condition, &gate);
    pthread_mutex_unlock(&gate);
    usleep(3000);
    int status = p->card && ((int)p->card != __atomic_load_n(&present, __ATOMIC_RELAXED) ||
                            __atomic_exchange_n(&force_write_error, 0, __ATOMIC_RELAXED)) ? -1 : 0;
    pthread_mutex_lock(&gate); writing = false; pthread_mutex_unlock(&gate);
    return status;
}
bool pcm_play_dma_complete_callback(enum pcm_dma_status s, const void **data, size_t *size)
{ (void)s; *data = samples; *size = sizeof(samples); return true; }
enum pcm_dma_status pcm_play_dma_status_callback(enum pcm_dma_status s) { return s; }
static void notify(void)
{
    pthread_mutex_lock(&gate); ++notifications; pthread_cond_broadcast(&condition); pthread_mutex_unlock(&gate);
}
static int notification_count(void)
{ pthread_mutex_lock(&gate); int n = notifications; pthread_mutex_unlock(&gate); return n; }
static void wait_for_change(int previous)
{
    struct timespec deadline; clock_gettime(CLOCK_REALTIME, &deadline); deadline.tv_sec += 3;
    pthread_mutex_lock(&gate);
    while (notifications == previous)
        assert(pthread_cond_timedwait(&condition, &gate, &deadline) == 0);
    assert(notifications == previous + 1); pthread_mutex_unlock(&gate);
}
static void select_output(int expected)
{
    sink_dma_stop(); pcm_m3x_switch_output();
    assert((int)CARD == expected && !_output_change_pending && !_output_fault);
    assert(builtin_pcm_sink.caps.num_samprs == (expected ? 1 : HW_NUM_FREQ));
    assert(builtin_pcm_sink.pending_freq == builtin_pcm_sink.caps.default_freq);
    assert(_dma_stopped == 1);
}
static void *switch_in_thread(void *unused)
{
    (void)unused;
    pthread_mutex_lock(&gate); switching = true; pthread_cond_broadcast(&condition); pthread_mutex_unlock(&gate);
    select_output(8); return NULL;
}
int main(void)
{
    sink_dma_init(); sink_dma_postinit(); pcm_m3x_set_output_callback(notify);
    assert(CARD == 0 && internal_inits == 1);
    sink_set_freq(1); assert(_config.rate == 48000);
    sink_dma_start(samples, sizeof(samples));
    int previous = notification_count();
    __atomic_store_n(&present, 7, __ATOMIC_RELAXED);
    wait_for_change(previous); select_output(7);
    assert(mutes == 1 && unrouted == 1);
    sink_dma_start(samples, sizeof(samples));
    /* A transient USB error with the identity still present reopens USB. */
    previous = notification_count();
    __atomic_store_n(&force_write_error, 1, __ATOMIC_RELAXED);
    wait_for_change(previous); select_output(7);
    sink_dma_start(samples, sizeof(samples));
    /* The USB write reports ENODEV before the periodic identity poll. */
    previous = notification_count();
    __atomic_store_n(&present, -1, __ATOMIC_RELAXED);
    wait_for_change(previous); select_output(0);
    assert(internal_inits == 2 && builtin_pcm_sink.caps.samprs == hw_freq_sampr);
    sink_set_freq(1); assert(_config.rate == 48000);
    /* Route close cannot pass an in-flight write. */
    pthread_mutex_lock(&gate); block_write = true; release_write = false; pthread_mutex_unlock(&gate);
    sink_dma_start(samples, sizeof(samples));
    pthread_mutex_lock(&gate);
    while (!writing) pthread_cond_wait(&condition, &gate);
    int before = closes; pthread_mutex_unlock(&gate);
    __atomic_store_n(&present, 8, __ATOMIC_RELAXED);
    pthread_t control; assert(pthread_create(&control, NULL, switch_in_thread, NULL) == 0);
    pthread_mutex_lock(&gate);
    while (!switching) pthread_cond_wait(&condition, &gate);
    pthread_mutex_unlock(&gate);
    usleep(30000); assert(closes == before);
    pthread_mutex_lock(&gate); release_write = true; block_write = false;
    pthread_cond_broadcast(&condition); pthread_mutex_unlock(&gate);
    pthread_join(control, NULL);
    /* Paused/menu sessions still detect removal without starting playback. */
    previous = notification_count();
    __atomic_store_n(&present, -1, __ATOMIC_RELAXED);
    wait_for_change(previous); select_output(0);
    /* Busy insertion falls back once and stays stable until reinsertion. */
    previous = notification_count(); busy = 100;
    __atomic_store_n(&present, 7, __ATOMIC_RELAXED);
    wait_for_change(previous); select_output(0);
    previous = notification_count(); usleep(350000); assert(notification_count() == previous);
    __atomic_store_n(&present, -1, __ATOMIC_RELAXED); usleep(350000);
    busy = 0; prepare_failures = 2;
    __atomic_store_n(&present, 7, __ATOMIC_RELAXED);
    wait_for_change(previous); select_output(7);
    /* A rapid reinsert changes the current card; stale events use latest state. */
    previous = notification_count(); __atomic_store_n(&present, 9, __ATOMIC_RELAXED);
    wait_for_change(previous); select_output(9);
    pcm_m3x_set_output_callback(NULL); pcm_close_device();
    pthread_cancel(_pcm_thread); pthread_join(_pcm_thread, NULL);
    puts("PASS: live/paused swaps, removed PCM, busy and prepare recovery, repeated cards, write serialization");
    return 0;
}
