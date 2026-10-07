/* m3xtest: tinyalsa mixer + PCM inspector / tone generator for the Shanling M3X.
 *
 * Built against Rockbox' bundled tinyalsa
 * (firmware/target/hosted/tinyalsa), which has a reduced API:
 *   enum pcm_format { S16_LE=0, S32_LE=1, S8=2, S24_LE=3 }
 *   struct pcm_config { channels, rate, period_size, period_count, format,
 *                       start_threshold, stop_threshold, silence_threshold }
 *
 * Modes:
 *   mixer                     list every ALSA control on card0
 *   mixer <name>              show one control (values + range)
 *   set <name> <value>        set a control (integer, or enum string)
 *   setpct <name> <0-100>     set a control by percent
 *   caps <dev>                PCM capabilities
 *   tone <dev> [hz] [s] [rate] [ch] [bits] [fmt]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <stdbool.h>

#include "tinyalsa/asoundlib.h"

static const char *ctype_name(enum mixer_ctl_type t)
{
    switch (t) {
    case MIXER_CTL_TYPE_BOOL:   return "BOOL";
    case MIXER_CTL_TYPE_INT:    return "INT";
    case MIXER_CTL_TYPE_ENUM:   return "ENUM";
    case MIXER_CTL_TYPE_BYTE:   return "BYTE";
    case MIXER_CTL_TYPE_IEC958: return "IEC958";
    case MIXER_CTL_TYPE_INT64:  return "INT64";
    default:                    return "UNKNOWN";
    }
}

static int list_mixer(void)
{
    struct mixer *m = mixer_open(0);
    unsigned int n, i;
    if (!m) { fprintf(stderr, "mixer_open(0) failed\n"); return 1; }
    n = mixer_get_num_ctls(m);
    printf("card0 mixer '%s': %u controls\n\n", mixer_get_name(m), n);
    for (i = 0; i < n; i++) {
        struct mixer_ctl *c = mixer_get_ctl(m, i);
        const char *name;
        enum mixer_ctl_type t;
        unsigned int nv, ne, v, e;
        if (!c) continue;
        name = mixer_ctl_get_name(c);
        t    = mixer_ctl_get_type(c);
        nv   = mixer_ctl_get_num_values(c);
        ne   = mixer_ctl_get_num_enums(c);
        printf("[%3u] %-40s %-8s nv=%u", i, name, ctype_name(t), nv);
        if (t == MIXER_CTL_TYPE_INT)
            printf(" range=[%d..%d]", mixer_ctl_get_range_min(c), mixer_ctl_get_range_max(c));
        if (ne > 1) {
            printf(" enums={");
            for (e = 0; e < ne; e++)
                printf("%s%s", e ? "," : "", mixer_ctl_get_enum_string(c, e));
            printf("}");
        }
        printf("\n");
        for (v = 0; v < nv; v++) {
            int val = mixer_ctl_get_value(c, v);
            if (val < 0) printf("       [%u] = <error>\n", v);
            else         printf("       [%u] = %d (%d%%)\n", v, val, mixer_ctl_get_percent(c, v));
        }
    }
    mixer_close(m);
    return 0;
}

static int show_ctl(const char *name)
{
    struct mixer *m = mixer_open(0);
    struct mixer_ctl *c;
    unsigned int nv, v;
    if (!m) { fprintf(stderr, "mixer_open(0) failed\n"); return 1; }
    c = mixer_get_ctl_by_name(m, name);
    if (!c) { fprintf(stderr, "no such control: %s\n", name); return 1; }
    nv = mixer_ctl_get_num_values(c);
    printf("%s: type=%s nv=%u", name, ctype_name(mixer_ctl_get_type(c)), nv);
    if (mixer_ctl_get_type(c) == MIXER_CTL_TYPE_INT)
        printf(" range=[%d..%d]", mixer_ctl_get_range_min(c), mixer_ctl_get_range_max(c));
    printf("\n");
    for (v = 0; v < nv; v++)
        printf("  [%u] = %d (%d%%)\n", v, mixer_ctl_get_value(c, v), mixer_ctl_get_percent(c, v));
    mixer_close(m);
    return 0;
}

static int set_ctl(const char *name, const char *val, int use_pct)
{
    struct mixer *m = mixer_open(0);
    struct mixer_ctl *c;
    int rc;
    if (!m) { fprintf(stderr, "mixer_open(0) failed\n"); return 1; }
    c = mixer_get_ctl_by_name(m, name);
    if (!c) { fprintf(stderr, "no such control: %s\n", name); return 1; }
    if (use_pct) rc = mixer_ctl_set_percent(c, 0, atoi(val));
    else if (mixer_ctl_get_type(c) == MIXER_CTL_TYPE_ENUM)
        rc = mixer_ctl_set_enum_by_string(c, val);
    else rc = mixer_ctl_set_value(c, 0, atoi(val));
    if (rc < 0) { fprintf(stderr, "set failed: %s = %s\n", name, val); return 1; }
    printf("OK %s = %s -> now %d (%d%%)\n", name, val,
           mixer_ctl_get_value(c, 0), mixer_ctl_get_percent(c, 0));
    mixer_close(m);
    return 0;
}

static int sweep_ctl(const char *name, unsigned lo, unsigned hi)
{
    struct mixer *m = mixer_open(0);
    struct mixer_ctl *c;
    unsigned v;
    if (!m) { fprintf(stderr, "mixer_open(0) failed\n"); return 1; }
    c = mixer_get_ctl_by_name(m, name);
    if (!c) { fprintf(stderr, "no such control: %s\n", name); return 1; }
    printf("# %s type=%s range=[%d..%d]\n", name,
           ctype_name(mixer_ctl_get_type(c)),
           mixer_ctl_get_range_min(c), mixer_ctl_get_range_max(c));
    printf("# sent,applied\n");
    for (v = lo; v <= hi; v++) {
        if (mixer_ctl_set_value(c, 0, (int)v) < 0) { printf("%u,ERR\n", v); continue; }
        printf("%u,%d\n", v, mixer_ctl_get_value(c, 0));
    }
    mixer_close(m);
    return 0;
}

static int show_caps(unsigned int dev)
{
    struct pcm_params *p = pcm_params_get(0, dev, 0);
    if (!p) { fprintf(stderr, "pcm_params_get(0,%u) failed\n", dev); return 1; }
    printf("pcm card0 dev%u:\n", dev);
    printf("  rate      : %u .. %u Hz\n", pcm_params_get_min(p, PCM_PARAM_RATE),
                                       pcm_params_get_max(p, PCM_PARAM_RATE));
    printf("  channels  : %u .. %u\n",     pcm_params_get_min(p, PCM_PARAM_CHANNELS),
                                       pcm_params_get_max(p, PCM_PARAM_CHANNELS));
    printf("  period    : %u .. %u us\n",  pcm_params_get_min(p, PCM_PARAM_PERIOD_TIME),
                                       pcm_params_get_max(p, PCM_PARAM_PERIOD_TIME));
    printf("  buffer    : %u .. %u us\n",  pcm_params_get_min(p, PCM_PARAM_BUFFER_TIME),
                                       pcm_params_get_max(p, PCM_PARAM_BUFFER_TIME));
    pcm_params_free(p);
    return 0;
}

/* Single open attempt, printing the driver's error. Silent. */
static int try_open(unsigned dev, unsigned rate, unsigned ch, unsigned fmt,
                    unsigned period, unsigned count)
{
    struct pcm_config cfg;
    struct pcm *p;

    memset(&cfg, 0, sizeof(cfg));
    cfg.channels     = ch;
    cfg.rate         = rate;
    cfg.period_size  = period;
    cfg.period_count = count;
    cfg.format       = (enum pcm_format)fmt;

    printf("card0 dev%u: calling pcm_open(rate=%u ch=%u fmt=%u period=%u count=%u)...\n",
           dev, rate, ch, fmt, period, count);
    fflush(stdout);
    p = pcm_open(0, dev, PCM_OUT, &cfg);
    printf("card0 dev%u: pcm_open() RETURNED (%s)\n", dev, p ? "non-NULL" : "NULL");
    fflush(stdout);
    if (p && pcm_is_ready(p)) {
        printf("card0 dev%u OK: rate=%u ch=%u fmt=%u period=%u count=%u -> buffer=%u frames (%u bytes)\n",
               dev, rate, ch, fmt, period, count,
               pcm_get_buffer_size(p), pcm_frames_to_bytes(p, pcm_get_buffer_size(p)));
        pcm_close(p);
        return 0;
    }
    printf("card0 dev%u FAILED (rate=%u ch=%u fmt=%u period=%u count=%u): %s\n",
           dev, rate, ch, fmt, period, count, p ? pcm_get_error(p) : "pcm_open() returned NULL");
    if (p) pcm_close(p);
    return 1;
}

/* Open/close only - produces no sound. Reports which period/period_count
 * combinations the M3X PCM will accept for a given format. */
static int probe_cfgs(unsigned dev, unsigned rate, unsigned ch, unsigned fmt,
                      unsigned bits)
{
    static const unsigned int periods[]  = { 64, 128, 256, 512, 1024, 2048 };
    static const unsigned int pcounts[]  = { 2, 3, 4, 6, 8 };
    struct pcm_config cfg;
    unsigned i, j, ok = 0, total = 0;

    printf("probing card0 dev%u: %u ch, %u Hz, fmt=%u (%u bit)\n",
           dev, ch, rate, fmt, bits);
    printf("%-8s", "period");
    for (j = 0; j < sizeof(pcounts) / sizeof(pcounts[0]); j++)
        printf(" count=%-3u", pcounts[j]);
    printf("\n");

    for (i = 0; i < sizeof(periods) / sizeof(periods[0]); i++) {
        printf("%-8u", periods[i]);
        for (j = 0; j < sizeof(pcounts) / sizeof(pcounts[0]); j++) {
            struct pcm *p;
            memset(&cfg, 0, sizeof(cfg));
            cfg.channels     = ch;
            cfg.rate         = rate;
            cfg.period_size  = periods[i];
            cfg.period_count = pcounts[j];
            cfg.format       = (enum pcm_format)fmt;
            total++;
            p = pcm_open(0, dev, PCM_OUT, &cfg);
            if (p && pcm_is_ready(p)) {
                unsigned buf = pcm_get_buffer_size(p);
                printf("   %-7u", buf);
                pcm_close(p);
                ok++;
            } else {
                printf("   %-7s", "FAIL");
                if (p) pcm_close(p);
            }
        }
        printf("\n");
        fflush(stdout);
    }
    printf("-> %u/%u accepted\n", ok, total);
    return 0;
}

static int play_tone(unsigned dev, double hz, double secs, unsigned rate,
                     unsigned ch, unsigned bits, unsigned fmt)
{
    struct pcm_config cfg;
    struct pcm *p;
    unsigned int frames_per_buf, bytes_per_frame, buf_bytes, chunk;
    unsigned int cycle, i, c;
    unsigned char *buf;
    unsigned long total, done = 0;
    int rc = 0;

    memset(&cfg, 0, sizeof(cfg));
    cfg.channels       = ch;
    cfg.rate           = rate;
    cfg.period_size    = 1024;
    cfg.period_count   = 4;
    cfg.format         = (enum pcm_format)fmt;
    cfg.start_threshold = 4096;
    cfg.stop_threshold  = 4096;

    p = pcm_open(0, dev, PCM_OUT, &cfg);
    if (!p || !pcm_is_ready(p)) {
        fprintf(stderr, "pcm_open(0,%u) FAILED: %s\n", dev, p ? pcm_get_error(p) : "alloc");
        if (p) pcm_close(p);
        return 1;
    }
    frames_per_buf = pcm_get_buffer_size(p);
    bytes_per_frame = ch * (bits / 8);
    buf_bytes = frames_per_buf * bytes_per_frame;
    printf("opened card0 dev%u: %u ch, %u Hz, %u bits, fmt=%u, buffer=%u frames\n",
           dev, ch, rate, bits, fmt, frames_per_buf);

    cycle = (unsigned int)(rate / hz);
    if (cycle < 2) cycle = 2;

    buf = calloc(1, buf_bytes);
    if (!buf) { pcm_close(p); return 1; }
    for (i = 0; i < cycle; i++) {
        double s = sin(2.0 * M_PI * (double)i / (double)cycle) * 0.35;
        for (c = 0; c < ch; c++) {
            int v = (int)(s * (c == 0 ? 1.0 : 0.7) * 2147483647.0);
            if (bits == 16) {
                short sv = (short)v;
                memcpy(buf + (i * ch + c) * 2, &sv, 2);
            } else {
                int iv = v;
                memcpy(buf + (i * ch + c) * 4, &iv, 4);
            }
        }
    }

    chunk = (cycle < frames_per_buf) ? cycle : frames_per_buf;
    total = (unsigned long)((double)rate * (double)ch * (double)(bits / 8) * secs);
    printf("playing %.1f s of %.0f Hz (%.2f s per loop) ...\n",
           secs, hz, (double)cycle / (double)rate);
    fflush(stdout);

    while (done < total) {
        unsigned int n = chunk;
        if (done + (unsigned long)n * bytes_per_frame > total)
            n = (unsigned int)((total - done) / bytes_per_frame);
        if (n == 0) break;
        if (pcm_write(p, buf + (done % (unsigned long)buf_bytes), n * bytes_per_frame) < 0) {
            fprintf(stderr, "write error after %lu bytes: %s\n", done, pcm_get_error(p));
            rc = 1;
            break;
        }
        done += (unsigned long)n * bytes_per_frame;
    }
    printf("wrote %lu / %lu bytes (rc=%d)\n", done, total, rc);
    free(buf);
    pcm_close(p);
    return rc;
}

/*
    Set the whole AK4497 route and then open the PCM, all inside one process -
    the same thing Rockbox's sink_dma_init() does. Doing it in a single process
    matters: the Quinary MI2S route only comes up when the mixer controls are
    applied back to back. Running the same steps as separate `m3xtest set`
    invocations (each of which opens and closes controlC0) can leave the driver
    in a state where pcm_open() then fails with EINVAL, so the control path and
    the PCM open have to share one process to reproduce the real behaviour.

    silent: opens and closes the PCM without ever writing samples.
*/
static int try_route(unsigned dev, unsigned rate, unsigned ch, unsigned fmt,
                     unsigned period, unsigned count, bool slow)
{
    static const struct { const char *name; const char *val; int is_int; } steps[] = {
        { "Output Mux",                            "HiFi 2V Mode",      0 },
        { "AK4497 polo dac number",                "two",               0 },
        { "AK4497 POLO mode control",              "pcm",               0 },
        { "AK4497 filter control",                 "slow",              0 },
        { "AK4497 gain control",                   "high",              0 },
        { "QUIN MI2S RX Format",                   "LPCM",              0 },
        { "MI2S_RX Channels",                      "Two",               0 },
        { "QUIN_MI2S_RX Audio Mixer MultiMedia1",   NULL,                1 },
        { "AK4497 Format",                         "S32_LE",            0 },
        { "Bit mode control",                      "16",                0 },
        { "AK4497 mute control",                   "unmute",            0 },
    };

    struct mixer *m;
    struct pcm_config cfg;
    struct pcm *p;
    unsigned i;
    unsigned skip_max = 0;
    bool skip[16] = { false };
    const char *sk = getenv("M3X_SKIP");

    /* M3X_SKIP="0,7,9" leaves out the listed step indices, so the exact step
     * that poisons the Quinary route can be found by bisection. */
    if (sk) {
        const char *p2 = sk;
        while (*p2 && skip_max < 16) {
            if (*p2 >= '0' && *p2 <= '9') {
                unsigned idx = (unsigned)(*p2 - '0');
                if (idx < 16) skip[idx] = true;
                if (skip_max < idx + 1) skip_max = idx + 1;
            }
            p2++;
        }
    }
    if (skip_max > 16) skip_max = 16;

    m = mixer_open(0);
    if (!m) {
        printf("route: mixer_open(0) failed\n");
        return 1;
    }

    for (i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
        struct mixer_ctl *c;
        int rc;

        if (slow)
            usleep(100000);

        if (i < skip_max && skip[i])
            continue;

        c = mixer_get_ctl_by_name(m, steps[i].name);

        if (!c) {
            printf("route: step %2u MISSING control '%s'\n", i, steps[i].name);
            continue;
        }
        rc = steps[i].is_int ? mixer_ctl_set_value(c, 0, 1)
                             : mixer_ctl_set_enum_by_string(c, steps[i].val);
        printf("route: step %2u %-40s -> %s\n", i, steps[i].name,
               rc < 0 ? "FAILED" : "ok");
    }

    /* Rockbox keeps its mixer fd open across pcm_open(); so do we. */
    memset(&cfg, 0, sizeof(cfg));
    cfg.channels     = ch;
    cfg.rate         = rate;
    cfg.period_size  = period;
    cfg.period_count = count;
    cfg.format       = (enum pcm_format)fmt;

    printf("route: pcm_open(rate=%u ch=%u fmt=%u period=%u count=%u) ...\n",
           rate, ch, fmt, period, count);
    fflush(stdout);

    p = pcm_open(dev < 10 ? 0 : dev, dev, PCM_OUT, &cfg);

    if (p && pcm_is_ready(p)) {
        printf("route: OK -> buffer=%u frames (%u bytes)\n",
               pcm_get_buffer_size(p), pcm_frames_to_bytes(p, pcm_get_buffer_size(p)));
        pcm_close(p);
        mixer_close(m);
        return 0;
    }

    printf("route: FAILED: %s\n",
           p ? pcm_get_error(p) : "pcm_open() returned NULL");
    if (p) pcm_close(p);
    mixer_close(m);
    return 1;
}


int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr,
            "usage:\n"
            "  m3xtest mixer                     list all card0 controls\n"
            "  m3xtest mixer <name>              show one control\n"
            "  m3xtest set <name> <value>        set control (int, or enum string)\n"
            "  m3xtest setpct <name> <0-100>     set control by percent\n"
            "  m3xtest sweep <name> [lo] [hi]     map sent->applied values\n"
            "  m3xtest caps <dev>                PCM capabilities\n"
            "  m3xtest open <dev> [rate] [ch] [fmt] [period] [count]  single silent open\n"
            "  m3xtest probe <dev> [rate] [ch] [fmt] [bits]   try period/count matrix (silent)\n"
            "  m3xtest route [dev] [rate] [ch] [fmt] [period] [count]\n"
            "        set the whole AK4497 route AND open the PCM in one process,\n"
            "        exactly like Rockbox does. Prints per-step timing, since the\n"
            "        route only comes up reliably when the controls are set fast.\n"
            "  m3xtest route2 ...     same, but deliberately slow (100ms between steps)\n"
            "  m3xtest tone <dev> [hz] [s] [rate] [ch] [bits] [fmt]\n"
            "        fmt: 0=S16_LE 1=S32_LE 3=S24_LE (default 1)\n");
        return 1;
    }
    if (!strcmp(argv[1], "mixer"))  return (argc > 2) ? show_ctl(argv[2]) : list_mixer();
    if (!strcmp(argv[1], "set"))    return set_ctl(argv[2], argv[3], 0);
    if (!strcmp(argv[1], "setpct")) return set_ctl(argv[2], argv[3], 1);
    if (!strcmp(argv[1], "sweep"))
        return sweep_ctl(argv[2], (argc > 3) ? (unsigned)atoi(argv[3]) : 0u,
                                  (argc > 4) ? (unsigned)atoi(argv[4]) : 255u);
    if (!strcmp(argv[1], "caps"))   return show_caps((unsigned)atoi(argv[2]));
    if (!strcmp(argv[1], "open")) {
        unsigned dev    = (argc > 2) ? (unsigned)atoi(argv[2]) : 0;
        unsigned rate   = (argc > 3) ? (unsigned)atoi(argv[3]) : 44100;
        unsigned ch     = (argc > 4) ? (unsigned)atoi(argv[4]) : 2;
        unsigned fmt    = (argc > 5) ? (unsigned)atoi(argv[5]) : 1;
        unsigned period = (argc > 6) ? (unsigned)atoi(argv[6]) : 1024;
        unsigned count  = (argc > 7) ? (unsigned)atoi(argv[7]) : 4;
        return try_open(dev, rate, ch, fmt, period, count);
    }
    if (!strcmp(argv[1], "route") || !strcmp(argv[1], "route2")) {
        unsigned dev    = (argc > 2) ? (unsigned)atoi(argv[2]) : 0;
        unsigned rate   = (argc > 3) ? (unsigned)atoi(argv[3]) : 44100;
        unsigned ch     = (argc > 4) ? (unsigned)atoi(argv[4]) : 2;
        unsigned fmt    = (argc > 5) ? (unsigned)atoi(argv[5]) : 1;
        unsigned period = (argc > 6) ? (unsigned)atoi(argv[6]) : 256;
        unsigned count  = (argc > 7) ? (unsigned)atoi(argv[7]) : 4;
        return try_route(dev, rate, ch, fmt, period, count,
                         !strcmp(argv[1], "route2"));
    }
    if (!strcmp(argv[1], "probe")) {
        unsigned dev  = (argc > 2) ? (unsigned)atoi(argv[2]) : 0;
        unsigned rate = (argc > 3) ? (unsigned)atoi(argv[3]) : 44100;
        unsigned ch   = (argc > 4) ? (unsigned)atoi(argv[4]) : 2;
        unsigned fmt  = (argc > 5) ? (unsigned)atoi(argv[5]) : 1; /* S32_LE */
        unsigned bits = (argc > 6) ? (unsigned)atoi(argv[6]) : 32;
        return probe_cfgs(dev, rate, ch, fmt, bits);
    }
    if (!strcmp(argv[1], "tone")) {
        unsigned dev  = (argc > 2) ? (unsigned)atoi(argv[2]) : 0;
        double  hz    = (argc > 3) ? atof(argv[3]) : 440.0;
        double  secs  = (argc > 4) ? atof(argv[4]) : 3.0;
        unsigned rate  = (argc > 5) ? (unsigned)atoi(argv[5]) : 44100;
        unsigned ch    = (argc > 6) ? (unsigned)atoi(argv[6]) : 2;
        unsigned bits  = (argc > 7) ? (unsigned)atoi(argv[7]) : 32;
        unsigned fmt   = (argc > 8) ? (unsigned)atoi(argv[8]) : 1;
        return play_tone(dev, hz, secs, rate, ch, bits, fmt);
    }
    fprintf(stderr, "unknown mode: %s\n", argv[1]);
    return 1;
}