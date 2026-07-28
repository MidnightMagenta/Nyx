#include <nyx/early_serial.h>
#include <nyx/initcall.h>
#include <nyx/linkage.h>
#include <nyx/vfs.h>

#define CONSOLE_RDEV makedev(5, 1)

static int console_write(dev_t d, struct uio *u) {
    (void) d;
    char buf[128];

    while (u->uio_resid > 0) {
        size_t k = u->uio_resid < sizeof(buf) ? u->uio_resid : sizeof(buf);
        int    e = uiomove(buf, k, u);
        if (e) { return e; }

        for (size_t i = 0; i < k; i++) { early_serial_putc(buf[i]); }
    }

    return 0;
}


static int console_read(dev_t d, struct uio *u) {
    (void) d;
    (void) u;
    return 0;
} /* no kbd yet */
static const struct cdevsw console_cdevsw = {
        .d_name  = "console",
        .d_open  = cdev_noop,
        .d_close = cdev_noop,
        .d_read  = console_read,
        .d_write = console_write,
        .d_ioctl = cdev_notty,
};

DEFINE_INITCALL(init_consoledev) {
    cdev_register("console", &console_cdevsw, CONSOLE_RDEV, 0666);
}
