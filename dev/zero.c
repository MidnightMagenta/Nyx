#include <nyx/linkage.h>
#include <nyx/vfs.h>

#define ZERO_RDEV makedev(1, 5)

static int zero_read(dev_t d, struct uio *u) {
    (void) d;
    static const char zb[256] = {0};
    while (u->uio_resid > 0) {
        size_t k = u->uio_resid < sizeof(zb) ? u->uio_resid : sizeof(zb);
        int    e = uiomove((void *) zb, k, u);
        if (e) { return e; }
    }
    return 0;
}

static const struct cdevsw zero_cdevsw = {
        .d_name  = "zero",
        .d_open  = cdev_noop,
        .d_close = cdev_noop,
        .d_read  = zero_read,
        .d_write = cdev_sink,
        .d_ioctl = cdev_notty,
};

void __init init_zerodev() {
    cdev_register("zero", &zero_cdevsw, ZERO_RDEV, 0666);
}
