#ifndef _NYX_FSNODE_H
#define _NYX_FSNODE_H

#include <nyx/types.h>
#include <nyx/vfs.h>

struct dirent {
    u64  d_fileno;
    u16  d_reclen;
    u8   d_type;
    u8   d_namlen;
    char d_name[];
};
enum { DT_UNKNOWN = 0, DT_FIFO = 1, DT_CHR = 2, DT_DIR = 4, DT_BLK = 6, DT_REG = 8, DT_LNK = 10, DT_SOCK = 12 };

struct fsnode {
    char                  *fn_name;
    size_t                 fn_namelen;
    enum vtype             fn_type;
    u16                    fn_mode;
    u64                    fn_ino;
    struct fsnode         *fn_parent;
    struct list_head       fn_children;
    struct list_head       fn_sibling;
    struct vnode          *fn_vnode;
    const struct vnodeops *fn_vops;
};

int fsnode_init(struct fsnode *fn, const char *name, size_t namelen, enum vtype type, const struct vnodeops *vops);

struct fsnode *fsnode_child(struct fsnode *dir, const char *name, size_t namelen);
void           fsnode_link(struct fsnode *dir, struct fsnode *child);
int            fsnode_vget(struct mount *mp, struct fsnode *n, struct vnode **vpp);

int fsnode_lookup(struct vnode *dvp, struct vnode **vpp, struct componentname *cnp);
int fsnode_readdir(struct vnode *vp, struct uio *uio);
int fsnode_reclaim(struct vnode *vp);

#endif
