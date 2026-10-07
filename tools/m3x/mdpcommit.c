/* mdpcommit: drive /dev/graphics/fb0 onto the panel via MSMFB_DISPLAY_COMMIT.
 *
 * Why
 * ---
 * On this MSM the fb is not the scanned-out buffer by default: SurfaceFlinger
 * composites into gralloc buffers and programs the MDP overlay pipe through the
 * MSMFB_DISPLAY_COMMIT ioctl. Plain writes to fb0 therefore never reach the
 * panel, which is why Rockbox can render a correct image into fb0 and the
 * screen stays unchanged.
 *
 * MSMFB_DISPLAY_COMMIT is the ioctl SurfaceFlinger itself uses to hand the
 * panel to a new buffer (drivers/video/msm/mdss/mdss_fb.c ->
 * mdss_fb_display_commit -> mdss_fb_pan_display_ex). It is a normal ioctl on
 * the fb file descriptor, so a native Rockbox process can call it - no Java,
 * no Surface, no SurfaceFlinger.
 *
 * Usage:
 *   mdpcommit <dev> [yoffset]
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
#include <linux/fb.h>

#ifndef MSMFB_IOCTL_MAGIC
#define MSMFB_IOCTL_MAGIC 'm'
#endif

struct mdp_rect {
    uint32_t x, y, w, h;
} __attribute__((packed));

struct mdp_display_commit {
    uint32_t flags;
    uint32_t wait_for_finish;
    struct fb_var_screeninfo var;
    struct mdp_rect l_roi;
    struct mdp_rect r_roi;
};

#define MSMFB_DISPLAY_COMMIT \
    _IOW(MSMFB_IOCTL_MAGIC, 164, struct mdp_display_commit)

int main(int argc, char **argv)
{
    const char *dev = argc > 1 ? argv[1] : "/dev/graphics/fb0";
    unsigned yoff = (argc > 2) ? (unsigned)strtoul(argv[2], NULL, 0) : 0;

    int fd = open(dev, O_RDWR);
    if (fd < 0) {
        printf("open(%s) failed: %s\n", dev, strerror(errno));
        return 1;
    }

    struct fb_var_screeninfo var;
    memset(&var, 0, sizeof(var));
    if (ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0) {
        printf("FBIOGET_VSCREENINFO: %s\n", strerror(errno));
        return 1;
    }
    printf("fb: %ux%u virtual %ux%u, current yoffset=%u\n",
           var.xres, var.yres, var.xres_virtual, var.yres_virtual, var.yoffset);

    struct mdp_display_commit d;
    memset(&d, 0, sizeof(d));

    d.flags = 0;
    d.wait_for_finish = 1;

    /* Commit the whole visible area of the requested page. */
    d.var = var;
    d.var.yoffset = yoff;
    d.var.xoffset = 0;
    d.var.activate = FB_ACTIVATE_NOW;

    d.l_roi.x = 0;
    d.l_roi.y = 0;
    d.l_roi.w = var.xres;
    d.l_roi.h = var.yres;
    d.r_roi.x = d.r_roi.y = d.r_roi.w = d.r_roi.h = 0;

    printf("calling MSMFB_DISPLAY_COMMIT (nr=0x%lx, size=%zu) yoffset=%u roi=%ux%u ...\n",
           (unsigned long)MSMFB_DISPLAY_COMMIT, sizeof(d), yoff,
           d.l_roi.w, d.l_roi.h);
    fflush(stdout);

    if (ioctl(fd, MSMFB_DISPLAY_COMMIT, &d) < 0) {
        printf("MSMFB_DISPLAY_COMMIT FAILED: %s (errno %d)\n",
               strerror(errno), errno);
        return 1;
    }

    printf("MSMFB_DISPLAY_COMMIT OK\n");

    struct fb_var_screeninfo after;
    memset(&after, 0, sizeof(after));
    if (ioctl(fd, FBIOGET_VSCREENINFO, &after) == 0)
        printf("after: yoffset=%u xoffset=%u\n", after.yoffset, after.xoffset);

    return 0;
}