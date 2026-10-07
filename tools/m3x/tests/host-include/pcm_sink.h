#ifndef M3X_TEST_PCM_SINK_H
#define M3X_TEST_PCM_SINK_H
struct pcm_sink {
    struct { const unsigned int *samprs; unsigned int num_samprs, default_freq; int volume_type; } caps;
    struct {
        void (*init)(void), (*postinit)(void);
        void (*set_freq)(uint16_t);
        void (*lock)(void), (*unlock)(void);
        void (*play)(const void *, size_t);
        void (*stop)(void);
    } ops;
};
#endif
