/* fbdump: copy the M3X framebuffer out as raw RGBA so it can be inspected.
 *
 * /dev/graphics/fb0 on the M3X reports 768x1280, 32bpp, with red.offset == 0,
 * i.e. the bytes in memory are R,G,B,A (confirmed by SurfaceFlinger reporting
 * format=1 / PIXEL_FORMAT_RGBA_8888).
 *
 *   fbdump <fb-dev> <out-file> [bytes]
 *
 * Writes the visible buffer only (xres * yres * bpp / 8), starting at the
 * framebuffer's offset, i.e. the first of the two yres_virtual pages.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

int main(int argc, char **argv)
{
    const char *dev = (argc > 1) ? argv[1] : "/dev/graphics/fb0";
    const char *out = (argc > 2) ? argv[2] : "/data/local/tmp/fb.bin";
    int fd;
    struct fb_var_screeninfo v;
    struct fb_fix_screeninfo f;
    size_t need;
    unsigned char *buf;
    FILE *fp;

    fd = open(dev, O_RDONLY);
    if (fd < 0) { perror("open"); return 1; }

    memset(&v, 0, sizeof(v));
    memset(&f, 0, sizeof(f));
    if (ioctl(fd, FBIOGET_VSCREENINFO, &v) < 0) { perror("FBIOGET_VSCREENINFO"); return 1; }
    if (ioctl(fd, FBIOGET_FSCREENINFO, &f) < 0) { perror("FBIOGET_FSCREENINFO"); return 1; }

    need = (size_t)v.xres * v.yres * v.bits_per_pixel / 8;
    if (argc > 3) need = (size_t)strtoul(argv[3], NULL, 0);

    fprintf(stderr, "%s: %ux%u %ubpp, visible bytes %zu\n", dev, v.xres, v.yres,
            v.bits_per_pixel, need);

    buf = malloc(need);
    if (!buf) { fprintf(stderr, "malloc failed\n"); return 1; }

    if (lseek(fd, 0, SEEK_SET) < 0) { perror("lseek"); return 1; }
    if (read(fd, buf, need) != (ssize_t)need) {
        perror("read");
        free(buf);
        return 1;
    }

    fp = fopen(out, "wb");
    if (!fp) { perror("fopen"); free(buf); return 1; }
    if (fwrite(buf, 1, need, fp) != need) { perror("fwrite"); }
    fclose(fp);
    fprintf(stderr, "wrote %zu bytes to %s\n", need, out);

    free(buf);
    close(fd);
    return 0;
}