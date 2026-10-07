/* fbpan: correct FBIOPAN_DISPLAY / FBIOBLANK test for the Shanling M3X.
 *
 * Why this file exists
 * --------------------
 * An earlier probe (fbflip.c) tested fb ioctls using numbers that were not from
 * linux/fb.h. FBIOPANIC was actually FBIOGET_FSCREENINFO (0x4602), FBIOBLANK was
 * 0x4801 rather than the real 0x4611, and several of the others were invented.
 * Every "ENOTTY" from that probe therefore only meant "unrecognised ioctl
 * number", not "driver lacks the feature".
 *
 * mdss_fb actually implements all of these (drivers/video/msm/mdss/mdss_fb.c):
 *
 *     .fb_blank       = mdss_fb_blank        -> FBIOBLANK
 *     .fb_pan_display = mdss_fb_pan_display  -> FBIOPAN_DISPLAY
 *     .fb_ioctl_v2    = mdss_fb_ioctl        -> MSMFB_* vendor ioctls
 *
 * and FBIOPAN_DISPLAY routes to the same mdss_fb_pan_display_ex() that
 * MSMFB_DISPLAY_COMMIT uses.
 *
 * Real numbers, from include/uapi/linux/fb.h:
 *     FBIOGET_VSCREENINFO  0x4600     FBIOGET_FSCREENINFO  0x4602
 *     FBIOPUT_VSCREENINFO  0x4601     FBIOPAN_DISPLAY      0x4606
 *     FBIOBLANK            0x4611
 *
 * Usage:
 *   fbpan --info                 report geometry and current var
 *   fbpan --blank N              FBIOBLANK(N)   0=off 1=blank/unblank
 *   fbpan --pan [yoffset]        present page, no fill
 *   fbpan --panfill RRGGBB [yo]  fill both pages with RRGGBB, then present
 *   fbpan --pagefill RRGGBB P[y] fill one page, then present page P
 *
 * Produces no sound.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>

static int fd = -1;
static struct fb_fix_screeninfo fix;
static struct fb_var_screeninfo var;

static void report(const char *tag)
{
    struct fb_var_screeninfo v;
    memset(&v, 0, sizeof(v));
    if (ioctl(fd, FBIOGET_VSCREENINFO, &v) == 0)
        printf("%-18s yoffset=%-5u xoffset=%-5u activate=0x%-4x %ux%u virtual %ux%u\n",
               tag, v.yoffset, v.xoffset, v.activate,
               v.xres, v.yres, v.xres_virtual, v.yres_virtual);
    else
        printf("%-18s FBIOGET_VSCREENINFO: %s\n", tag, strerror(errno));
}

static int fill_pages(unsigned rgb)
{
    size_t len = (size_t)fix.line_length * var.yres_virtual;
    unsigned char *p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    unsigned char px[4];
    unsigned pages, pg;

    if (p == MAP_FAILED) {
        printf("mmap(%zu): %s\n", len, strerror(errno));
        return 1;
    }

    /* The M3X fb is RGBA8888 (red.offset 0, green 8, blue 16, alpha 24). */
    px[0] = rgb & 0xff;
    px[1] = (rgb >> 8) & 0xff;
    px[2] = (rgb >> 16) & 0xff;
    px[3] = 0xff;

    pages = var.yres_virtual / var.yres;
    if (!pages) pages = 1;

    for (pg = 0; pg < pages; pg++) {
        unsigned row_bytes = var.xres * 4;
        unsigned y;
        unsigned char *row = malloc(row_bytes);
        unsigned i;

        if (!row) { munmap(p, len); return 1; }
        for (i = 0; i + 4 <= row_bytes; i += 4)
            memcpy(row + i, px, 4);

        for (y = 0; y < var.yres; y++)
            memcpy(p + (size_t)pg * var.yres * fix.line_length
                     + (size_t)y * fix.line_length, row, row_bytes);
        free(row);
        printf("  filled page %u (yoffset %u) %02x%02x%02x\n",
               pg, pg * var.yres, px[2], px[1], px[0]);
    }

    msync(p, len, MS_SYNC);
    munmap(p, len);
    return 0;
}

static int fill_one_page(unsigned rgb, unsigned page)
{
    size_t len = (size_t)fix.line_length * var.yres_virtual;
    unsigned char *p, *row;
    unsigned char px[4];
    unsigned row_bytes = var.xres * 4;
    unsigned y, i;

    if (var.yres_virtual / var.yres && page >= var.yres_virtual / var.yres) {
        printf("page %u out of range (%u pages)\n", page,
               var.yres_virtual / var.yres);
        return 1;
    }

    p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        printf("mmap(%zu): %s\n", len, strerror(errno));
        return 1;
    }

    px[0] = rgb & 0xff;
    px[1] = (rgb >> 8) & 0xff;
    px[2] = (rgb >> 16) & 0xff;
    px[3] = 0xff;

    row = malloc(row_bytes);
    if (!row) { munmap(p, len); return 1; }
    for (i = 0; i + 4 <= row_bytes; i += 4)
        memcpy(row + i, px, 4);

    for (y = 0; y < var.yres; y++)
        memcpy(p + (size_t)page * var.yres * fix.line_length
                 + (size_t)y * fix.line_length, row, row_bytes);

    msync(p, len, MS_SYNC);
    munmap(p, len);
    free(row);

    printf("  filled page %u (yoffset %u) %02x%02x%02x\n",
           page, page * var.yres, px[2], px[1], px[0]);
    return 0;
}

static int do_blank(int mode)
{
    int rc = ioctl(fd, FBIOBLANK, (unsigned long)mode);
    if (rc < 0) {
        printf("FBIOBLANK(%d) FAILED: %s (errno %d)\n", mode, strerror(errno), errno);
        return 1;
    }
    printf("FBIOBLANK(%d) OK\n", mode);
    usleep(300000);
    report("after blank");
    return 0;
}

static int do_pan(unsigned yoff)
{
    struct fb_var_screeninfo v = var;

    printf("FBIOPAN_DISPLAY(yoffset=%u) ...\n", yoff);
    fflush(stdout);
    if (ioctl(fd, FBIOPAN_DISPLAY, &v) < 0) {
        printf("FBIOPAN_DISPLAY FAILED: %s (errno %d)\n", strerror(errno), errno);
        return 1;
    }
    printf("FBIOPAN_DISPLAY OK\n");
    usleep(300000);
    report("after pan");
    return 0;
}

int main(int argc, char **argv)
{
    const char *dev = "/dev/graphics/fb0";
    unsigned fill = 0x00FF00;   /* green */
    unsigned yoff = 0;

    fd = open(dev, O_RDWR);
    if (fd < 0) {
        printf("open(%s): %s\n", dev, strerror(errno));
        return 1;
    }
    if (ioctl(fd, FBIOGET_FSCREENINFO, &fix) < 0 ||
        ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0) {
        printf("get: %s\n", strerror(errno));
        return 1;
    }

    printf("fb: %ux%u virtual %ux%u %ubpp line_length %u smem %u id '%s'\n",
           var.xres, var.yres, var.xres_virtual, var.yres_virtual,
           var.bits_per_pixel, fix.line_length, fix.smem_len, fix.id);
    printf("RGB offsets: r=%u g=%u b=%u a=%u\n",
           var.red.offset, var.green.offset, var.blue.offset, var.transp.offset);
    report("current");

    if (argc < 2) {
        printf("usage: --info | --blank N | --pan [yoff] | --panfill RRGGBB [yoff]\n");
        return 0;
    }

    if (!strcmp(argv[1], "--info"))
        return 0;

    if (!strcmp(argv[1], "--blank"))
        return do_blank(argc > 2 ? atoi(argv[2]) : 0);

    if (!strcmp(argv[1], "--pan")) {
        yoff = (argc > 2) ? (unsigned)strtoul(argv[2], NULL, 0) : 0;
        return do_pan(yoff);
    }

    if (!strcmp(argv[1], "--panfill")) {
        if (argc > 2) fill = (unsigned)strtoul(argv[2], NULL, 16);
        yoff = (argc > 3) ? (unsigned)strtoul(argv[3], NULL, 0) : 0;
        if (fill_pages(fill) < 0)
            return 1;
        return do_pan(yoff);
    }

    if (!strcmp(argv[1], "--pagefill")) {
        /* --pagefill RRGGBB PAGE [yoffset] : fill one page, pan to it */
        if (argc < 4) { printf("--pagefill needs RRGGBB and PAGE\n"); return 1; }
        unsigned rgb = (unsigned)strtoul(argv[2], NULL, 16);
        unsigned page = (unsigned)strtoul(argv[3], NULL, 0);
        unsigned yo = (argc > 4) ? (unsigned)strtoul(argv[4], NULL, 0)
                                 : page * var.yres;
        if (fill_one_page(rgb, page) < 0)
            return 1;
        return do_pan(yo);
    }

    printf("unknown option '%s'\n", argv[1]);
    return 1;
}