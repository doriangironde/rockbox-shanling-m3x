#include <assert.h>
#include <stdlib.h>
#include "../../../rockbox/firmware/export/config/shanlingm3x.h"
#define WANT_SWVOL
#if PCM_NATIVE_BITDEPTH > 16
#define WANT_SWVOL_32
#endif
#include "../../../rockbox/firmware/pcm_sw_volume.c"

static const void *submitted;
static size_t submitted_bytes;
static void submit(const void *data, size_t bytes) { submitted = data; submitted_bytes = bytes; }
static void stop(void) {}
static struct pcm_sink sink = {.ops = {.play = submit, .stop = stop}};
struct pcm_sink *pcm_get_current_sink(void) { return &sink; }
void pcm_play_lock(void) {}
void pcm_play_unlock(void) {}
void pcm_play_stop_int(void) {}
bool pcm_get_more_int(const void **data, size_t *size) { (void)data; (void)size; return false; }
enum pcm_dma_status pcm_play_call_status_cb(enum pcm_dma_status s) { return s; }
enum pcm_dma_status pcm_play_dma_status_callback(enum pcm_dma_status s)
{ return pcm_play_dma_status_callback_int_swvol(s); }

int main(void)
{
    const int16_t input[] = {32767, -32768, 12345, -12345, 1, -1, 0, 0};
    struct { int32_t before, samples[8], after; } output = {123, {0}, 456};
    pcm_set_master_volume(0, 0); pcm_sync_pcm_factors();
    pcm_sw_volume_copy_buffer(output.samples, input, sizeof(input));
    for (int i = 0; i < 8; ++i) assert(output.samples[i] == (int32_t)input[i] * 65536);
    assert(output.before == 123 && output.after == 456);
    pcm_set_master_volume(-60, -120); pcm_sync_pcm_factors();
    pcm_sw_volume_copy_buffer(output.samples, input, sizeof(input));
    /* -6 dB and -12 dB should be near half and quarter amplitude. */
    assert(output.samples[0] > 1060000000 && output.samples[0] < 1090000000);
    assert(output.samples[1] < -530000000 && output.samples[1] > -550000000);
    pcm_new_factor_l = PCM_FACTOR_UNITY / 2;
    pcm_new_factor_r = 0;
    pcm_sync_pcm_factors();
    pcm_sw_volume_copy_buffer(output.samples, input, sizeof(input));
    for (int i = 0; i < 8; i += 2) {
        assert(output.samples[i] == (int32_t)input[i] * 32768);
        assert(output.samples[i + 1] == 0);
    }
    pcm_set_master_volume(PCM_MUTE_LEVEL, PCM_MUTE_LEVEL); pcm_sync_pcm_factors();
    pcm_sw_volume_copy_buffer(output.samples, input, sizeof(input));
    for (int i = 0; i < 8; ++i) assert(output.samples[i] == 0);

    pcm_set_master_volume(0, 0);
    /* The PCM core supplies the expanded byte count to its volume adapter. */
    pcm_play_dma_start_int_swvol(input, sizeof(input) * 2);
    assert(submitted && submitted_bytes == sizeof(input) * 2);
    const int32_t *samples = submitted;
    for (int i = 0; i < 8; ++i) assert(samples[i] == (int32_t)input[i] * 65536);
    return 0;
}
