#include <assert.h>
#include <stdlib.h>
#include "../../../rockbox/firmware/target/hosted/ibasso/pcm-ibasso.c"

struct pcm { bool alive; };
static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
static bool writing, release_write, changing;
static int closes;
void panicf(const char *fmt, ...) { (void)fmt; abort(); }
void audiohw_m3x_init(void) {}
void audiohw_m3x_unmute(void) {}
void audiohw_m3x_fallback_route(void) {}
struct pcm *pcm_open(unsigned int c, unsigned int d, unsigned int flags, struct pcm_config *config)
{
    (void)c; (void)d; (void)flags;
    assert(config->format == PCM_FORMAT_S32_LE);
    struct pcm *p = malloc(sizeof(*p)); assert(p); p->alive = true; return p;
}
int pcm_is_ready(struct pcm *p) { return p && p->alive; }
const char *pcm_get_error(struct pcm *p) { (void)p; return "test"; }
int pcm_close(struct pcm *p)
{
    pthread_mutex_lock(&gate);
    assert(!writing && p && p->alive);
    ++closes; p->alive = false; free(p);
    pthread_mutex_unlock(&gate);
    return 0;
}
int pcm_stop(struct pcm *p) { assert(p && p->alive); return 0; }
int pcm_write(struct pcm *p, const void *data, unsigned int bytes)
{
    assert(data && bytes == 32);
    pthread_mutex_lock(&gate);
    assert(p && p->alive);
    writing = true; pthread_cond_broadcast(&condition);
    while (!release_write) pthread_cond_wait(&condition, &gate);
    assert(p->alive); writing = false;
    pthread_mutex_unlock(&gate);
    return 0;
}
bool pcm_play_dma_complete_callback(enum pcm_dma_status s, const void **data, size_t *size)
{ (void)s; (void)data; (void)size; sink_dma_stop(); return false; }
enum pcm_dma_status pcm_play_dma_status_callback(enum pcm_dma_status s) { return s; }
static void *change_rate(void *unused)
{
    (void)unused;
    pthread_mutex_lock(&gate);
    changing = true; pthread_cond_broadcast(&condition);
    pthread_mutex_unlock(&gate);
    sink_set_freq(0);
    return NULL;
}
int main(void)
{
    sink_dma_init(); sink_dma_postinit();
    /* Nested core locks must recurse; playback resumes only after unlock. */
    sink_lock(); sink_lock(); sink_unlock(); sink_set_freq(1);
    const int32_t samples[8] = {0};
    sink_dma_start(samples, sizeof(samples)); sink_unlock();
    pthread_mutex_lock(&gate);
    while (!writing) pthread_cond_wait(&condition, &gate);
    int before = closes;
    pthread_mutex_unlock(&gate);
    pthread_t changer; assert(pthread_create(&changer, NULL, change_rate, NULL) == 0);
    pthread_mutex_lock(&gate);
    while (!changing) pthread_cond_wait(&condition, &gate);
    pthread_mutex_unlock(&gate);
    /* Give the competing rate change time to reach its close operation while
     * the fake hardware write remains blocked. */
    usleep(30000);
    pthread_mutex_lock(&gate);
    assert(closes == before); /* A rate change cannot close an in-flight write. */
    release_write = true; pthread_cond_broadcast(&condition);
    pthread_mutex_unlock(&gate);
    pthread_join(changer, NULL);
    sink_set_freq(HW_NUM_FREQ); /* Reject invalid table indices. */
    sink_dma_stop(); pcm_close_device();
    pthread_cancel(_pcm_thread); pthread_join(_pcm_thread, NULL);
    return 0;
}
