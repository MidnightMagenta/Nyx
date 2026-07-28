#include <mm/address.h>
#include <mm/kmalloc.h>
#include <mm/mm_types.h>
#include <mm/physmem.h>
#include <mm/slab.h>
#include <nyx/linkage.h>
#include <nyx/minmax.h>
#include <nyx/string.h>

#include <asi/bitops.h>
#include <asi/page.h>

#define pr_fmt(f) "kmalloc: " f

#define KMALLOC_NUM_SIZES 10
#define KMALLOC_SMALLEST  8

static kmem_cache_t *kmalloc_caches[KMALLOC_NUM_SIZES];

void __init kmalloc_init() {
    static char cache_name[32] __initdata;
    for (int i = 0; i < KMALLOC_NUM_SIZES; i++) {
        sprintf(cache_name, "kmalloc_%d", KMALLOC_SMALLEST << i);
        kmalloc_caches[i] =
                kmem_create_cache(cache_name, KMALLOC_SMALLEST << i, KMALLOC_SMALLEST << i, NULL, NULL, M_SLEEPOK);
    }
}

void *kmalloc(unsigned long size, int flags) {
    int order;
    int idx;

    if (size == 0) return NULL;

    order = MAX((int) cilog2(size), ilog2((unsigned long) KMALLOC_SMALLEST));
    idx   = order - ilog2((unsigned long) KMALLOC_SMALLEST);
    if (idx >= KMALLOC_NUM_SIZES) {
        struct page *pg = pm_alloc_pages(flags, order - ilog2((unsigned long) PAGE_SIZE));
        if (!pg) { return NULL; }
        SetPageKmalloc(pg);
        return page_address(pg);
    }

    return kmem_cache_alloc(kmalloc_caches[idx], flags);
}

void kfree(void *addr) {
    struct page  *pg;
    kmem_cache_t *cache;

    pg = virt_to_page(addr);

    if (PageKmalloc(pg)) {
        ClearPageKmalloc(pg);
        __pm_free_pages(pg, pg->head_order);
        return;
    }

    cache = pg->kmem_cache;
    kmem_cache_free(cache, addr);
}
