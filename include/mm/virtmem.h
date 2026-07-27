#ifndef _MM_VIRTMEM_H
#define _MM_VIRTMEM_H

#include <mm/mm_types.h>
#include <nyx/stddef.h>
#include <nyx/types.h>

#include <asi/address.h>
#include <asi/page_data.h>

pgd_t *vm_get_page_table(int gfp_flags);
void   vm_free_page_table(pgd_t *pgd);

int         vm_map(pgd_t *pgd, phys_addr_t phys, virt_addr_t virt, size_t len, unsigned long flags, int gfp_flags);
int         vm_map_raw(pgd_t *pgd, phys_addr_t phys, virt_addr_t virt, size_t len, unsigned long flags, int gfp_flags);
int         vm_umap(pgd_t *pgd, virt_addr_t virt, size_t len);
int         vm_copy(pgd_t *dst, pgd_t *src, int flags);
int         vm_copy_kernel(pgd_t *dst, pgd_t *src);
int         vm_copy_user(pgd_t *dst, pgd_t *src, int flags);
void        vm_free_user(pgd_t *pgd);
void        vm_activate(pgd_t *pgd);
phys_addr_t vm_getphys(pgd_t *pgd, virt_addr_t virt);
int         vm_copyout(pgd_t *pgd, virt_addr_t dst_virt, char *src, size_t len);
int         vm_copyin(pgd_t *pgd, char *dst, virt_addr_t src_virt, size_t len);
int         vm_copyinstr(pgd_t *pgd, void *dst, virt_addr_t srcva, size_t len, size_t *done);
int         vm_access_ok(pgd_t *pgd, virt_addr_t virt, size_t len);

static inline void *vm_getva(pgd_t *pgd, virt_addr_t virt) {
    return __va(vm_getphys(pgd, virt));
}

#endif
