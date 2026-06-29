#include "cpio.h"
#include <mm/kmalloc.h>
#include <mm/mm_types.h>
#include <mm/slab.h>
#include <nyx/align.h>
#include <nyx/errno.h>
#include <nyx/limits.h>
#include <nyx/string.h>
#include <nyx/types.h>
#include <nyx/vfs.h>

int     cpio_lookup(struct vnode *dir, const char *name, struct vnode **out);
ssize_t cpio_read(struct file *f, void *buf, size_t len);
off_t   cpio_seek(struct file *f, off_t off, int whence);

static kmem_cache_t *cpiovn_cache;
static struct vops   cpiofs_vops = {
        cpio_lookup,
        cpio_read,
        NULL,
        cpio_seek,
        NULL,
};

static struct vfsops cpiofs_vfsops = {
        NULL,
};

int cpiofs_mount(struct fs_type *fs, void *, struct vfs **out);

static struct fs_type cpiofs_type = {
        .name  = "cpiofs",
        .mount = cpiofs_mount,
        .node  = LIST_HEAD_INIT(cpiofs_type.node),
};

void cpio_init() {
    cpiovn_cache = kmem_create_cache("cpiov", sizeof(struct cpio_node), _Alignof(struct cpio_node), NULL, NULL, 0);
    vfs_register_fs(&cpiofs_type);
}

static struct vnode *cpio_make_vnode(struct vfs *v, enum vtype t) {
    struct vnode     *vn;
    struct cpio_node *cn;

    vn = vnode_alloc(M_SLEEPOK);
    if (!vn) { return NULL; }
    cn = kmem_cache_alloc(cpiovn_cache, M_SLEEPOK);
    if (!cn) {
        vnode_free(vn);
        return NULL;
    }

    vget(vn);

    vn->data  = cn;
    cn->vnode = vn;

    vn->type    = t;
    vn->ops     = &cpiofs_vops;
    vn->vfs     = v;
    vn->mounted = NULL;
    refcount_init(&vn->refs, 1);

    cn->name[0] = '\0';
    cn->data    = NULL;
    cn->size    = 0;
    cn->parent  = NULL;
    list_init(&cn->children);
    list_init(&cn->sibling);
    list_init(&cn->all);
    list_add_tail(&cn->all, &((struct cpiofs_sb *) v->data)->all_nodes);

    return vn;
}

static struct cpiofs_sb *cpiofs_mksb(void *arg) {
    struct cpiofs_sb *sb = kmalloc(sizeof(struct cpiofs_sb), M_SLEEPOK);
    if (!sb) { return NULL; }

    sb->image = arg;
    list_init(&sb->all_nodes);

    return sb;
}

static inline unsigned int hex8(const char *v) {
    char rdbuf[9];
    memcpy(rdbuf, v, 8);
    rdbuf[8] = 0;

    return strtou(rdbuf, NULL, 16);
}

static const char *next_comp(const char *path, char *comp) {
    while (*path == '/') { path++; }

    if (*path == '\0') { return NULL; }

    size_t i = 0;
    while (*path != '/' && *path != '\0') {
        if (i < NAME_MAX) { comp[i++] = *path; }
        path++;
    }
    comp[i] = '\0';

    return path;
}

static inline struct vnode *dir_find(struct vnode *dir, const char *comp) {
    struct cpio_node *cn = (struct cpio_node *) dir->data;
    struct cpio_node *child;
    struct list_head *cur;

    list_for_each(cur, &cn->children) {
        child = list_entry(cur, struct cpio_node, sibling);
        if (strcmp(child->name, comp) == 0) { return child->vnode; }
    }

    return NULL;
}

static inline struct vnode *make_dir(struct vnode *dir, const char *comp) {
    struct vnode     *vn;
    struct cpio_node *cn, *dcn;

    vn = cpio_make_vnode(dir->vfs, VDIR);
    if (!vn) { return NULL; }

    cn  = (struct cpio_node *) vn->data;
    dcn = (struct cpio_node *) dir->data;

    list_add_tail(&cn->sibling, &dcn->children);
    cn->parent = dcn;
    strncpy(cn->name, comp, NAME_MAX);
    cn->name[NAME_MAX] = '\0';

    return vn;
}

static struct vnode *make_file(struct vnode *dir, const char *comp, void *data, size_t size) {
    struct vnode     *vn;
    struct cpio_node *cn, *dcn;

    vn = cpio_make_vnode(dir->vfs, VREG);
    if (!vn) { return NULL; }

    cn  = (struct cpio_node *) vn->data;
    dcn = (struct cpio_node *) dir->data;

    list_add_tail(&cn->sibling, &dcn->children);
    cn->parent = dcn;
    cn->data   = data;
    cn->size   = size;
    strncpy(cn->name, comp, NAME_MAX);
    cn->name[NAME_MAX] = '\0';

    return vn;
}

static int cpiofs_rdinodes(struct vnode *root, void *blob) {
    struct cpio_header *hdr;
    char               *p = blob;
    const char         *name, *slash, *leaf, *rest;
    char               *data, *next;
    char                comp[NAME_MAX + 1];
    u32                 namesz, mode, filesz;
    struct vnode       *dir, *child;

    for (;;) {
        hdr = (void *) p;
        if (strncmp(hdr->c_mag, CPIO_MAGIC, 6) != 0) { return -EFAULT; }

        namesz = hex8(hdr->c_namesz);
        filesz = hex8(hdr->c_filesz);
        name   = (char *) (p + sizeof(struct cpio_header));

        data = (char *) ALIGN_UP((uintptr_t) (name + namesz), 4);
        next = (char *) ALIGN_UP((uintptr_t) (data + filesz), 4);

        if (namesz == 2 && name[0] == '.') { goto advance; } // root node. Skip
        if (namesz == 11 && strcmp(name, CPIO_END_FILENAME) == 0) { break; }

        slash = strrchr(name, '/');
        leaf  = slash ? slash + 1 : name;

        dir  = root;
        rest = name;
        while (rest < (slash ? slash : name)) {
            rest = next_comp(rest, comp);
            if (!rest) { break; }
            child = dir_find(dir, comp);
            if (!child) { child = make_dir(dir, comp); }
            if (!child) { return -ENOMEM; }
            dir = child;
        }

        mode = hex8(hdr->c_mode);
        switch (mode & CPIO_MODE_FT_MASK) {
            case CPIO_MODE_DIR:
                if (!dir_find(dir, leaf)) {
                    child = make_dir(dir, leaf);
                    if (!child) { return -ENOMEM; }
                }
                break;
            case CPIO_MODE_REGFILE:
                child = make_file(dir, leaf, data, filesz);
                if (!child) { return -ENOMEM; }
                break;
            default:
                return -EFAULT;
        }

    advance:
        p = next;
    }

    return 0;
}

int cpiofs_mount(struct fs_type *fs, void *arg, struct vfs **out) {
    struct vfs   *v;
    struct vnode *root;
    (void) fs;

    v = kmalloc(sizeof(struct vfs), M_SLEEPOK);
    if (!v) { return -ENOMEM; }

    v->covered = NULL;
    v->data    = cpiofs_mksb(arg);

    root = cpio_make_vnode(v, VDIR);
    if (!root) {
        kfree(v);
        return -ENOMEM;
    }

    v->root = root;
    v->ops  = &cpiofs_vfsops;

    cpiofs_rdinodes(root, arg);

    *out = v;
    return 0;
}

int cpio_lookup(struct vnode *dir, const char *name, struct vnode **out) {
    struct cpio_node *cp = (struct cpio_node *) dir->data;
    struct cpio_node *child;
    struct list_head *cur;

    if (strcmp(name, "..") == 0) {
        if (cp->parent) { *out = cp->parent->vnode; };
        *out = cp->vnode;
        return 0;
    }

    list_for_each(cur, &cp->children) {
        child = list_entry(cur, struct cpio_node, sibling);
        if (strcmp(child->name, name) == 0) {
            vget(child->vnode);
            *out = child->vnode;
            return 0;
        }
    }

    return -ENOENT;
}

ssize_t cpio_read(struct file *f, void *buf, size_t len) {
    struct cpio_node *cp = (struct cpio_node *) f->vn->data;
    if (!len) { return 0; }
    if (f->offset + len > cp->size) { len = cp->size - f->offset; }

    memcpy((char *) buf, cp->data + f->offset, len);

    return len;
}

off_t cpio_seek(struct file *f, off_t off, int whence) {
    struct cpio_node *cp = (struct cpio_node *) f->vn->data;

    switch (whence) {
        case SEEK_SET:
            f->offset = off;
            break;
        case SEEK_CUR:
            f->offset += off;
            break;
        case SEEK_END:
            f->offset = cp->size + off;
            break;
        default:
            return -EINVAL;
    }

    return 0;
}
