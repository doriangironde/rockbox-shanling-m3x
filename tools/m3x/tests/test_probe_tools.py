"""Run diagnostic code with fake framebuffer/PCM I/O, never real hardware.

M3X_PROBE_SOURCE_DIR selects preserved before-sources for regression proof.
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
SOURCES = Path(os.environ.get("M3X_PROBE_SOURCE_DIR", ROOT / "tools/m3x"))
FB_HEADER = r"""
#ifndef PROBE_FB_H
#define PROBE_FB_H
struct fb_bitfield { unsigned offset, length, msb_right; };
struct fb_var_screeninfo {
    unsigned xres, yres, xres_virtual, yres_virtual, xoffset, yoffset;
    unsigned bits_per_pixel, activate;
    struct fb_bitfield red, green, blue, transp;
};
struct fb_fix_screeninfo { unsigned line_length, smem_len; char id[16]; };
#define FBIOGET_VSCREENINFO 0x4600
#define FBIOGET_FSCREENINFO 0x4602
#define FBIOPAN_DISPLAY 0x4606
#define FBIOBLANK 0x4611
#define FB_ACTIVATE_NOW 0
#endif
"""


class ProbeToolsTests(unittest.TestCase):
    def compile_and_run(self, source, harness, arguments=()):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            (base / "linux").mkdir()
            (base / "linux/fb.h").write_text(FB_HEADER)
            wrapper = base / "probe.c"
            wrapper.write_text(harness.replace("@SOURCE@", str(SOURCES / source)))
            output = base / "probe"
            dead_code = "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"
            subprocess.run(["cc", "-std=gnu11", "-O1", "-g", "-ffunction-sections",
                            "-fdata-sections", "-fsanitize=address,undefined",
                            "-fno-sanitize-recover=all", dead_code, "-I" + str(base),
                            "-I" + str(ROOT / "rockbox/firmware/target/hosted/tinyalsa/include"),
                            str(wrapper), "-lm", "-o", str(output)], check=True)
            input_file, capture = base / "framebuffer", base / "capture"
            input_file.write_bytes(bytes(range(16)))
            result = subprocess.run([str(output), str(input_file), str(capture), *arguments],
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_pan_uses_requested_page_rgb_order_and_stops_after_failed_fill(self):
        self.compile_and_run("fbpan.c", r"""
#include <assert.h>
#include <stdarg.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
static unsigned char pixels[16];
static unsigned selected, pans;
static int fail_map;
static int fake_open(const char *path, int flags, ...) { (void)path; (void)flags; return 7; }
static int fake_ioctl(int fd, unsigned long request, ...) {
    (void)fd;
    va_list args; va_start(args, request); void *value = va_arg(args, void *); va_end(args);
    if (request == FBIOGET_VSCREENINFO) {
        struct fb_var_screeninfo *v = value; memset(v, 0, sizeof(*v));
        v->xres = v->xres_virtual = 1; v->yres = 2; v->yres_virtual = 4;
        v->bits_per_pixel = 32; v->yoffset = selected;
        v->green.offset = 8; v->blue.offset = 16; v->transp.offset = 24;
    } else if (request == FBIOGET_FSCREENINFO) {
        struct fb_fix_screeninfo *f = value; memset(f, 0, sizeof(*f));
        f->line_length = 4; f->smem_len = sizeof(pixels);
    } else if (request == FBIOPAN_DISPLAY) {
        struct fb_var_screeninfo *v = value; selected = v->yoffset; ++pans;
    }
    return 0;
}
static void *fake_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t off) {
    (void)addr; (void)prot; (void)flags; (void)fd; (void)off;
    assert(length == sizeof(pixels));
    if (fail_map) { errno = ENOMEM; return MAP_FAILED; }
    return pixels;
}
static int fake_munmap(void *addr, size_t n) { (void)addr; (void)n; return 0; }
static int fake_msync(void *addr, size_t n, int flags) { (void)addr; (void)n; (void)flags; return 0; }
static int fake_usleep(useconds_t usec) { (void)usec; return 0; }
#define open fake_open
#define ioctl fake_ioctl
#define mmap fake_mmap
#define munmap fake_munmap
#define msync fake_msync
#define usleep fake_usleep
#define main probe_main
#include "@SOURCE@"
#undef main
int main(void) {
    char *pan[] = {"fbpan", "--pan", "2"};
    assert(probe_main(3, pan) == 0 && selected == 2 && pans == 1);
    char *fill[] = {"fbpan", "--pagefill", "FF0000", "1"};
    assert(probe_main(4, fill) == 0 && selected == 2);
    assert(pixels[8] == 255 && pixels[9] == 0 && pixels[10] == 0 && pixels[11] == 255);
    unsigned before = pans; fail_map = 1;
    assert(probe_main(4, fill) != 0 && pans == before);
    char *all[] = {"fbpan", "--panfill", "00FF00"};
    assert(probe_main(3, all) != 0 && pans == before);
    return 0;
}
""")

    def test_tone_chunk_continuity_sample_width_and_low_frequency_bounds(self):
        self.compile_and_run("m3xtest.c", r"""
#include <assert.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
static int unused_probe_main(int, char **);
#define main unused_probe_main
#include "@SOURCE@"
#undef main
struct pcm { struct pcm_config config; };
static struct pcm stream;
static unsigned long frames;
static double frequency, peak;
static unsigned sample_bytes;
struct pcm *pcm_open(unsigned int card, unsigned int device, unsigned int flags, struct pcm_config *c) {
    assert(card == 0 && device == 0 && flags == PCM_OUT); stream.config = *c; return &stream;
}
int pcm_is_ready(struct pcm *p) { return p == &stream; }
int pcm_close(struct pcm *p) { assert(p == &stream); return 0; }
const char *pcm_get_error(struct pcm *p) { (void)p; return "test"; }
unsigned int pcm_get_buffer_size(struct pcm *p) { (void)p; return 4096; }
int pcm_write(struct pcm *p, const void *data, unsigned int bytes) {
    assert(p == &stream && bytes % (2 * sample_bytes) == 0 && bytes <= 4096 * 2 * sample_bytes);
    const unsigned char *buffer = data;
    for (unsigned i = 0; i < bytes / (2 * sample_bytes); ++i) {
        double s = sin(2.0 * M_PI * frequency * (frames + i) / stream.config.rate) * 0.35;
        for (unsigned c = 0; c < 2; ++c) {
            int32_t actual;
            if (sample_bytes == 2) { int16_t v; memcpy(&v, buffer + (i * 2 + c) * 2, 2); actual = v; }
            else memcpy(&actual, buffer + (i * 2 + c) * 4, 4);
            int32_t expected = (int32_t)(s * (c ? 0.7 : 1.0) * peak);
            assert(llabs((long long)actual - expected) <= 1);
        }
    }
    frames += bytes / (2 * sample_bytes); return 0;
}
int main(void) {
    const unsigned formats[] = {PCM_FORMAT_S16_LE, PCM_FORMAT_S32_LE, PCM_FORMAT_S24_LE};
    for (unsigned f = 0; f < 3; ++f) {
        sample_bytes = f == 0 ? 2 : 4;
        peak = f == 0 ? 32767.0 : f == 1 ? 2147483647.0 : 8388607.0;
        frequency = 440; frames = 0;
        assert(play_tone(0, frequency, 0.2, 44100, 2, sample_bytes * 8, formats[f]) == 0);
        assert(frames == 8820);
    }
    sample_bytes = 4; peak = 2147483647.0; frequency = 1; frames = 0;
    assert(play_tone(0, frequency, 0.2, 44100, 2, 32, PCM_FORMAT_S32_LE) == 0);
    assert(frames == 8820);
    assert(play_tone(0, 0, 1, 44100, 2, 32, PCM_FORMAT_S32_LE) != 0);
    assert(play_tone(0, 440, 1, 44100, 2, 16, PCM_FORMAT_S32_LE) != 0);
    return 0;
}
""")

    def test_dump_returns_failure_for_short_write_and_failed_close(self):
        for failure in ("write", "close"):
            with self.subTest(failure=failure):
                self.compile_and_run("fbdump.c", r"""
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
static int fail_write;
static int fake_ioctl(int fd, unsigned long request, ...) {
    (void)fd; va_list args; va_start(args, request); void *value = va_arg(args, void *); va_end(args);
    if (request == FBIOGET_VSCREENINFO) {
        struct fb_var_screeninfo *v = value; memset(v, 0, sizeof(*v));
        v->xres = 1; v->yres = 4; v->bits_per_pixel = 32;
    } else { memset(value, 0, sizeof(struct fb_fix_screeninfo)); }
    return 0;
}
static size_t fake_fwrite(const void *data, size_t size, size_t count, FILE *file) {
    if (fail_write) { errno = ENOSPC; return count - 1; }
    return fwrite(data, size, count, file);
}
static int fake_fclose(FILE *file) { fclose(file); errno = EIO; return -1; }
#define ioctl fake_ioctl
#define fwrite fake_fwrite
#define fclose fake_fclose
#define main probe_main
#include "@SOURCE@"
#undef main
int main(int argc, char **argv) {
    assert(argc == 4); fail_write = strcmp(argv[3], "write") == 0;
    assert(probe_main(3, argv) != 0); return 0;
}
""", (failure,))
