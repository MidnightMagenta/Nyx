#ifndef _NYX_VFS_H
#define _NYX_VFS_H

#include <nyx/list.h>
#include <nyx/refcount.h>
#include <nyx/stddef.h>
#include <uapi/posix_types.h>

struct vops;
struct vfsops;
struct vfs;
struct process;

enum vtype { VNON, VREG, VDIR, VCHR, VBLK, VLNK };

struct vnode {
    enum vtype         type;
    struct refcount    refs;
    const struct vops *ops;
    struct vfs        *vfs;
    struct vfs        *mounted;
    void              *data;
};

struct file {
    struct vnode   *vn;
    off_t           offset;
    int             flags;
    struct refcount refs;
};

struct vops {
    int     (*lookup)(struct vnode *dir, const char *name, struct vnode **out);
    ssize_t (*read)(struct file *f, void *buf, size_t len);
    ssize_t (*write)(struct file *f, const void *buf, size_t len);
    off_t   (*lseek)(struct file *f, off_t off, int whence);
    void    (*reclaim)(struct vnode *vn);
};

struct vfs {
    struct vnode  *root;
    struct vnode  *covered;
    struct vfsops *ops;
    void          *data;
};

struct vfsops {
    int (*unmount)(struct vfs *vfs);
};

struct fs_type {
    const char      *name;
    int              (*mount)(struct fs_type *, void *arg, struct vfs **out);
    struct list_head node;
};

#define NR_FILES 32

struct files {
    struct refcount refs;
    struct file    *fd[NR_FILES]; // NULL terminated
};

extern struct vnode *root_vnode;

void          vget(struct vnode *v);
void          vput(struct vnode *v);
struct vnode *vnode_alloc(int mflags);
void          vnode_free(struct vnode *v);

void fget(struct file *f);
void fput(struct file *f);

void          files_put(struct files *files);
void          files_get(struct files *files);
struct files *files_fork(struct process *parent);
struct files *files_share(struct process *parent);

int             vfs_register_fs(struct fs_type *fs);
struct fs_type *vfs_find_fs(const char *name);

int vfs_mount_root(struct fs_type *fs, void *arg);
int vfs_mount(const char *path, struct fs_type *fs, void *arg);

#define SEEK_SET 1
#define SEEK_CUR 2
#define SEEK_END 3

int vfs_namei(const char *path, struct vnode **out);
int vfs_open(const char *path, int flags, struct file **out);
int vfs_close(struct file *f);
int vfs_dup(struct file *f, struct file **newf);

static inline off_t vfs_lseek(struct file *f, off_t offset, int whence) {
    return f->vn->ops->lseek(f, offset, whence);
}

static inline ssize_t vfs_read(struct file *f, void *buf, size_t len) {
    return f->vn->ops->read(f, buf, len);
}

static inline ssize_t vfs_write(struct file *f, const void *buf, size_t len) {
    return f->vn->ops->write(f, buf, len);
}

#endif
