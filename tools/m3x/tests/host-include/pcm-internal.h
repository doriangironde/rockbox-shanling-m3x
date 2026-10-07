#define PCM_FACTOR_UNITY (1u << PCM_SW_VOLUME_FRACBITS)
#define PCM_FACTOR_MAX 0x10000u
#define PCM_PLAY_DBL_BUF_SAMPLES 1024
#define PCM_DBL_BUF_BSS
void pcm_sync_pcm_factors(void);
enum pcm_dma_status pcm_play_call_status_cb(enum pcm_dma_status);
void pcm_play_stop_int(void);
bool pcm_get_more_int(const void **, size_t *);
#include "pcm_sink.h"
struct pcm_sink *pcm_get_current_sink(void);
bool pcm_play_dma_complete_callback(enum pcm_dma_status, const void **, size_t *);
