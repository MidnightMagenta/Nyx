#include <fs/fsnode.h>
#include <mm/kmalloc.h>
#include <mm/mm_types.h>
#include <nyx/errno.h>
#include <nyx/list.h>
#include <nyx/string.h>
#include <nyx/types.h>
#include <nyx/uio.h>
#include <nyx/vfs.h>
#include <uapi/posix_types.h>

static inline size_t align8(size_t x) {
    return (x + 7) & ~(size_t) 7;
}

static u8 vtype_to_dtype(enum vtype t) {
    switch (t) {
        case VREG:
            return DT_REG;
        case VDIR:
            return DT_DIR;
        case VLNK:
            return DT_LNK;
        case VCHR:
            return DT_CHR;
        case VBLK:
            return DT_BLK;
        case VFIFO:
            return DT_FIFO;
        case VSOCK:
            return DT_SOCK;
        default:
            return DT_UNKNOWN;
    }
}

int fsnode_init(struct fsnode *fn, const char *name, size_t namelen, enum vtype type, const struct vnodeops *vops) {
    memset(fn, 0, sizeof(*fn));
    fn->fn_name = kmalloc(namelen + 1, M_SLEEPOK);
    if (!fn->fn_name) { return ENOMEM; }

    memcpy(fn->fn_name, name, namelen);
    fn->fn_name[namelen] = '\0';
    fn->fn_namelen       = namelen;
    fn->fn_type          = type;
    fn->fn_vops          = vops;
    list_init(&fn->fn_children);
    list_init(&fn->fn_sibling);

    return 0;
}

struct fsnode *fsnode_child(struct fsnode *dir, const char *name, size_t namelen) {
    struct fsnode *c;

    list_for_each_entry(c, &dir->fn_children, fn_sibling) {
        if (c->fn_namelen == namelen && memcmp(c->fn_name, name, namelen) == 0) { return c; }
    }
    return NULL;
}

void fsnode_link(struct fsnode *dir, struct fsnode *child) {
    child->fn_parent = dir;
    list_add_tail(&child->fn_sibling, &dir->fn_children);
}

int fsnode_vget(struct mount *mp, struct fsnode *n, struct vnode **vpp) {
    int           error;
    struct vnode *vp;

    if (n->fn_vnode) {
        vref(n->fn_vnode);
        *vpp = n->fn_vnode;
        return 0;
    }

    error = getnewvnode(mp, n->fn_vops, n->fn_type, &vp);
    if (error) { return error; }

    vp->v_data  = n;
    n->fn_vnode = vp;
    *vpp        = vp;
    return 0;
}

int fsnode_lookup(struct vnode *dvp, struct vnode **vpp, struct componentname *cnp) {
    struct fsnode *dn = dvp->v_data;
    if (dn->fn_type != VDIR) { return ENOTDIR; }

    struct fsnode *t;
    if (cnp->cn_namelen == 2 && cnp->cn_nameptr[0] == '.' && cnp->cn_nameptr[1] == '.') {
        t = dn->fn_parent ? dn->fn_parent : dn;
    } else if (cnp->cn_namelen == 1 && cnp->cn_nameptr[0] == '.') {
        t = dn;
    } else {
        t = fsnode_child(dn, cnp->cn_nameptr, cnp->cn_namelen);
        if (!t) { return ENOENT; }
    }
    return fsnode_vget(dvp->v_mount, t, vpp);
}

static int emit_dirent(struct uio *uio, u64 ino, u8 type, const char *name, size_t namlen) {
    union {
        struct dirent de;
        char          raw[sizeof(struct dirent) + NAME_MAX + 1];
        u64           align;
    } u;
    size_t reclen = align8(offsetof(struct dirent, d_name) + namlen + 1);

    if (reclen > uio->uio_resid) { return 0; }
    memset(&u, 0, reclen);
    u.de.d_fileno = ino;
    u.de.d_reclen = (u16) reclen;
    u.de.d_type   = type;
    u.de.d_namlen = (u8) namlen;
    memcpy(u.de.d_name, name, namlen);

    int e = uiomove(&u, reclen, uio);
    return e ? -e : (int) reclen;
}

int fsnode_readdir(struct vnode *vp, struct uio *uio) {
    struct fsnode *dn = vp->v_data;
    if (dn->fn_type != VDIR) { return ENOTDIR; }

    off_t          target = uio->uio_offset;
    off_t          i      = 0;
    int            r;
    struct fsnode *parent = dn->fn_parent ? dn->fn_parent : dn;
    struct fsnode *c;

    if (i >= target) {
        r = emit_dirent(uio, dn->fn_ino, DT_DIR, ".", 1);
        if (r == 0) { goto out; }
        if (r < 0) { return -r; }
    }
    i++;
    if (i >= target) {
        r = emit_dirent(uio, parent->fn_ino, DT_DIR, "..", 2);
        if (r == 0) { goto out; }
        if (r < 0) { return -r; }
    }
    i++;

    list_for_each_entry(c, &dn->fn_children, fn_sibling) {
        if (i >= target) {
            r = emit_dirent(uio, c->fn_ino, vtype_to_dtype(c->fn_type), c->fn_name, c->fn_namelen);
            if (r == 0) { goto out; }
            if (r < 0) { return -r; }
        }
        i++;
    }

out:
    uio->uio_offset = i;
    return 0;
}

int fsnode_reclaim(struct vnode *vp) {
    struct fsnode *n = vp->v_data;
    if (n) { n->fn_vnode = NULL; }
    vp->v_data = NULL;
    return 0;
}
