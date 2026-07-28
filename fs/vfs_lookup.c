#include <mm/kmalloc.h>
#include <mm/mm_types.h>
#include <mm/vmspace.h>
#include <nyx/errno.h>
#include <nyx/proc.h>
#include <nyx/string.h>
#include <nyx/uio.h>
#include <nyx/vfs.h>

static const char *next_component(struct componentname *cnp, const char *p) {
    while (*p == '/') { p++; }

    cnp->cn_nameptr = p;
    while (*p != '\0' && *p != '/') { p++; }

    cnp->cn_namelen = (size_t) (p - cnp->cn_nameptr);
    if (cnp->cn_namelen > NAME_MAX) { return NULL; }

    const char *q = p;
    while (*q == '/') { q++; }
    if (*q == '\0') {
        cnp->cn_flags |= NAMEI_ISLASTCN;
    } else {
        cnp->cn_flags &= ~NAMEI_ISLASTCN;
    }

    return p;
}

static inline bool cn_is(const struct componentname *cnp, const char *lit, size_t len) {
    return cnp->cn_namelen == len && memcmp(cnp->cn_nameptr, lit, len) == 0;
}

static struct vnode *cross_up(struct vnode *dvp, struct vnode *rootdir) {
    while (dvp != rootdir && dvp == dvp->v_mount->mnt_root && dvp->v_mount->mnt_vnodecovered != NULL) {
        struct vnode *covered = dvp->v_mount->mnt_vnodecovered;
        vref(covered);
        vrele(dvp);
        dvp = covered;
    }
    return dvp;
}

static int cross_down(struct vnode **vpp) {
    struct vnode *vp = *vpp;
    while (vp->v_mountedhere != NULL) {
        struct vnode *root;
        int           error = VFS_ROOT(vp->v_mountedhere, &root); /* +ref */
        if (error) return error;
        vrele(vp);
        vp = root;
    }
    *vpp = vp;
    return 0;
}

static int lookup(struct nameidata *ndp) {
    struct componentname *cnp  = &ndp->ni_cnd;
    const char           *path = ndp->ni_pathbuf;
    struct vnode         *dvp;
    struct vnode         *rootdir = ndp->ni_proc->fd->fd_rdir ? ndp->ni_proc->fd->fd_rdir : rootvnode;

    if (path[0] == '/') {
        dvp = rootdir;
    } else {
        dvp = ndp->ni_proc->fd->fd_cdir;
    }
    vref(dvp);

    cnp->cn_flags &= ~NAMEI_ISLASTCN;
    path = (path[0] == '/') ? path + 1 : path;

    {
        const char *q = path;
        while (*q == '/') { q++; }
        if (*q == '\0') {
            if (path == ndp->ni_pathbuf && *path == '\0') {
                vrele(dvp);
                return ENOENT;
            }
            ndp->ni_vp  = dvp;
            ndp->ni_dvp = NULL;
            return 0;
        }
    }

    for (;;) {
        path = next_component(cnp, path);
        if (path == NULL) {
            vrele(dvp);
            return ENAMETOOLONG;
        }
        bool last = (cnp->cn_flags & NAMEI_ISLASTCN) != 0;

        struct vnode *vp;

        if (cn_is(cnp, ".", 1)) {
            vp = dvp;
            vref(vp);
        } else if (cn_is(cnp, "..", 2)) {
            dvp = cross_up(dvp, rootdir);
            vn_lock(dvp, LK_EXCLUSIVE);
            int error = VOP_LOOKUP(dvp, &vp, cnp);
            VOP_UNLOCK(dvp);
            if (error) {
                vrele(dvp);
                return error;
            }

            error = cross_down(&vp);
            if (error) {
                vrele(dvp);
                return error;
            }
        } else {
            if (dvp->v_type != VDIR) {
                vrele(dvp);
                return ENOTDIR;
            }
            vn_lock(dvp, LK_EXCLUSIVE);
            int error = VOP_LOOKUP(dvp, &vp, cnp);
            VOP_UNLOCK(dvp);

            if (error == ENOENT && last && ndp->ni_op == NAMEI_CREATE) {
                ndp->ni_dvp = dvp;
                ndp->ni_vp  = NULL;
                return 0;
            }
            if (error) {
                vrele(dvp);
                return error;
            }

            int derr = cross_down(&vp);
            if (derr) {
                vrele(dvp);
                return derr;
            }

            if (vp->v_type == VLNK && (!last || (ndp->ni_flags & NAMEI_FOLLOW))) {
                vrele(vp);
                vrele(dvp);
                return ENOSYS;
            }
        }

        if (last) {
            ndp->ni_vp = vp;
            if (ndp->ni_op != NAMEI_LOOKUP || (ndp->ni_flags & NAMEI_WANTPARENT)) {
                ndp->ni_dvp = dvp;
            } else {
                vrele(dvp);
            }
            return 0;
        }

        vrele(dvp);
        dvp = vp;
    }
}

int namei(struct nameidata *ndp) {
    int   err;
    char *buf = kmalloc(MAXPATH, M_SLEEPOK);
    if (!buf) { return -ENOMEM; }

    if (ndp->ni_segflg == UIO_USERSPACE) {
        size_t done;
        err = copyinstr(buf, ndp->ni_dirp, MAXPATH, &done);
        if (err) { goto fail0; }
    } else {
        if (strlcpy(buf, ndp->ni_dirp, MAXPATH) >= MAXPATH) {
            err = ENAMETOOLONG;
            goto fail0;
        }
    }

    ndp->ni_pathbuf = buf;
    ndp->ni_vp = ndp->ni_dvp = NULL;
    ndp->ni_cnd.cn_nameiop   = ndp->ni_op;
    ndp->ni_cnd.cn_flags     = ndp->ni_flags;

    for (;;) {
        err = lookup(ndp);
        // TODO: if err == ERESTART_SYMLINK
        if (++ndp->ni_loopcnt > MAXSYMLINKS) {
            err = ELOOP;
            goto fail0;
        }
        ndp->ni_pathbuf = NULL;
        goto fail0;
    }

fail0:
    kfree(buf);
    return err;
}
