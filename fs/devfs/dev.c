#include <fs/fsnode.h>
#include <mm/kmalloc.h>
#include <mm/mm_types.h>
#include <nyx/errno.h>
#include <nyx/fcntl.h>
#include <nyx/kernel.h>
#include <nyx/linkage.h>
#include <nyx/string.h>
#include <nyx/vfs.h>

#include <asi/bug.h>

struct dev_node {
    struct fsnode        d_node;
    const struct cdevsw *d_cdev;
    dev_t                d_rdev;
};

#define VTODN(vp) container_of((struct fsnode *) (vp)->v_data, struct dev_node, d_node)

static struct fsnode        *devfs_root;
static u64                   devfs_ino = 2;
static const struct vnodeops devfs_vnodeops;

static struct dev_node *devfs_new(const char *name, size_t namelen, enum vtype type, u16 mode) {
    struct dev_node *dn = kmalloc(sizeof(struct dev_node), M_SLEEPOK);
    if (!dn) { return NULL; }

    memset(dn, 0, sizeof(struct dev_node));
    if (fsnode_init(&dn->d_node, name, namelen, type, &devfs_vnodeops)) {
        kfree(dn);
        return NULL;
    }

    dn->d_node.fn_mode = mode;
    dn->d_node.fn_ino  = devfs_ino++;
    return dn;
}

int cdev_register(const char *name, const struct cdevsw *cdev, dev_t dev, u16 mode) {
    struct fsnode *dir = devfs_root;
    const char    *p   = name;
    const char    *end = name + strlen(name);

    while (p < end) {
        const char      *comp, *q;
        struct fsnode   *child;
        struct dev_node *dn;
        size_t           clen;
        bool             last;

        while (p < end && *p == '/') { p++; }
        if (p >= end) { break; }

        comp = p;
        while (p < end && *p != '/') { p++; }
        clen = (size_t) (p - comp);

        q = p;
        while (q < end && *q == '/') { q++; }
        last = (q >= end);

        child = fsnode_child(dir, comp, clen);
        if (last) {
            if (child) { return -EEXIST; }

            dn = devfs_new(comp, clen, VCHR, mode);
            if (!dn) { return -ENOMEM; }

            dn->d_cdev = cdev;
            dn->d_rdev = dev;
            fsnode_link(dir, &dn->d_node);
            return 0;
        }

        if (!child) {
            dn = devfs_new(comp, clen, VDIR, 0755);
            if (!dn) { return -ENOMEM; }

            fsnode_link(dir, &dn->d_node);
        } else if (child->fn_type != VDIR) {
            return -ENOTDIR;
        }
    }

    return -EINVAL;
}

static int devfs_open(struct vnode *vp, int mode) {
    struct fsnode   *n;
    struct dev_node *sn;

    n = vp->v_data;
    if (n->fn_type == VDIR) { return 0; }
    sn = VTODN(vp);
    return sn->d_cdev->d_open(sn->d_rdev, mode);
}

static int devfs_close(struct vnode *vp, int mode) {
    struct fsnode   *n;
    struct dev_node *sn;

    n = vp->v_data;
    if (n->fn_type == VDIR) { return 0; }
    sn = VTODN(vp);
    return sn->d_cdev->d_close(sn->d_rdev, mode);
}

static int devfs_read(struct vnode *vp, struct uio *uio, int ioflag) {
    (void) ioflag;
    struct fsnode   *n;
    struct dev_node *dn;

    n = vp->v_data;
    if (n->fn_type == VDIR) { return -EISDIR; }
    dn = VTODN(vp);
    return dn->d_cdev->d_read(dn->d_rdev, uio);
}

static int devfs_write(struct vnode *vp, struct uio *uio, int ioflag) {
    (void) ioflag;
    struct fsnode   *n;
    struct dev_node *dn;

    n = vp->v_data;
    if (n->fn_type == VDIR) { return -EISDIR; }
    dn = VTODN(vp);
    return dn->d_cdev->d_write(dn->d_rdev, uio);
}

static int devfs_ioctl(struct vnode *vp, unsigned long cmd, void *data) {
    struct fsnode   *n;
    struct dev_node *dn;

    n = vp->v_data;
    if (n->fn_type == VDIR) { return -ENOTTY; }
    dn = VTODN(vp);
    return dn->d_cdev->d_ioctl(dn->d_rdev, cmd, data);
}

static int devfs_getattr(struct vnode *vp, struct vattr *vap) {
    struct fsnode *n = vp->v_data;

    memset(vap, 0, sizeof(struct vattr));
    vap->va_type   = n->fn_type;
    vap->va_mode   = n->fn_mode;
    vap->va_nlink  = 1;
    vap->va_fileid = n->fn_ino;
    if (n->fn_type == VCHR || n->fn_type == VBLK) { vap->va_rdev = VTODN(vp)->d_rdev; }
    return 0;
}

static const struct vnodeops devfs_vnodeops = {
        .vop_lookup   = fsnode_lookup,
        .vop_open     = devfs_open,
        .vop_close    = devfs_close,
        .vop_read     = devfs_read,
        .vop_write    = devfs_write,
        .vop_ioctl    = devfs_ioctl,
        .vop_getattr  = devfs_getattr,
        .vop_readdir  = fsnode_readdir,
        .vop_readlink = vop_noreadlink,
        .vop_inactive = vop_null,
        .vop_reclaim  = fsnode_reclaim,
};

static int devfs_mount(struct mount *mp, void *data) {
    (void) data;
    mp->mnt_data = devfs_root;
    return 0;
}

static int devfs_unmount(struct mount *mp, int flags) {
    (void) flags;
    mp->mnt_data = NULL;
    return 0;
}

static int devfs_vfsroot(struct mount *mp, struct vnode **vpp) {
    return fsnode_vget(mp, (struct fsnode *) mp->mnt_data, vpp);
}

static int devfs_statfs(struct mount *mp, struct statfs *sbp) {
    (void) mp;
    (void) sbp;
    return 0;
}

static const struct vfsops devfs_vfsops = {
        .vfs_mount   = devfs_mount,
        .vfs_unmount = devfs_unmount,
        .vfs_root    = devfs_vfsroot,
        .vfs_statfs  = devfs_statfs,
};

int cdev_noop(dev_t d, int m) {
    (void) d;
    (void) m;
    return 0;
}

int cdev_notty(dev_t d, unsigned long c, void *a) {
    (void) d;
    (void) c;
    (void) a;
    return -ENOTTY;
}

int cdev_sink(dev_t d, struct uio *u) {
    (void) d;
    u->uio_resid = 0;
    return 0;
}

void __init devfs_init() {
    struct dev_node *root = devfs_new(".", 1, VDIR, 0755);
    BUG_ON(!root);
    root->d_node.fn_ino = 1;
    devfs_root          = &root->d_node;

    vops_check(&devfs_vnodeops);
    BUG_ON(vfs_register("devfs", &devfs_vfsops));
}
