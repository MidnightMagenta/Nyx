#include <nyx/initcall.h>
#include <nyx/vfs.h>
#include <uapi/posix_types.h>

struct uio;

#define NULL_RDEV makedev(1, 3)

static int null_read(dev_t d, struct uio *u) {
    (void) d;
    (void) u;
    return 0;
}

static const struct cdevsw null_cdevsw = {
        .d_name  = "null",
        .d_open  = cdev_noop,
        .d_close = cdev_noop,
        .d_read  = null_read,
        .d_write = cdev_sink,
        .d_ioctl = cdev_notty,
};

DEFINE_INITCALL(init_nulldev) {
    cdev_register("null", &null_cdevsw, NULL_RDEV, 0666);
}
