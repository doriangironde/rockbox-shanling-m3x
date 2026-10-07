/* fbinfo: dump Android framebuffer geometry for the Shanling M3X port */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "/dev/graphics/fb0";
    int fd = open(path, O_RDWR);
    if (fd < 0) { printf("open(%s) failed: %s\n", path, strerror(errno)); return 1; }

    struct fb_var_screeninfo v; struct fb_fix_screeninfo f;
    memset(&v, 0, sizeof(v)); memset(&f, 0, sizeof(f));
    if (ioctl(fd, FBIOGET_VSCREENINFO, &v) < 0) { printf("FBIOGET_VSCREENINFO: %s\n", strerror(errno)); return 1; }
    if (ioctl(fd, FBIOGET_FSCREENINFO, &f) < 0) { printf("FBIOGET_FSCREENINFO: %s\n", strerror(errno)); return 1; }

    printf("== %s ==\n", path);
    printf("visible   : %ux%u\n", v.xres, v.yres);
    printf("virtual   : %ux%u\n", v.xres_virtual, v.yres_virtual);
    printf("offset    : %u,%u\n", v.xoffset, v.yoffset);
    printf("bpp       : %u\n", v.bits_per_pixel);
    printf("mm        : %ux%u\n", v.width, v.height);
    printf("RGB off   : r=%u/%u g=%u/%u b=%u/%u a=%u/%u\n",
           v.red.offset, v.red.length, v.green.offset, v.green.length,
           v.blue.offset, v.blue.length, v.transp.offset, v.transp.length);
    printf("activate  : 0x%x  nonstd: %u  grayscale: %u  rotate: 0x%x\n",
           v.activate, v.nonstd, v.grayscale, v.rotate);
    printf("line_length: %u  mmio_start: 0x%x\n", f.line_length, f.mmio_start);
    printf("smem_len  : %u (%u KiB)\n", f.smem_len, f.smem_len/1024);
    printf("smem_start: 0x%llx  id: %s\n", (unsigned long long)f.smem_start, f.id);
    unsigned need = f.line_length * v.yres_virtual;
    printf("need bytes (line_length*yres_virtual) = %u (%u KiB)\n", need, need/1024);
    printf("need bytes (xres*yres*bpp/8)          = %u (%u KiB)\n",
           v.xres*v.yres*v.bits_per_pixel/8, (v.xres*v.yres*v.bits_per_pixel/8)/1024);

    /* Optional: try to pin the scanned-out page with yoffset (double buffering
     * via FBIOPUT_VSCREENINFO). Usage: fbinfo <dev> <yoffset> */
    if (argc > 2) {
        unsigned yoff = (unsigned)strtoul(argv[2], NULL, 0);
        struct fb_var_screeninfo p = v;
        p.yoffset = yoff;
        p.activate = FB_ACTIVATE_NOW;
        if (ioctl(fd, FBIOPUT_VSCREENINFO, &p) < 0) {
            printf("FBIOPUT_VSCREENINFO yoffset=%u FAILED: %s\n", yoff, strerror(errno));
        } else {
            struct fb_var_screeninfo r;
            memset(&r, 0, sizeof(r));
            if (ioctl(fd, FBIOGET_VSCREENINFO, &r) == 0)
                printf("FBIOPUT_VSCREENINFO yoffset=%u OK -> read back yoffset=%u xoffset=%u\n",
                       yoff, r.yoffset, r.xoffset);
            else
                printf("FBIOPUT_VSCREENINFO yoffset=%u OK (readback failed: %s)\n",
                       yoff, strerror(errno));
        }
    }

    close(fd);
    return 0;
}
