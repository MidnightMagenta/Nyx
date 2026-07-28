#ifndef _MM_MM_TYPES_H
#define _MM_MM_TYPES_H

#include <nyx/atomic.h>
#include <nyx/list.h>
#include <nyx/refcount.h>
#include <nyx/types.h>

#include <asi/bitops.h>
#include <asi/page_data.h>

#define PG_reserved (1 << 0)
#define PG_buddy    (1 << 1)
#define PG_slab     (1 << 2)
#define PG_pgtable  (1 << 3)
#define PG_head     (1 << 4)
#define PG_kmalloc  (1 << 5)

#define PageReserved(page) test_bit(PG_reserved, &(page)->flags)
#define PageBuddy(page)    test_bit(PG_buddy, &(page)->flags)
#define PageSlab(page)     test_bit(PG_slab, &(page)->flags)
#define PagePgtable(page)  test_bit(PG_pgtable, &(page)->flags)
#define PageHead(page)     test_bit(PG_head, &(page)->flags)
#define PageKmalloc(page)  test_bit(PG_kmalloc, &(page)->flags)

#define SetPageReserved(page) set_bit(PG_reserved, &(page)->flags)
#define SetPageBuddy(page)    set_bit(PG_buddy, &(page)->flags)
#define SetPageSlab(page)     set_bit(PG_slab, &(page)->flags)
#define SetPagePgtable(page)  set_bit(PG_pgtable, &(page)->flags)
#define SetPageHead(page)     set_bit(PG_head, &(page)->flags)
#define SetPageKmalloc(page)  set_bit(PG_kmalloc, &(page)->flags)

#define ClearPageReserved(page) clear_bit(PG_reserved, &(page)->flags)
#define ClearPageBuddy(page)    clear_bit(PG_buddy, &(page)->flags)
#define ClearPageSlab(page)     clear_bit(PG_slab, &(page)->flags)
#define ClearPagePgtable(page)  clear_bit(PG_pgtable, &(page)->flags)
#define ClearPageHead(page)     clear_bit(PG_head, &(page)->flags)
#define ClearPageKmalloc(page)  clear_bit(PG_kmalloc, &(page)->flags)

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
    u64              flags;
    struct list_head list;

    int zone_id;
    int head_order;
    u64 private;
    struct refcount refcnt;
    struct refcount refcnt_private;

    union {
        struct {
            struct kmem_cache_s *kmem_cache;
            struct kmem_slab_s  *kmem_slab;
        };
    };
};

#define VM_READ          (1 << 0)
#define VM_WRITE         (1 << 1)
#define VM_EXEC          (1 << 2)
#define VM_USER          (1 << 3)
#define VM_CACHE_DISABLE (1 << 4)

struct vma_region_struct {
    struct list_head list;

    virt_addr_t start, end;
    u32         prot;
    u32         flags;

    void *private;
};

struct vmspace {
    pgd_t           *pgd;
    struct list_head vma_regions;

    struct refcount refcount;
};

#endif
