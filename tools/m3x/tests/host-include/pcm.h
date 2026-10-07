#include <stddef.h>
#include <stdbool.h>
enum pcm_dma_status { PCM_DMAST_ERR_DMA = -1, PCM_DMAST_OK = 0, PCM_DMAST_STARTED = 1 };
#define PCM_SAMPLE_SIZE 4
void pcm_play_lock(void);
void pcm_play_unlock(void);
enum pcm_dma_status pcm_play_dma_status_callback(enum pcm_dma_status);
