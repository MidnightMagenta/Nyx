#ifndef _NYX_VFS_H
#define _NYX_VFS_H

#include <nyx/list.h>
#include <nyx/refcount.h>
#include <nyx/spinlock.h>
#include <nyx/stddef.h>
#include <nyx/uio.h>
#include <uapi/posix_types.h>

#define MAXPATH     4096
#define NAME_MAX    255
#define MAXSYMLINKS 32

enum vtype { VNON, VREG, VDIR, VBLK, VCHR, VLNK, VFIFO, VSOCK };

struct vnode;
struct mount;
struct file;
struct vnodeops;
struct vfsops;
struct fileops;
struct componentname;
struct statfs;

struct vattr {
    enum vtype va_type;
    u16        va_mode;
    u32        va_nlink;
    u32        va_uid;
    u32        va_gid;
    u64        va_fileid;
    off_t      va_size;
    u32        va_rdev;
};

typedef u32 dev_t;
#define makedev(maj, min) ((dev_t) (((maj) << 20) | ((min) & 0xfffff)))
#define major(d)          ((uint32_t) ((d) >> 20))
#define minor(d)          ((uint32_t) ((d) & 0xfffff))

struct vnode {
    enum vtype             v_type;
    const struct vnodeops *v_op;
    void                  *v_data;
    struct mount          *v_mount;
    struct mount          *v_mountedhere;
    struct refcount        v_ref;
    u32                    v_lockstate;
};

struct mount {
    const struct vfsops *mnt_op;
    void                *mnt_data;
    struct vnode        *mnt_root;
    struct vnode        *mnt_vnodecovered;

    struct list_head mnt_link;
};

struct vfsops {
    int (*vfs_mount)(struct mount *mp, void *data);
    int (*vfs_unmount)(struct mount *mp, int flags);
    int (*vfs_root)(struct mount *mp, struct vnode **vpp);
    int (*vfs_statfs)(struct mount *mp, struct statfs *sbp);
};

struct vnodeops {
    int (*vop_lookup)(struct vnode *dvp, struct vnode **vpp, struct componentname *cnp);
    int (*vop_open)(struct vnode *vp, int mode);
    int (*vop_close)(struct vnode *vp, int mode);
    int (*vop_read)(struct vnode *vp, struct uio *uio, int ioflag);
    int (*vop_write)(struct vnode *vp, struct uio *uio, int ioflag);
    int (*vop_ioctl)(struct vnode *vp, unsigned long cmd, void *data);
    int (*vop_getattr)(struct vnode *vp, struct vattr *vap);
    int (*vop_readdir)(struct vnode *vp, struct uio *uio);
    int (*vop_readlink)(struct vnode *vp, struct uio *uio);
    int (*vop_inactive)(struct vnode *vp);
    int (*vop_reclaim)(struct vnode *vp);
};

struct fileops {
    int (*fo_read)(struct file *fp, struct uio *uio, int flags);
    int (*fo_write)(struct file *fp, struct uio *uio, int flags);
    int (*fo_ioctl)(struct file *fp, unsigned long cmd, void *data);
    int (*fo_stat)(struct file *fp, struct vattr *vap);
    int (*fo_close)(struct file *fp);
};

struct file {
    const struct fileops *f_ops;
    void                 *f_data;
    off_t                 f_offset;
    int                   f_flags;
    struct refcount       f_count;
};

struct fdentry {
    struct file *fe_file;
    int          fe_flags;
};

#define FD_CLOEXEC 1

struct filedesc {
    struct fdentry *fd_files;
    int             fd_nfiles;
    struct vnode   *fd_cdir;
    struct vnode   *fd_rdir;
    spinlock_t      fd_lock;
};

struct cdevsw {
    const char *d_name;
    int         (*d_open)(dev_t dev, int mode);
    int         (*d_close)(dev_t dev, int mode);
    int         (*d_read)(dev_t dev, struct uio *uio);
    int         (*d_write)(dev_t dev, struct uio *uio);
    int         (*d_ioctl)(dev_t dev, unsigned long cmd, void *data);
};

enum { NAMEI_LOOKUP, NAMEI_CREATE, NAMEI_DELETE, NAMEI_RENAME };

#define NAMEI_FOLLOW     0x0001
#define NAMEI_ISLASTCN   0x0002
#define NAMEI_WANTPARENT 0x0004

struct componentname {
    int         cn_nameiop;
    u32         cn_flags;
    const char *cn_nameptr;
    size_t      cn_namelen;
};

struct nameidata {
    // inputs
    const char     *ni_dirp;
    enum uio_seg    ni_segflg;
    int             ni_op;
    u32             ni_flags;
    struct process *ni_proc;

    // outputs
    struct vnode *ni_vp;
    struct vnode *ni_dvp;

    // scratchpad
    struct componentname ni_cnd;
    char                *ni_pathbuf;
    int                  ni_loopcnt;
};

static inline int VFS_MOUNT(struct mount *mp, void *d) {
    return mp->mnt_op->vfs_mount(mp, d);
}
static inline int VFS_UNMOUNT(struct mount *mp, int f) {
    return mp->mnt_op->vfs_unmount(mp, f);
}
static inline int VFS_ROOT(struct mount *mp, struct vnode **vpp) {
    return mp->mnt_op->vfs_root(mp, vpp);
}
static inline int VFS_STATFS(struct mount *mp, struct statfs *s) {
    return mp->mnt_op->vfs_statfs(mp, s);
}

static inline int VOP_LOOKUP(struct vnode *d, struct vnode **vpp, struct componentname *c) {
    return d->v_op->vop_lookup(d, vpp, c);
}
static inline int VOP_OPEN(struct vnode *v, int m) {
    return v->v_op->vop_open(v, m);
}
static inline int VOP_CLOSE(struct vnode *v, int m) {
    return v->v_op->vop_close(v, m);
}
static inline int VOP_READ(struct vnode *v, struct uio *u, int f) {
    return v->v_op->vop_read(v, u, f);
}
static inline int VOP_WRITE(struct vnode *v, struct uio *u, int f) {
    return v->v_op->vop_write(v, u, f);
}
static inline int VOP_IOCTL(struct vnode *v, unsigned long c, void *d) {
    return v->v_op->vop_ioctl(v, c, d);
}
static inline int VOP_GETATTR(struct vnode *v, struct vattr *a) {
    return v->v_op->vop_getattr(v, a);
}
static inline int VOP_READDIR(struct vnode *v, struct uio *u) {
    return v->v_op->vop_readdir(v, u);
}
static inline int VOP_READLINK(struct vnode *v, struct uio *u) {
    return v->v_op->vop_readlink(v, u);
}
static inline int VOP_INACTIVE(struct vnode *v) {
    return v->v_op->vop_inactive(v);
}
static inline int VOP_RECLAIM(struct vnode *v) {
    return v->v_op->vop_reclaim(v);
}

static inline int vn_lock(struct vnode *vp, int flags) {
    (void) vp;
    (void) flags;
    return 0;
}
static inline void VOP_UNLOCK(struct vnode *vp) {
    (void) vp;
}
#define LK_EXCLUSIVE 0x1

int  getnewvnode(struct mount *mp, const struct vnodeops *ops, enum vtype type, struct vnode **vpp);
void vref(struct vnode *vp);
void vrele(struct vnode *vp);
void vput(struct vnode *vp);

int                  vfs_register(const char *name, const struct vfsops *ops);
const struct vfsops *vfs_byname(const char *name);
int                  do_mount(const char *fsname, struct vnode *covered, void *data, struct mount **mpp);
int                  do_unmount(struct mount *mp, int flags);
int                  vfs_mountroot(const char *fsname, void *data);

void vops_check(const struct vnodeops *ops);

int vop_nolookup(struct vnode *, struct vnode **, struct componentname *);
int vop_noopen(struct vnode *, int);
int vop_noclose(struct vnode *, int);
int vop_noread(struct vnode *, struct uio *, int);
int vop_nowrite(struct vnode *, struct uio *, int);
int vop_noioctl(struct vnode *, unsigned long, void *);
int vop_noreaddir(struct vnode *, struct uio *);
int vop_noreadlink(struct vnode *, struct uio *);
int vop_null(struct vnode *);

int namei(struct nameidata *ndp);

int              fdinit(struct process *p);
struct filedesc *fdcopy(struct filedesc *src);
void             fdfree(struct process *p);
int              fdalloc(struct process *p, int min, int *fdout);
int              falloc(struct process *p, struct file **fpout, int *fdout);
struct file     *fget(struct process *p, int fd);
void             fhold(struct file *fp);
void             fdrop(struct file *fp);
int              fd_close(struct process *p, int fd);

int vn_open(struct nameidata *ndp, int fflags, u16 mode);
int vn_rdwr(enum uio_rw rw, struct vnode *vp, void *buf, size_t len, off_t off, size_t *residp);
int vfs_open(struct process *p, const char *path, enum uio_seg seg, int flags, u16 mode, int *fdout);

int kern_open(struct process *p, const char *path, int flags, u16 mode, int *fdout);
int kern_close(struct process *p, int fd);
int kern_read(struct process *p, int fd, const void *ubuf, size_t count, ssize_t *nread);
int kern_write(struct process *p, int fd, const void *ubuf, size_t count, ssize_t *nwritten);
int kern_dup(struct process *p, int oldfd, int *newfd);

extern const struct fileops vnops;

extern struct vnode    *rootvnode;
extern struct list_head mountlist;

#endif
