#include <mm/mm_types.h>
#include <mm/slab.h>
#include <nyx/current.h>
#include <nyx/errno.h>
#include <nyx/linkage.h>
#include <nyx/list.h>
#include <nyx/percpu.h>
#include <nyx/refcount.h>
#include <nyx/string.h>
#include <nyx/vfs.h>

static LIST_HEAD(fs_list);
struct vnode *root_vnode;

static kmem_cache_t *vnode_cache;

extern void         init_files();
extern void         cpio_init();
extern struct file *file_alloc(int mflags);
extern void         file_free(struct file *f);

void __init init_vfs() {
    vnode_cache = kmem_create_cache("vnode", sizeof(struct vnode), _Alignof(struct vnode), NULL, NULL, 0);

    init_files();
    cpio_init();
}

void vget(struct vnode *v) {
    refcount_inc(&v->refs);
}

void vput(struct vnode *v) {
    if (refcount_get_dec(&v->refs) == 1) {
        if (v->ops->reclaim) { v->ops->reclaim(v); }
    }
}

struct vnode *vnode_alloc(int mflags) {
    return kmem_cache_alloc(vnode_cache, mflags);
}

void vnode_free(struct vnode *v) {
    kmem_cache_free(vnode_cache, v);
}

int vfs_register_fs(struct fs_type *fs) {
    list_add_tail(&fs->node, &fs_list);
    return 0;
}

struct fs_type *vfs_find_fs(const char *name) {
    struct list_head *cur;
    struct fs_type   *fs;

    list_for_each(cur, &fs_list) {
        fs = list_entry(cur, struct fs_type, node);
        if (strcmp(name, fs->name) == 0) { return fs; }
    }

    return NULL;
}

int vfs_mount_root(struct fs_type *fs, void *arg) {
    int         res;
    struct vfs *rootfs;

    if ((res = fs->mount(fs, arg, &rootfs))) { return res; }

    rootfs->covered = NULL;
    root_vnode      = rootfs->root;
    vget(root_vnode);
    return 0;
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

int namei(const char *path, struct vnode **out) {
    struct vnode *cur, *next;
    char          comp[NAME_MAX + 1];
    int           err;

    cur = (path[0] == '/') ? root_vnode : current()->proc->cwd;

    while ((path = next_comp(path, comp)) != NULL) {
        if (comp[0] == '\0' || strcmp(comp, ".") == 0) { continue; }
        if (cur->type != VDIR) {
            vput(cur);
            return -ENOTDIR;
        }

        if (strcmp(comp, "..") == 0) {
            if (cur == cur->vfs->root && cur->vfs->covered) {
                next = cur->vfs->covered;
                vget(next);
                vput(cur);
                cur = next;
            }
        }

        err = cur->ops->lookup(cur, comp, &next);
        vput(cur);
        if (err) { return err; }
        cur = next;

        while (cur->mounted) {
            next = cur->mounted->root;
            vget(next);
            vput(cur);
            cur = next;
        }
    }

    *out = cur;
    return 0;
}

int vfs_open(const char *path, int flags, struct file **out) {
    struct vnode *v;
    int           err;

    if ((err = namei(path, &v))) { return err; }
    *out = file_alloc(M_SLEEPOK);
    if (!*out) {
        err = -ENOMEM;
        goto fail0;
    }

    memset(*out, 0, sizeof(struct file));
    refcount_init(&(*out)->refs, 1);
    (*out)->vn    = v;
    (*out)->flags = flags;

    return 0;

fail0:
    vput(v);
    return err;
}

int vfs_close(struct file *f) {
    fput(f);
    return 0;
}

int vfs_dup(struct file *f, struct file **newf) {
    *newf = file_alloc(M_SLEEPOK);
    if (!*newf) { return -ENOMEM; }

    memset(*newf, 0, sizeof(struct file));
    refcount_init(&(*newf)->refs, 1);
    (*newf)->vn    = f->vn;
    (*newf)->flags = f->flags;

    return 0;
}
