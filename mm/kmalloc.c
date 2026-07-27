#include <mm/address.h>
#include <mm/kmalloc.h>
#include <mm/mm_types.h>
#include <mm/physmem.h>
#include <mm/slab.h>
#include <nyx/linkage.h>
#include <nyx/minmax.h>
#include <nyx/string.h>

#include <asi/bitops.h>

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
    if (size == 0) return NULL;
    int order = MAX((int) cilog2(size), ilog2((unsigned long) KMALLOC_SMALLEST));
    int idx   = order - ilog2((unsigned long) KMALLOC_SMALLEST);
    if (idx >= KMALLOC_NUM_SIZES) return NULL;
    return kmem_cache_alloc(kmalloc_caches[idx], flags);
}

void kfree(void *addr) {
    struct page  *page;
    kmem_cache_t *cache;

    page  = virt_to_page(addr);
    cache = page->kmem_cache;
    kmem_cache_free(cache, addr);
}
