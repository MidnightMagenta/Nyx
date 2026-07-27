#include <mm/kmalloc.h>
#include <nyx/errno.h>
#include <nyx/fcntl.h>
#include <nyx/proc.h>
#include <nyx/spinlock.h>
#include <nyx/string.h>
#include <nyx/syscall.h>
#include <nyx/vfs.h>

#include <asi/bug.h>

#define FD_INITIAL 32

static int fd_grow(struct filedesc *fdp, int want) {
    int             ncap = fdp->fd_nfiles ? fdp->fd_nfiles : FD_INITIAL;
    struct fdentry *na;

    while (ncap <= want) { ncap *= 2; }

    na = kmalloc(ncap * sizeof(struct fdentry), M_SLEEPOK);
    if (!na) { return ENOMEM; }
    memset(na, 0, ncap * sizeof(struct fdentry));

    if (fdp->fd_files) {
        memcpy(na, fdp->fd_files, fdp->fd_nfiles * sizeof(struct fdentry));
        kfree(fdp->fd_files);
    }

    fdp->fd_files  = na;
    fdp->fd_nfiles = ncap;

    return 0;
}

int fdinit(struct process *p) {
    struct filedesc *fdp = kmalloc(sizeof(*fdp), M_SLEEPOK);
    if (!fdp) { return ENOMEM; }

    memset(fdp, 0, sizeof(*fdp));
    spinlock_init(&fdp->fd_lock);

    if (fd_grow(fdp, FD_INITIAL - 1) != 0) {
        kfree(fdp);
        return ENOMEM;
    }

    if (rootvnode) {
        vref(rootvnode);
        fdp->fd_cdir = rootvnode;
        vref(rootvnode);
        fdp->fd_rdir = rootvnode;
    }

    p->fd = fdp;
    return 0;
}

struct filedesc *fdcopy(struct filedesc *src) {
    struct filedesc *fdp = kmalloc(sizeof(*fdp), M_SLEEPOK);
    if (!fdp) { return NULL; }

    memset(fdp, 0, sizeof(*fdp));
    spinlock_init(&fdp->fd_lock);

    if (fd_grow(fdp, src->fd_nfiles - 1) != 0) {
        kfree(fdp);
        return NULL;
    }

    for (int fd = 0; fd < src->fd_nfiles; fd++) {
        struct file *fp = src->fd_files[fd].fe_file;
        if (!fp) { continue; }
        fhold(fp);
        fdp->fd_files[fd] = src->fd_files[fd];
    }

    if (src->fd_cdir) {
        vref(src->fd_cdir);
        fdp->fd_cdir = src->fd_cdir;
    }

    if (src->fd_rdir) {
        vref(src->fd_rdir);
        fdp->fd_rdir = src->fd_rdir;
    }

    return fdp;
}

void fdfree(struct process *p) {
    struct filedesc *fdp = p->fd;
    if (!fdp) return;
    for (int fd = 0; fd < fdp->fd_nfiles; fd++) {
        if (fdp->fd_files[fd].fe_file) { fdrop(fdp->fd_files[fd].fe_file); }
    }

    if (fdp->fd_cdir) { vrele(fdp->fd_cdir); }
    if (fdp->fd_rdir) { vrele(fdp->fd_rdir); }
    kfree(fdp->fd_files);
    kfree(fdp);

    p->fd = NULL;
}

int fdalloc(struct process *p, int min, int *fdout) {
    struct filedesc *fdp = p->fd;

    for (int fd = min; fd < fdp->fd_nfiles; fd++) {
        if (fdp->fd_files[fd].fe_file == NULL) {
            *fdout = fd;
            return 0;
        }
    }

    int fd    = fdp->fd_nfiles;
    int error = fd_grow(fdp, fd);
    if (error) { return error; }
    *fdout = fd;
    return 0;
}

int falloc(struct process *p, struct file **fpout, int *fdout) {
    int          fd, error = fdalloc(p, 0, &fd);
    struct file *fp;

    if (error) return error;

    fp = kmalloc(sizeof(*fp), M_SLEEPOK);
    if (!fp) { return ENOMEM; }
    memset(fp, 0, sizeof(*fp));
    refcount_set(&fp->f_count, 1);

    p->fd->fd_files[fd].fe_file  = fp;
    p->fd->fd_files[fd].fe_flags = 0;

    *fpout = fp;
    *fdout = fd;

    return 0;
}

struct file *fget(struct process *p, int fd) {
    struct filedesc *fdp = p->fd;
    struct file     *fp;

    if (fd < 0 || fd >= fdp->fd_nfiles) { return NULL; }
    fp = fdp->fd_files[fd].fe_file;
    if (fp) { fhold(fp); }

    return fp;
}

void fhold(struct file *fp) {
    BUG_ON(refcount_get(&fp->f_count) == 0);
    refcount_inc(&fp->f_count);
}

void fdrop(struct file *fp) {
    if (!refcount_dec_and_test(&fp->f_count)) { return; }
    if (fp->f_ops) { (void) fp->f_ops->fo_close(fp); }
    kfree(fp);
}

int fd_close(struct process *p, int fd) {
    struct filedesc *fdp = p->fd;
    struct file     *fp;

    if (fd < 0 || fd >= fdp->fd_nfiles) { return EBADF; }
    fp = fdp->fd_files[fd].fe_file;
    if (!fp) { return EBADF; }

    fdp->fd_files[fd].fe_file  = NULL;
    fdp->fd_files[fd].fe_flags = 0;
    fdrop(fp);

    return 0;
}

static inline int fp_can_read(struct file *fp) {
    return (fp->f_flags & O_ACCMODE) != O_WRONLY;
}
static inline int fp_can_write(struct file *fp) {
    return (fp->f_flags & O_ACCMODE) != O_RDONLY;
}

static int vn_read(struct file *fp, struct uio *uio, int flags) {
    struct vnode *vp;
    int           error;

    if (!fp_can_read(fp)) { return EBADF; }

    vp              = fp->f_data;
    uio->uio_offset = fp->f_offset;
    error           = VOP_READ(vp, uio, flags);
    fp->f_offset    = uio->uio_offset;

    return error;
}

static int vn_write(struct file *fp, struct uio *uio, int flags) {
    struct vnode *vp;
    int           error;

    if (!fp_can_write(fp)) { return EBADF; }

    vp              = fp->f_data;
    uio->uio_offset = fp->f_offset;
    error           = VOP_WRITE(vp, uio, flags);
    fp->f_offset    = uio->uio_offset;

    return error;
}

static int vn_ioctl(struct file *fp, unsigned long cmd, void *data) {
    return VOP_IOCTL((struct vnode *) fp->f_data, cmd, data);
}

static int vn_stat(struct file *fp, struct vattr *vap) {
    return VOP_GETATTR((struct vnode *) fp->f_data, vap);
}

static int vn_closefile(struct file *fp) {
    struct vnode *vp    = fp->f_data;
    int           error = VOP_CLOSE(vp, fp->f_flags);
    vrele(vp);
    return error;
}

const struct fileops vnops = {
        .fo_read  = vn_read,
        .fo_write = vn_write,
        .fo_ioctl = vn_ioctl,
        .fo_stat  = vn_stat,
        .fo_close = vn_closefile,
};

int vn_open(struct nameidata *ndp, int fflags, u16 mode) {
    struct vnode *vp;
    int           error;
    (void) mode;

    error = namei(ndp);
    if (error) { return error; }

    vp    = ndp->ni_vp;
    error = VOP_OPEN(vp, fflags);
    if (error) {
        vrele(vp);
        ndp->ni_vp = NULL;
        return error;
    }
    return 0;
}

int vn_rdwr(enum uio_rw rw, struct vnode *vp, void *buf, size_t len, off_t off, size_t *residp) {
    struct iovec iov = {.iov_base = buf, .iov_len = len};
    struct uio   uio = {
            .uio_iov    = &iov,
            .uio_iovcnt = 1,
            .uio_offset = off,
            .uio_resid  = len,
            .uio_rw     = rw,
            .uio_segflg = UIO_SYSSPACE,
    };

    int e = (rw == UIO_READ) ? VOP_READ(vp, &uio, 0) : VOP_WRITE(vp, &uio, 0);
    if (residp) { *residp = uio.uio_resid; }
    return e;
}

int vfs_open(struct process *p, const char *path, enum uio_seg seg, int flags, u16 mode, int *fdout) {
    struct nameidata nd = {
            .ni_dirp   = path,
            .ni_segflg = seg,
            .ni_op     = NAMEI_LOOKUP,
            .ni_flags  = NAMEI_FOLLOW,
            .ni_proc   = p,
    };

    struct file *fp;
    int          error, fd;

    error = vn_open(&nd, flags, mode);
    if (error) { return error; }

    error = falloc(p, &fp, &fd);
    if (error) {
        (void) VOP_CLOSE(nd.ni_vp, flags);
        vrele(nd.ni_vp);
        return error;
    }

    fp->f_ops    = &vnops;
    fp->f_data   = nd.ni_vp;
    fp->f_offset = 0;
    fp->f_flags  = flags;
    if (flags & O_CLOEXEC) { p->fd->fd_files[fd].fe_flags |= FD_CLOEXEC; }

    *fdout = fd;
    return 0;
}

int kern_open(struct process *p, const char *path, int flags, u16 mode, int *fdout) {
    return vfs_open(p, path, UIO_USERSPACE, flags, mode, fdout);
}

int kern_close(struct process *p, int fd) {
    return fd_close(p, fd);
}

int kern_read(struct process *p, int fd, const void *ubuf, size_t count, ssize_t *nread) {
    struct file *fp;
    struct iovec iov;
    struct uio   uio;
    int          error;

    fp           = fget(p, fd);
    iov.iov_base = (void *) ubuf;
    iov.iov_len  = count;

    uio.uio_iov    = &iov;
    uio.uio_iovcnt = 1;
    uio.uio_resid  = count;
    uio.uio_rw     = UIO_READ;
    uio.uio_segflg = UIO_USERSPACE;

    error  = fp->f_ops->fo_read(fp, &uio, 0);
    *nread = (ssize_t) (count - uio.uio_resid);
    fdrop(fp);
    return error;
}

int kern_write(struct process *p, int fd, const void *ubuf, size_t count, ssize_t *nwritten) {
    struct file *fp;
    struct iovec iov;
    struct uio   uio;
    int          error;

    fp           = fget(p, fd);
    iov.iov_base = (void *) ubuf;
    iov.iov_len  = count;

    uio.uio_iov    = &iov;
    uio.uio_iovcnt = 1;
    uio.uio_resid  = count;
    uio.uio_rw     = UIO_WRITE;
    uio.uio_segflg = UIO_USERSPACE;

    error     = fp->f_ops->fo_write(fp, &uio, 0);
    *nwritten = (ssize_t) (count - uio.uio_resid);
    fdrop(fp);
    return error;
}

int kern_dup(struct process *p, int oldfd, int *newfd) {
    struct file *fp = fget(p, oldfd);
    int          fd, error;

    if (!fp) return EBADF;

    error = fdalloc(p, 0, &fd);
    if (error) {
        fdrop(fp);
        return error;
    }

    p->fd->fd_files[fd].fe_file  = fp;
    p->fd->fd_files[fd].fe_flags = 0;
    *newfd                       = fd;
    return 0;
}

int sys_open(struct thread *t, struct syscall_args *args, register_t *retval) {
    return kern_open(t->proc, (const char *) args->arg1, (int) args->arg2, 0, (int *) retval);
}

int sys_close(struct thread *t, struct syscall_args *args, register_t *retval) {
    (void) retval;
    return kern_close(t->proc, (int) args->arg1);
}

int sys_read(struct thread *t, struct syscall_args *args, register_t *retval) {
    return kern_read(t->proc, (int) args->arg1, (const void *) args->arg2, (size_t) args->arg3, (ssize_t *) retval);
}

int sys_write(struct thread *t, struct syscall_args *args, register_t *retval) {
    return kern_write(t->proc, (int) args->arg1, (const void *) args->arg2, (size_t) args->arg3, (ssize_t *) retval);
}

int sys_dup(struct thread *t, struct syscall_args *args, register_t *retval) {
    return kern_dup(t->proc, (int) args->arg1, (int *) retval);
}
