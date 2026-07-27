#ifndef _ASI_BOOT_H
#define _ASI_BOOT_H

#include <nyx/compiler.h>
#include <nyx/types.h>

#include <asi/address.h>
#include <asi/mmap.h>

#define BP_VERSION_NONE 0
#define BP_VERSION_1    1

struct boot_params {
    u32             version;          /* 0x000 */
    u32             size;             /* 0x004 */
    struct mmap_map mem_map;          /* 0x008 */
    u64             kernel_load_base; /* 0xA10 */
    u64             initramfs_base;   /* 0xA18 */
    u64             initramfs_size;   /* 0xA20 */
} __packed;

extern struct boot_params *bootparams;

#define bootparams_virt()   ((struct boot_params *) __va(bootparams))
#define get_initramfs()     ((void *) bootparams_virt()->initramfs_base)
#define get_initramfs_len() (bootparams_virt()->initramfs_size)

#endif
