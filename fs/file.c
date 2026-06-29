#include <mm/slab.h>
#include <nyx/linkage.h>
#include <nyx/proc.h>
#include <nyx/refcount.h>
#include <nyx/vfs.h>

static kmem_cache_t *file_cache;
static kmem_cache_t *files_cache;

void file_free(struct file *f);

void __init init_files() {
    file_cache  = kmem_create_cache("file", sizeof(struct file), _Alignof(struct file), NULL, NULL, 0);
    files_cache = kmem_create_cache("files", sizeof(struct files), _Alignof(struct files), NULL, NULL, M_SLEEPOK);
}

void fget(struct file *f) {
    refcount_inc(&f->refs);
}

void fput(struct file *f) {
    if (refcount_get_dec(&f->refs) == 1) {
        vput(f->vn);
        file_free(f);
    }
}

struct file *file_alloc(int mflags) {
    return kmem_cache_alloc(file_cache, mflags);
}

void file_free(struct file *f) {
    kmem_cache_free(file_cache, f);
}

void files_get(struct files *files) {
    refcount_inc(&files->refs);
}

void files_put(struct files *files) {
    struct file *f;
    size_t       i = 0;
    if (refcount_get_dec(&files->refs) == 1) {
        while ((f = files->fd[i++])) { fput(f); }
    }
}

struct files *files_fork(struct process *parent) {
    struct files *newf = kmem_cache_alloc(files_cache, M_SLEEPOK);
    struct file  *f, *nf;
    size_t        i;

    if (!newf) { return NULL; }
    refcount_init(&newf->refs, 1);

    i = 0;
    while ((f = parent->files->fd[i])) {
        if (vfs_dup(f, &nf)) { goto fail0; }
        newf->fd[i] = nf;
        i++;
    }

    (void) parent;

    return newf;

fail0:
    i = 0;
    while ((f = newf->fd[i++])) { vfs_close(f); }
    kmem_cache_free(files_cache, newf);
    return NULL;
}

struct files *files_share(struct process *parent) {
    struct file *f;
    size_t       i;

    refcount_inc(&parent->files->refs);

    i = 0;
    while ((f = parent->files->fd[i++])) { fget(f); }
    return parent->files;
}
