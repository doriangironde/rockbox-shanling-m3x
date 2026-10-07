/* i2cdump: silent register reader, used to reverse-engineer the M3X AK4497 DAC.
 *
 * The AK4495/AK4497 codec sits on i2c bus 5 at address 0x12; we only READ here,
 * so nothing is audible and nothing is written.
 *
 * Uses the classic i2c-dev "write reg, then read N bytes" pattern, which avoids
 * the SMBus structs (the NDK's linux/i2c-dev.h is too old for them).
 *
 *   i2cdump <i2c-dev> <slave-addr> <first-reg> <last-reg> [nbytes] [8|16]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

/* Not all (older) i2c-dev.h headers define these */
#ifndef I2C_SLAVE_FORCE
#define I2C_SLAVE_FORCE  0x0706
#endif

int main(int argc, char **argv)
{
    const char *dev;
    int fd, slave, first, last, nbytes = 1, width = 8;

    if (argc < 5) {
        fprintf(stderr,
            "usage: i2cdump <i2c-dev> <slave-addr> <first-reg> <last-reg> [nbytes] [8|16]\n");
        return 1;
    }
    dev   = argv[1];
    slave = (int)strtol(argv[2], NULL, 0);
    first = (int)strtol(argv[3], NULL, 0);
    last  = (int)strtol(argv[4], NULL, 0);
    if (argc > 5) nbytes = atoi(argv[5]);
    if (argc > 6) width  = atoi(argv[6]);
    if (nbytes < 1 || nbytes > 32) nbytes = 1;

    fd = open(dev, O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    /* The kernel ak4497 driver already owns this address, so plain I2C_SLAVE
     * returns EBUSY; force-claim it instead (read-only, safe when idle). */
    if (ioctl(fd, I2C_SLAVE_FORCE, slave) < 0 && ioctl(fd, I2C_SLAVE, slave) < 0) {
        perror("I2C_SLAVE(_FORCE)"); close(fd); return 1;
    }

    printf("# device=%s slave=0x%02x nbytes=%d width=%d regs 0x%02x..0x%02x\n",
           dev, slave, nbytes, width, first, last);

    for (int r = first; r <= last; r++) {
        unsigned char addr[2], buf[32];

        memset(addr, 0, sizeof(addr));
        memset(buf,  0xff, sizeof(buf));
        if (width == 16) { addr[0] = (unsigned char)(r >> 8); addr[1] = (unsigned char)r; }
        else             { addr[0] = (unsigned char)r; }

        if (write(fd, addr, width == 16 ? 2 : 1) != (width == 16 ? 2 : 1)) {
            printf("0x%02x WERR %s\n", r, strerror(errno));
            continue;
        }
        if (read(fd, buf, (size_t)nbytes) != nbytes) {
            printf("0x%02x RERR %s\n", r, strerror(errno));
            continue;
        }
        printf("0x%02x =", r);
        for (int i = 0; i < nbytes; i++) printf(" %02x", buf[i]);
        printf("\n");
    }
    close(fd);
    return 0;
}