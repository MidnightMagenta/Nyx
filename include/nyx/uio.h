#ifndef _NYX_UIO_H
#define _NYX_UIO_H

#include <nyx/stddef.h>
#include <uapi/posix_types.h>

enum uio_rw { UIO_READ, UIO_WRITE };
enum uio_seg { UIO_USERSPACE, UIO_SYSSPACE };

struct iovec {
    void  *iov_base;
    size_t iov_len;
};

struct uio {
    struct iovec *uio_iov;
    int           uio_iovcnt;
    off_t         uio_offset;
    size_t        uio_resid;
    enum uio_rw   uio_rw;
    enum uio_seg  uio_segflg;
};

int uiomove(void *buf, size_t n, struct uio *uio);

#endif
