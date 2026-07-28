#include <mm/kmalloc.h>
#include <mm/mm_types.h>
#include <mm/slab.h>
#include <nyx/errno.h>
#include <nyx/linkage.h>
#include <nyx/list.h>
#include <nyx/refcount.h>
#include <nyx/string.h>
#include <nyx/vfs.h>

#include <asi/bug.h>

struct uio;

struct vnode *rootvnode;
LIST_HEAD(mountlist);

static kmem_cache_t *vnode_cache;
static kmem_cache_t *mount_cache;

struct vfsconf {
    const char          *vc_name;
    const struct vfsops *vc_ops;
    struct list_head     vc_link;
};
static LIST_HEAD(vfsconf_list);

void __init init_vfs() {
    vnode_cache = kmem_create_cache("vnode", sizeof(struct vnode), _Alignof(struct vnode), NULL, NULL, 0);
    mount_cache = kmem_create_cache("mount", sizeof(struct mount), _Alignof(struct vnode), NULL, NULL, 0);
    BUG_ON(!vnode_cache || !mount_cache);
}

// NOTE: probably temporary
extern void cpiofs_init();
extern void devfs_init();

void __init init_filesystems() {
    cpiofs_init();
    devfs_init();
}
// ENDNOTE

int getnewvnode(struct mount *mp, const struct vnodeops *ops, enum vtype type, struct vnode **vpp) {
    struct vnode *vp = kmem_cache_alloc(vnode_cache, M_SLEEPOK);
    if (!vp) { return ENOMEM; }

    memset(vp, 0, sizeof(*vp));
    vp->v_type        = type;
    vp->v_op          = ops;
    vp->v_mount       = mp;
    vp->v_mountedhere = NULL;
    refcount_set(&vp->v_ref, 1);

    *vpp = vp;
    return 0;
}

void vref(struct vnode *vp) {
    BUG_ON(refcount_get(&vp->v_ref) == 0);
    refcount_inc(&vp->v_ref);
}

static void vgone(struct vnode *vp) {
    BUG_ON(refcount_get(&vp->v_ref) != 0);
    BUG_ON(vp->v_mountedhere != NULL);

    (void) VOP_RECLAIM(vp);
    kmem_cache_free(vnode_cache, vp);
}

void vrele(struct vnode *vp) {
    if (!refcount_dec_and_test(&vp->v_ref)) { return; }
    (void) VOP_INACTIVE(vp);
    vgone(vp);
}

void vput(struct vnode *vp) {
    VOP_UNLOCK(vp);
    vrele(vp);
}

int vfs_register(const char *name, const struct vfsops *ops) {
    BUG_ON(!ops->vfs_mount || !ops->vfs_unmount || !ops->vfs_root || !ops->vfs_statfs);

    struct vfsconf *vc = kmalloc(sizeof(struct vfsconf), M_SLEEPOK);
    if (!vc) { return -ENOMEM; }
    vc->vc_name = name;
    vc->vc_ops  = ops;
    list_add(&vc->vc_link, &vfsconf_list);
    return 0;
}

const struct vfsops *vfs_byname(const char *name) {
    struct vfsconf *vc;
    list_for_each_entry(vc, &vfsconf_list, vc_link) {
        if (strcmp(vc->vc_name, name) == 0) { return vc->vc_ops; }
    }
    return NULL;
}

int do_mount(const char *fsname, struct vnode *covered, void *data, struct mount **mpp) {
    struct mount        *mp;
    const struct vfsops *ops;
    int                  error;

    ops = vfs_byname(fsname);
    if (!ops) { return -ENODEV; }

    if (covered && covered->v_mountedhere) { return -EBUSY; }

    mp = kmem_cache_alloc(mount_cache, M_SLEEPOK);
    if (!mp) { return -ENOMEM; }
    memset(mp, 0, sizeof(*mp));
    mp->mnt_op           = ops;
    mp->mnt_vnodecovered = covered;

    error = VFS_MOUNT(mp, data);
    if (error) {
        kmem_cache_free(mount_cache, mp);
        return error;
    }

    struct vnode *root;
    error = VFS_ROOT(mp, &root);
    if (error) {
        VFS_UNMOUNT(mp, 0);
        kmem_cache_free(mount_cache, mp);
        return error;
    }
    mp->mnt_root = root;

    if (covered) {
        vref(covered);
        covered->v_mountedhere = mp;
    }
    list_add(&mp->mnt_link, &mountlist);

    if (mpp) { *mpp = mp; }
    return 0;
}

int do_unmount(struct mount *mp, int flags) {
    struct vnode *covered = mp->mnt_vnodecovered;
    int           error;

    BUG_ON(mp->mnt_root && refcount_get(&mp->mnt_root->v_ref) != 1);

    if (covered) {
        covered->v_mountedhere = NULL;
        vrele(covered);
    }
    if (mp->mnt_root) { vrele(mp->mnt_root); }

    error = VFS_UNMOUNT(mp, flags);
    if (error) { return error; }

    list_del(&mp->mnt_link);
    kmem_cache_free(mount_cache, mp);
    return 0;
}

int vfs_mountroot(const char *fsname, void *data) {
    struct mount *mp;
    int           error = do_mount(fsname, NULL, data, &mp);
    if (error) { return error; }

    error = VFS_ROOT(mp, &rootvnode);
    return error;
}

int vop_nolookup(struct vnode *v, struct vnode **vpp, struct componentname *c) {
    (void) v;
    (void) vpp;
    (void) c;
    return ENOTDIR;
}
int vop_noopen(struct vnode *v, int m) {
    (void) v;
    (void) m;
    return 0;
}
int vop_noclose(struct vnode *v, int m) {
    (void) v;
    (void) m;
    return 0;
}
int vop_noread(struct vnode *v, struct uio *u, int f) {
    (void) v;
    (void) u;
    (void) f;
    return EISDIR;
}
int vop_nowrite(struct vnode *v, struct uio *u, int f) {
    (void) v;
    (void) u;
    (void) f;
    return EISDIR;
}
int vop_noioctl(struct vnode *v, unsigned long c, void *d) {
    (void) v;
    (void) c;
    (void) d;
    return ENOTTY;
}
int vop_noreaddir(struct vnode *v, struct uio *u) {
    (void) v;
    (void) u;
    return ENOTDIR;
}
int vop_noreadlink(struct vnode *v, struct uio *u) {
    (void) v;
    (void) u;
    return EINVAL;
}
int vop_null(struct vnode *v) {
    (void) v;
    return 0;
}

void vops_check(const struct vnodeops *ops) {
    BUG_ON(!ops->vop_lookup || !ops->vop_open || !ops->vop_close || !ops->vop_read || !ops->vop_write ||
           !ops->vop_ioctl || !ops->vop_getattr || !ops->vop_readdir || !ops->vop_readlink || !ops->vop_inactive ||
           !ops->vop_reclaim);
}
