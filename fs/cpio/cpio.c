#include <fs/cpio.h>
#include <mm/kmalloc.h>
#include <mm/mm_types.h>
#include <nyx/errno.h>
#include <nyx/fcntl.h>
#include <nyx/linkage.h>
#include <nyx/stddef.h>
#include <nyx/string.h>
#include <nyx/types.h>
#include <nyx/vfs.h>

#include <asi/bug.h>

struct dirent {
    u64  d_fileno;
    u16  d_reclen;
    u8   d_type;
    u8   d_namlen;
    char d_name[];
};
enum { DT_UNKNOWN = 0, DT_FIFO = 1, DT_CHR = 2, DT_DIR = 4, DT_BLK = 6, DT_REG = 8, DT_LNK = 10, DT_SOCK = 12 };

struct cpio_node {
    char             *n_name;
    size_t            n_namelen;
    enum vtype        n_type;
    u16               n_mode;
    u64               n_ino;
    const u8         *n_data;
    size_t            n_size;
    struct cpio_node *n_parent;
    struct list_head  n_children;
    struct list_head  n_sibling;
    struct vnode     *n_vnode;
};

struct cpio_mount {
    const u8         *cm_base;
    size_t            cm_len;
    struct cpio_node *cm_root;
    u64               cm_nextino;
};

static const struct vnodeops cpio_vnodeops;

static inline size_t align4(size_t x) {
    return (x + 3) & ~(size_t) 3;
}
static inline size_t align8(size_t x) {
    return (x + 7) & ~(size_t) 7;
}

static u32 hex8(const char *p) {
    u32 v = 0;
    for (int i = 0; i < 8; i++) {
        u8 c = (u8) p[i];
        v <<= 4;
        if (c >= '0' && c <= '9') {
            v |= (u32) (c - '0');
        } else if (c >= 'a' && c <= 'f') {
            v |= (u32) (c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            v |= (u32) (c - 'A' + 10);
        }
    }
    return v;
}

static enum vtype mode_to_vtype(u32 mode) {
    switch (mode & 0170000) {
        case 0040000:
            return VDIR;
        case 0100000:
            return VREG;
        case 0120000:
            return VLNK;
        case 0020000:
            return VCHR;
        case 0060000:
            return VBLK;
        case 0010000:
            return VFIFO;
        case 0140000:
            return VSOCK;
        default:
            return VREG;
    }
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

static struct cpio_node *node_alloc(const char *name, size_t namelen, enum vtype type) {
    struct cpio_node *n = kmalloc(sizeof(struct cpio_node), M_SLEEPOK);
    if (!n) { return NULL; }

    memset(n, 0, sizeof(struct cpio_node));
    n->n_name = kmalloc(namelen + 1, M_SLEEPOK);
    if (!n->n_name) {
        kfree(n);
        return NULL;
    }

    memcpy(n->n_name, name, namelen);
    n->n_name[namelen] = '\0';
    n->n_namelen       = namelen;
    list_init(&n->n_children);
    list_init(&n->n_sibling);
    n->n_type = type;

    return n;
}

static struct cpio_node *dir_find(struct cpio_node *dir, const char *name, size_t namelen) {
    struct cpio_node *c;

    list_for_each_entry(c, &dir->n_children, n_sibling) {
        if (c->n_namelen == namelen && memcmp(c->n_name, name, namelen) == 0) { return c; }
    }
    return NULL;
}

static void dir_link(struct cpio_node *dir, struct cpio_node *child) {
    child->n_parent = dir;
    list_add_tail(&child->n_sibling, &dir->n_children);
}

static int cpio_insert(struct cpio_mount *cm,
                       const char        *path,
                       size_t             pathlen,
                       enum vtype         type,
                       u16                mode,
                       u64                ino,
                       const u8          *data,
                       size_t             size) {
    struct cpio_node *dir = cm->cm_root;
    const char       *p   = path;
    const char       *end = path + pathlen;

    while (p < end) {
        const char *comp, *q;
        size_t      clen;
        bool        last;

        while (p < end && *p == '/') { p++; }
        if (p >= end) { break; }

        comp = p;
        while (p < end && *p != '/') { p++; }
        clen = (size_t) (p - comp);

        q = p;
        while (q < end && *q == '/') { q++; }
        last = (q >= end);

        if (clen == 1 && comp[0] == '.') { continue; }

        struct cpio_node *child = dir_find(dir, comp, clen);
        if (!child) {
            child = node_alloc(comp, clen, last ? type : VDIR);
            if (!child) { return ENOMEM; }

            child->n_ino  = last ? ino : cm->cm_nextino++;
            child->n_mode = last ? mode : 0755;

            if (last) {
                child->n_data = data;
                child->n_size = size;
            }
            dir_link(dir, child);
        } else if (last) {
            child->n_type = type;
            child->n_mode = mode;
            child->n_ino  = ino;
            child->n_data = data;
            child->n_size = size;
        }

        if (last) { return 0; }
        if (child->n_type != VDIR) { return ENOTDIR; }
        dir = child;
    }
    return 0;
}

static int cpio_parse(struct cpio_mount *cm) {
    const u8 *base = cm->cm_base;
    size_t    len  = cm->cm_len;
    size_t    off  = 0;

    for (;;) {
        int         e;
        const char *h, *name;
        u32         mode, filesize, namesize;
        size_t      name_off, nlen, data_off;
        const u8   *data;

        if (off + 110 > len) { return EINVAL; }
        h = (const char *) (base + off);
        if (memcmp(h, "070701", 6) != 0 && memcmp(h, "070702", 6) != 0) { return EINVAL; }

        mode     = hex8(h + 14);
        filesize = hex8(h + 54);
        namesize = hex8(h + 94);

        name_off = off + 110;
        if (name_off + namesize > len || namesize == 0) { return EINVAL; }
        name = (const char *) (base + name_off);
        if (name[namesize - 1] != '\0') { return EINVAL; }
        nlen = namesize - 1;

        if (nlen == 10 && memcmp(name, "TRAILER!!!", 10) == 0) { break; }

        data_off = align4(name_off + namesize);
        if (data_off + filesize > len) { return EINVAL; }
        data = base + data_off;

        if (nlen == 1 && name[0] == '.') {
            cm->cm_root->n_mode = mode & 07777;
        } else {
            e = cpio_insert(cm, name, nlen, mode_to_vtype(mode), mode & 07777, cm->cm_nextino++, data, filesize);
            if (e) { return e; }
        }

        off = align4(data_off + filesize);
    }
    return 0;
}

static void cpio_free_tree(struct cpio_node *n) {
    struct cpio_node *c, *tmp;
    list_for_each_entry_safe(c, tmp, &n->n_children, n_sibling) {
        cpio_free_tree(c);
    }

    kfree(n->n_name);
    kfree(n);
}

static int cpio_vget(struct mount *mp, struct cpio_node *n, struct vnode **vpp) {
    int           error;
    struct vnode *vp;

    if (n->n_vnode) {
        vref(n->n_vnode);
        *vpp = n->n_vnode;
        return 0;
    }

    error = getnewvnode(mp, &cpio_vnodeops, n->n_type, &vp);
    if (error) { return error; }

    vp->v_data = n;
    n->n_vnode = vp;
    *vpp       = vp;
    return 0;
}

static int cpio_lookup(struct vnode *dvp, struct vnode **vpp, struct componentname *cnp) {
    struct cpio_node *dn = dvp->v_data;
    if (dn->n_type != VDIR) { return ENOTDIR; }

    struct cpio_node *target;
    if (cnp->cn_namelen == 2 && cnp->cn_nameptr[0] == '.' && cnp->cn_nameptr[1] == '.') {
        target = dn->n_parent ? dn->n_parent : dn;
    } else if (cnp->cn_namelen == 1 && cnp->cn_nameptr[0] == '.') {
        target = dn;
    } else {
        target = dir_find(dn, cnp->cn_nameptr, cnp->cn_namelen);
        if (!target) { return ENOENT; }
    }
    return cpio_vget(dvp->v_mount, target, vpp);
}

static int cpio_open(struct vnode *vp, int mode) {
    (void) vp;
    if ((mode & O_ACCMODE) != O_RDONLY) { return EROFS; }
    return 0;
}

static int cpio_read(struct vnode *vp, struct uio *uio, int ioflag) {
    struct cpio_node *n = vp->v_data;
    (void) ioflag;

    if (vp->v_type == VDIR) { return EISDIR; }
    if (vp->v_type != VREG && vp->v_type != VLNK) { return EINVAL; }
    if (uio->uio_offset < 0) { return EINVAL; }
    if ((size_t) uio->uio_offset >= n->n_size) { return 0; }

    size_t avail = n->n_size - (size_t) uio->uio_offset;
    return uiomove((void *) (n->n_data + uio->uio_offset), avail, uio);
}

static int cpio_write(struct vnode *vp, struct uio *uio, int ioflag) {
    (void) vp;
    (void) uio;
    (void) ioflag;
    return EROFS;
}

static int cpio_getattr(struct vnode *vp, struct vattr *vap) {
    struct cpio_node *n = vp->v_data;

    memset(vap, 0, sizeof(*vap));
    vap->va_type   = n->n_type;
    vap->va_mode   = n->n_mode;
    vap->va_nlink  = 1;
    vap->va_fileid = n->n_ino;
    vap->va_size   = (n->n_type == VDIR) ? 0 : (off_t) n->n_size;

    return 0;
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

static int cpio_readdir(struct vnode *vp, struct uio *uio) {
    struct cpio_node *dn = vp->v_data;
    if (dn->n_type != VDIR) return ENOTDIR;

    off_t             target = uio->uio_offset;
    off_t             i      = 0;
    int               r;
    struct cpio_node *parent = dn->n_parent ? dn->n_parent : dn;
    struct cpio_node *c;

    if (i >= target) {
        r = emit_dirent(uio, dn->n_ino, DT_DIR, ".", 1);
        if (r == 0) { goto out; }
        if (r < 0) { return -r; }
    }
    i++;
    if (i >= target) {
        r = emit_dirent(uio, parent->n_ino, DT_DIR, "..", 2);
        if (r == 0) { goto out; }
        if (r < 0) { return -r; }
    }
    i++;

    list_for_each_entry(c, &dn->n_children, n_sibling) {
        if (i >= target) {
            r = emit_dirent(uio, c->n_ino, vtype_to_dtype(c->n_type), c->n_name, c->n_namelen);
            if (r == 0) goto out;
            if (r < 0) return -r;
        }
        i++;
    }

out:
    uio->uio_offset = i;
    return 0;
}

static int cpio_readlink(struct vnode *vp, struct uio *uio) {
    struct cpio_node *n = vp->v_data;
    if (vp->v_type != VLNK) { return EINVAL; }
    return uiomove((void *) n->n_data, n->n_size, uio);
}

static int cpio_reclaim(struct vnode *vp) {
    struct cpio_node *n = vp->v_data;
    if (n) { n->n_vnode = NULL; }
    vp->v_data = NULL;
    return 0;
}

static const struct vnodeops cpio_vnodeops = {
        .vop_lookup   = cpio_lookup,
        .vop_open     = cpio_open,
        .vop_close    = vop_noclose,
        .vop_read     = cpio_read,
        .vop_write    = cpio_write,
        .vop_ioctl    = vop_noioctl,
        .vop_getattr  = cpio_getattr,
        .vop_readdir  = cpio_readdir,
        .vop_readlink = cpio_readlink,
        .vop_inactive = vop_null,
        .vop_reclaim  = cpio_reclaim,
};

static int cpio_mount(struct mount *mp, void *data) {
    struct cpio_args *args = data;
    if (!args || !args->base || args->len < 110) { return EINVAL; }

    struct cpio_mount *cm = kmalloc(sizeof(*cm), M_SLEEPOK);
    if (!cm) { return ENOMEM; }

    memset(cm, 0, sizeof(*cm));
    cm->cm_base    = (const u8 *) args->base;
    cm->cm_len     = args->len;
    cm->cm_nextino = 2;

    cm->cm_root = node_alloc(".", 1, VDIR);
    if (!cm->cm_root) {
        kfree(cm);
        return ENOMEM;
    }
    cm->cm_root->n_ino  = 1;
    cm->cm_root->n_mode = 0755;

    int e = cpio_parse(cm);
    if (e) {
        cpio_free_tree(cm->cm_root);
        kfree(cm);
        return e;
    }
    mp->mnt_data = cm;

    return 0;
}

static int cpio_unmount(struct mount *mp, int flags) {
    struct cpio_mount *cm = mp->mnt_data;
    (void) flags;

    cpio_free_tree(cm->cm_root);
    kfree(cm);
    mp->mnt_data = NULL;

    return 0;
}

static int cpio_root(struct mount *mp, struct vnode **vpp) {
    struct cpio_mount *cm = mp->mnt_data;
    return cpio_vget(mp, cm->cm_root, vpp);
}

static int cpio_statfs(struct mount *mp, struct statfs *sbp) {
    (void) mp;
    (void) sbp;
    return 0;
}

static const struct vfsops cpio_vfsops = {
        .vfs_mount   = cpio_mount,
        .vfs_unmount = cpio_unmount,
        .vfs_root    = cpio_root,
        .vfs_statfs  = cpio_statfs,
};

void __init cpiofs_init(void) {
    vops_check(&cpio_vnodeops);
    int e = vfs_register("cpio", &cpio_vfsops);
    BUG_ON(e);
}
