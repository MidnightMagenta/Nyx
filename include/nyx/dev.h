#ifndef _NYX_DEV_H
#define _NYX_DEV_H

#include <nyx/vfs.h>

struct cdev;

struct cdev_ops {
    ssize_t (*read)(struct cdev *, void *buf, size_t len);
    ssize_t (*write)(struct cdev *, const void *buf, size_t len);
};

struct cdev_args {
    const char            *name;
    const struct cdev_ops *ops;
    void                  *priv;
};

int register_cdev(struct cdev_args *args);

#endif
