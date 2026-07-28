#ifndef _MM_MM_H
#define _MM_MM_H

#include <mm/mm_types.h>
#include <mm/virtmem.h>
#include <nyx/atomic.h>
#include <nyx/current.h>
#include <nyx/proc.h>


struct vmspace *vmspace_fork(struct process *p);
struct vmspace *vmspace_share(struct process *p);
struct vmspace *vmspace_new(struct process *parent);
void            vmspace_activate(struct vmspace *mm);
void            vmspace_put(struct vmspace *mm);
int             vmspace_map(struct vmspace *mm, virt_addr_t addr, size_t len, unsigned long flags, int gfp_flags);
int  vmspace_mapcopy(struct vmspace *mm, virt_addr_t addr, void *data, size_t len, unsigned long flags, int gfp_flags);
void vmspace_unmap(struct vmspace *mm, virt_addr_t addr, size_t len);

static inline int copyout(const void *uaddr, void *kaddr, size_t len) {
    return vm_copyout(current()->t_proc->p_mm->v_pgd, (virt_addr_t) uaddr, kaddr, len);
}

static inline int copyin(void *kaddr, const void *uaddr, size_t len) {
    return vm_copyin(current()->t_proc->p_mm->v_pgd, kaddr, (virt_addr_t) uaddr, len);
}

static inline int copyinstr(void *kaddr, const void *uaddr, size_t len, size_t *done) {
    return vm_copyinstr(current()->t_proc->p_mm->v_pgd, kaddr, (virt_addr_t) uaddr, len, done);
}

#endif
