#include <fs/cpio.h>
#include <fs/fsnode.h>
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

struct cpio_node {
    struct fsnode c_node;
    const u8     *n_data;
    size_t        n_size;
};
#define VTOCN(vp) container_of((struct fsnode *) (vp)->v_data, struct cpio_node, c_node)

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

static struct cpio_node *cpio_node_alloc(const char *name, size_t namelen, enum vtype type) {
    struct cpio_node *cn = kmalloc(sizeof(struct cpio_node), M_SLEEPOK);
    if (!cn) { return NULL; }

    memset(cn, 0, sizeof(struct cpio_node));
    if (fsnode_init(&cn->c_node, name, namelen, type, &cpio_vnodeops) != 0) {
        kfree(cn);
        return NULL;
    }
    return cn;
}

static int cpio_insert(struct cpio_mount *cm,
                       const char        *path,
                       size_t             pathlen,
                       enum vtype         type,
                       u16                mode,
                       u64                ino,
                       const u8          *data,
                       size_t             size) {
    struct fsnode *dir = &cm->cm_root->c_node;
    const char    *p   = path;
    const char    *end = path + pathlen;

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

        struct fsnode *child = fsnode_child(dir, comp, clen);
        if (!child) {
            struct cpio_node *cn = cpio_node_alloc(comp, clen, last ? type : VDIR);
            if (!cn) { return ENOMEM; }

            cn->c_node.fn_ino  = last ? ino : cm->cm_nextino++;
            cn->c_node.fn_mode = last ? mode : 0755;
            if (last) {
                cn->n_data = data;
                cn->n_size = size;
            }
            fsnode_link(dir, &cn->c_node);
            child = &cn->c_node;
        } else if (last) {
            struct cpio_node *cn = container_of(child, struct cpio_node, c_node);
            child->fn_type       = type;
            child->fn_mode       = mode;
            child->fn_ino        = ino;
            cn->n_data           = data;
            cn->n_size           = size;
        }

        if (last) { return 0; }
        if (child->fn_type != VDIR) { return ENOTDIR; }
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
            cm->cm_root->c_node.fn_mode = mode & 07777;
        } else {
            e = cpio_insert(cm, name, nlen, mode_to_vtype(mode), mode & 07777, cm->cm_nextino++, data, filesize);
            if (e) { return e; }
        }

        off = align4(data_off + filesize);
    }
    return 0;
}

static void cpio_free_tree(struct cpio_node *cn) {
    struct fsnode *c, *tmp;
    list_for_each_entry_safe(c, tmp, &cn->c_node.fn_children, fn_sibling) {
        cpio_free_tree(container_of(c, struct cpio_node, c_node));
    }

    kfree(cn->c_node.fn_name);
    kfree(cn);
}

static int cpio_open(struct vnode *vp, int mode) {
    (void) vp;
    if ((mode & O_ACCMODE) != O_RDONLY) { return EROFS; }
    return 0;
}

static int cpio_read(struct vnode *vp, struct uio *uio, int ioflag) {
    struct cpio_node *cn = VTOCN(vp);
    (void) ioflag;

    if (vp->v_type == VDIR) { return EISDIR; }
    if (vp->v_type != VREG && vp->v_type != VLNK) { return EINVAL; }
    if (uio->uio_offset < 0) { return EINVAL; }
    if ((size_t) uio->uio_offset >= cn->n_size) { return 0; }

    size_t avail = cn->n_size - (size_t) uio->uio_offset;
    return uiomove((void *) (cn->n_data + uio->uio_offset), avail, uio);
}

static int cpio_write(struct vnode *vp, struct uio *uio, int ioflag) {
    (void) vp;
    (void) uio;
    (void) ioflag;
    return EROFS;
}

static int cpio_getattr(struct vnode *vp, struct vattr *vap) {
    struct cpio_node *cn = VTOCN(vp);

    memset(vap, 0, sizeof(*vap));
    vap->va_type   = cn->c_node.fn_type;
    vap->va_mode   = cn->c_node.fn_mode;
    vap->va_nlink  = 1;
    vap->va_fileid = cn->c_node.fn_ino;
    vap->va_size   = (cn->c_node.fn_type == VDIR) ? 0 : (off_t) cn->n_size;

    return 0;
}

static int cpio_readlink(struct vnode *vp, struct uio *uio) {
    struct cpio_node *cn = VTOCN(vp);
    if (vp->v_type != VLNK) { return EINVAL; }
    return uiomove((void *) cn->n_data, cn->n_size, uio);
}

static const struct vnodeops cpio_vnodeops = {
        .vop_lookup   = fsnode_lookup,
        .vop_open     = cpio_open,
        .vop_close    = vop_noclose,
        .vop_read     = cpio_read,
        .vop_write    = cpio_write,
        .vop_ioctl    = vop_noioctl,
        .vop_getattr  = cpio_getattr,
        .vop_readdir  = fsnode_readdir,
        .vop_readlink = cpio_readlink,
        .vop_inactive = vop_null,
        .vop_reclaim  = fsnode_reclaim,
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

    cm->cm_root = cpio_node_alloc(".", 1, VDIR);
    if (!cm->cm_root) {
        kfree(cm);
        return ENOMEM;
    }
    cm->cm_root->c_node.fn_ino  = 1;
    cm->cm_root->c_node.fn_mode = 0755;

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
    return fsnode_vget(mp, &cm->cm_root->c_node, vpp);
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
