#include <mm/vmspace.h>
#include <nyx/string.h>
#include <nyx/uio.h>

#include <asi/bug.h>

int uiomove(void *buf, size_t n, struct uio *uio) {
    char *cp    = buf;
    int   error = 0;

    BUG_ON(uio->uio_rw != UIO_READ && uio->uio_rw != UIO_WRITE);

    while (n > 0 && uio->uio_resid > 0) {
        struct iovec *iov = uio->uio_iov;
        size_t        cnt = iov->iov_len;

        if (cnt == 0) {
            uio->uio_iov++;
            uio->uio_iovcnt--;
            continue;
        }
        if (cnt > n) { cnt = n; }

        switch (uio->uio_segflg) {
            case UIO_USERSPACE:
                if (uio->uio_rw == UIO_READ) {
                    error = copyout(iov->iov_base, cp, cnt);
                } else {
                    error = copyin(cp, iov->iov_base, cnt);
                }
                if (error) { return error; }
                break;
            case UIO_SYSSPACE:
                if (uio->uio_rw == UIO_READ) {
                    memcpy(iov->iov_base, cp, cnt);
                } else {
                    memcpy(cp, iov->iov_base, cnt);
                }
                break;
        }

        iov->iov_base = (char *) iov->iov_base + cnt;
        iov->iov_len -= cnt;
        uio->uio_resid -= cnt;
        uio->uio_offset += cnt;
        cp += cnt;
        n -= cnt;
    }
    return 0;
}
