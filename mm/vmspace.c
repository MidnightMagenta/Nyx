#include <mm/mm_types.h>
#include <mm/physmem.h>
#include <mm/slab.h>
#include <mm/virtmem.h>
#include <mm/vmspace.h>
#include <nyx/errno.h>
#include <nyx/list.h>
#include <nyx/minmax.h>
#include <nyx/proc.h>
#include <nyx/refcount.h>
#include <nyx/string.h>
#include <nyx/types.h>

#include <asi/address.h>
#include <asi/page.h>

#define pr_fmt(fmt) "vmspace: " fmt

kmem_cache_t *vmspace_cache;

void vmspace_init() {
    vmspace_cache =
            kmem_create_cache("vmspace", sizeof(struct vmspace), _Alignof(struct vmspace), NULL, NULL, M_SLEEPOK);
}

struct vmspace *vmspace_fork(struct process *p) {
    struct vmspace *newvm = vmspace_new(p);
    if (!newvm) { return NULL; }
    if (vm_copy_user(newvm->v_pgd, p->p_mm->v_pgd, M_SLEEPOK) != 0) { goto fail0; }

    return newvm;

fail0:
    vmspace_put(newvm);
    return NULL;
}

struct vmspace *vmspace_share(struct process *parent) {
    refcount_inc(&parent->p_mm->v_refcount);
    return parent->p_mm;
}

struct vmspace *vmspace_new(struct process *parent) {
    struct vmspace *newvm = kmem_cache_alloc(vmspace_cache, M_SLEEPOK);
    if (!newvm) { return NULL; }

    refcount_init(&newvm->v_refcount, 1);
    list_init(&newvm->v_vmmap);
    newvm->v_pgd = vm_get_page_table(M_SLEEPOK);
    if (!newvm->v_pgd) { goto fail0; }

    if (vm_copy_kernel(newvm->v_pgd, parent->p_mm->v_pgd)) { goto fail1; }

    return newvm;

fail1:
    vm_free_page_table(newvm->v_pgd);
fail0:
    kmem_cache_free(vmspace_cache, newvm);
    return NULL;
}

void vmspace_activate(struct vmspace *mm) {
    vm_activate(mm->v_pgd);
}

void vmspace_put(struct vmspace *mm) {
    if (!mm) { return; }
    if (refcount_get_dec(&mm->v_refcount) == 1) {
        vm_free_user(mm->v_pgd);
        vm_free_page_table(mm->v_pgd);
        kmem_cache_free(vmspace_cache, mm);
    }
}

int vmspace_map(struct vmspace *mm, virt_addr_t addr, size_t len, unsigned long flags, int gfp_flags) {
    int res;
    addr = PG_ALIGN_DN(addr);
    len  = PG_ALIGN_UP(len);

    for (size_t off = 0; off < len; off += PAGE_SIZE) {
        phys_addr_t pa = pm_get_zeroed_page(gfp_flags);
        if (pa == INVALID_PHYS_ADDR) {
            vm_umap(mm->v_pgd, addr, len);
            return -ENOMEM;
        }
        if ((res = vm_map(mm->v_pgd, pa, addr + off, PAGE_SIZE, flags | VM_USER, gfp_flags))) {
            pm_free_page(pa);
            vm_umap(mm->v_pgd, addr, len);
            return res;
        }
    }

    return 0;
}

int vmspace_mapcopy(struct vmspace *mm, virt_addr_t addr, void *data, size_t len, unsigned long flags, int gfp_flags) {
    int    res;
    size_t end = PG_ALIGN_UP(addr + len);
    addr       = PG_ALIGN_DN(addr);

    for (size_t off = 0; addr + off < end; off += PAGE_SIZE) {
        phys_addr_t pa    = pm_get_zeroed_page(gfp_flags);
        size_t      chunk = MIN(PAGE_SIZE, len - off);

        if (pa == INVALID_PHYS_ADDR) {
            vm_umap(mm->v_pgd, addr, off);
            return -ENOMEM;
        }

        memcpy(__va(pa), (char *) data + off, chunk);
        if (chunk < PAGE_SIZE) memset((char *) __va(pa) + chunk, 0, PAGE_SIZE - chunk);

        if ((res = vm_map(mm->v_pgd, pa, addr + off, PAGE_SIZE, flags | VM_USER, gfp_flags))) {
            pm_free_page(pa);
            vm_umap(mm->v_pgd, addr, off);
            return res;
        }
    }
    return 0;
}

void vmspace_unmap(struct vmspace *mm, virt_addr_t addr, size_t len) {
    vm_umap(mm->v_pgd, addr, len);
}
