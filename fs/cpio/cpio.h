#ifndef _FS_CPIO_H
#define _FS_CPIO_H

#include <nyx/limits.h>
#include <nyx/list.h>

#define CPIO_MAGIC     "070701"
#define CPIO_MAGIC_LEN 6

#define CPIO_MODE_FT_MASK       0170000
#define CPIO_MODE_FT_SOCK       0140000
#define CPIO_MODE_FT_SYMLNK     0120000
#define CPIO_MODE_REGFILE       0100000
#define CPIO_MODE_BLOCK_SPECIAL 0060000
#define CPIO_MODE_DIR           0040000
#define CPIO_MODE_CHAR          0020000
#define CPIO_MODE_PIPE          0010000
#define CPIO_MODE_SUID          0004000
#define CPIO_MODE_SGID          0004000
#define CPIO_MODE_STICKY        0001000
#define CPIO_MODE_PERM_MASK     0000777

#define CPIO_END_FILENAME "TRAILER!!!"

struct cpio_header {
    char c_mag[6];
    char c_ino[8];
    char c_mode[8];
    char c_uid[8];
    char c_gid[8];
    char c_nlink[8];
    char c_mtime[8];
    char c_filesz[8];
    char c_devmajor[8];
    char c_devminor[8];
    char c_rdevmajor[8];
    char c_rdevminor[8];
    char c_namesz[8];
    char c_check[8];
};

struct cpiofs_sb {
    void            *image;
    size_t           image_len;
    struct list_head all_nodes;
};

struct cpio_node {
    struct vnode *vnode;

    char name[NAME_MAX + 1];

    void  *data;
    size_t size;

    struct cpio_node *parent;
    struct list_head  children;
    struct list_head  sibling;

    struct list_head all;
};

#endif
