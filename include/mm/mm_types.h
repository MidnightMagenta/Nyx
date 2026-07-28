#ifndef _MM_MM_TYPES_H
#define _MM_MM_TYPES_H

#include <nyx/atomic.h>
#include <nyx/list.h>
#include <nyx/refcount.h>
#include <nyx/types.h>
#include <nyx/vfs.h>
#include <uapi/posix_types.h>

#include <asi/bitops.h>
#include <asi/page_data.h>

struct vmspace;

#define PG_reserved (1 << 0)
#define PG_buddy    (1 << 1)
#define PG_slab     (1 << 2)
#define PG_pgtable  (1 << 3)
#define PG_head     (1 << 4)
#define PG_kmalloc  (1 << 5)

#define PageReserved(page) test_bit(PG_reserved, &(page)->pg_flags)
#define PageBuddy(page)    test_bit(PG_buddy, &(page)->pg_flags)
#define PageSlab(page)     test_bit(PG_slab, &(page)->pg_flags)
#define PagePgtable(page)  test_bit(PG_pgtable, &(page)->pg_flags)
#define PageHead(page)     test_bit(PG_head, &(page)->pg_flags)
#define PageKmalloc(page)  test_bit(PG_kmalloc, &(page)->pg_flags)

#define SetPageReserved(page) set_bit(PG_reserved, &(page)->pg_flags)
#define SetPageBuddy(page)    set_bit(PG_buddy, &(page)->pg_flags)
#define SetPageSlab(page)     set_bit(PG_slab, &(page)->pg_flags)
#define SetPagePgtable(page)  set_bit(PG_pgtable, &(page)->pg_flags)
#define SetPageHead(page)     set_bit(PG_head, &(page)->pg_flags)
#define SetPageKmalloc(page)  set_bit(PG_kmalloc, &(page)->pg_flags)

#define ClearPageReserved(page) clear_bit(PG_reserved, &(page)->pg_flags)
#define ClearPageBuddy(page)    clear_bit(PG_buddy, &(page)->pg_flags)
#define ClearPageSlab(page)     clear_bit(PG_slab, &(page)->pg_flags)
#define ClearPagePgtable(page)  clear_bit(PG_pgtable, &(page)->pg_flags)
#define ClearPageHead(page)     clear_bit(PG_head, &(page)->pg_flags)
#define ClearPageKmalloc(page)  clear_bit(PG_kmalloc, &(page)->pg_flags)

#define __M_DMA     (1 << 0)
#define __M_DMA32   (1 << 1)
#define __M_HIGHMEM (1 << 2)
#define __M_NOSLEEP (1 << 3)
#define __M_SLEEPOK (1 << 4)
#define __M_ZERO    (1 << 5)

#define M_DMA      (__M_DMA)
#define M_DMA32    (__M_DMA32)
#define M_NOSLEEP  (__M_NOSLEEP)
#define M_SLEEPOK  (__M_SLEEPOK)
#define M_USER     (__M_SLEEPOK)
#define M_HIGHUSER (__M_SLEEPOK | __M_HIGHMEM)

struct page {
    u64              pg_flags;
    struct list_head pg_list;

    int             pg_zone_id;
    int             pg_head_order;
    u64             pg_private;
    struct refcount pg_refcnt;
    struct refcount pg_refcnt_private;

    union {
        struct {
            struct kmem_cache_s *pg_kmem_cache;
            struct kmem_slab_s  *pg_kmem_slab;
        };
    };
};

#define VM_READ          (1 << 0)
#define VM_WRITE         (1 << 1)
#define VM_EXEC          (1 << 2)
#define VM_USER          (1 << 3)
#define VM_CACHE_DISABLE (1 << 4)

#define MAP_PRIVATE   (1 << 0)
#define MAP_SHARED    (1 << 1)
#define MAP_ANONYMOUS (1 << 2)

struct vm_map_entry {
    virt_addr_t vm_start;
    virt_addr_t vm_end;
    int         vm_prot;
    int         vm_flags;

    struct vnode *vm_vn;
    off_t         vm_foff;

    struct list_head vm_list;
    struct vmspace  *vm_vmspace;
};

struct vmspace {
    pgd_t *v_pgd;

    struct refcount  v_refcount;
    struct list_head v_vmmap;
};

#endif
