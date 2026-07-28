#include <mm/address.h>
#include <mm/memblock.h>
#include <mm/mm_types.h>
#include <mm/mmzone.h>
#include <mm/physmem.h>
#include <nyx/align.h>
#include <nyx/errno.h>
#include <nyx/kernel.h>
#include <nyx/linkage.h>
#include <nyx/stddef.h>
#include <nyx/string.h>
#include <nyx/types.h>

#include <asi/bug.h>
#include <asi/page.h>

#define pr_fmt(fmt) "memblock: " fmt

#ifdef CONFIG_MEMBLOCK_DEV_PRINT
#define memblock_pr_dev(fmt, ...) printk(pr_fmt(fmt), ##__VA_ARGS__)
#else
#define memblock_pr_dev(fmt, ...)
#endif

/* the maximum number of needed regions is roughly the number of regions in the
 * sanitised memory map + number of allocations. Since firmware memory maps usually
 * contain less than 32 regions, and during early boot there aren't many allocations
 * expected, 128 memblock regions were chosen as a conservative number */
#define MEMBLOCK_INIT_REGIONS 128

static struct memblock_region memblock_memory_init_regions[MEMBLOCK_INIT_REGIONS] __initdata;
static struct memblock_region memblock_reserved_init_regions[MEMBLOCK_INIT_REGIONS] __initdata;

struct memblock memblock __initdata;

void memblock_init() {
    memblock.memory.regions = memblock_memory_init_regions;
    memblock.memory.cnt     = 0;
    memblock.memory.max     = MEMBLOCK_INIT_REGIONS;
    memblock.memory.name    = "memory";

    memblock.reserved.regions = memblock_reserved_init_regions;
    memblock.reserved.cnt     = 0;
    memblock.reserved.max     = MEMBLOCK_INIT_REGIONS;
    memblock.reserved.name    = "reserved";
}

static void __init memblock_sort(struct memblock_region *const r, size_t *n) {
    struct memblock_region temp = {0};
    if (*n == 0) { return; }

    for (size_t i = 1; i < *n; ++i) {
        temp     = r[i];
        size_t j = i;

        while (j > 0 && r[j - 1].base > temp.base) {
            r[j] = r[j - 1];
            j--;
        }
        r[j] = temp;
    }
}

static void __init memblock_merge_regions(struct memblock_region *const r, size_t *n) {
    size_t wr = 0;
    if (*n == 0) { return; }

    for (size_t rd = 1; rd < *n; ++rd) {
        u64 prev_end = r[wr].base + r[wr].size;
        u64 cur_end  = r[rd].base + r[rd].size;

        if (r[rd].base <= prev_end) {
            if (cur_end > prev_end) { r[wr].size = cur_end - r[wr].base; }
        } else {
            wr++;
            r[wr] = r[rd];
        }
    }

    *n = wr + 1;
}

#define memblock_sort_and_merge(r, n)                                                                                  \
    memblock_sort(r, n);                                                                                               \
    memblock_merge_regions(r, n);

static int __init __memblock_add(struct memblock_type *regions, phys_addr_t addr, size_t size) {
    memblock_pr_dev("adding region [%#p - %#p] to %s\n", addr, addr + size, regions->name);
    if (regions->cnt >= regions->max) { return -ENOSPC; }
    regions->regions[regions->cnt++] = (struct memblock_region){addr, size};
    memblock_sort_and_merge(regions->regions, &regions->cnt);
    regions->total_size += size;
    return 0;
}

static int __init __memblock_remove(struct memblock_type *regions, phys_addr_t addr, size_t size) {
    u64    sb = addr;
    u64    se;
    size_t i;

    memblock_pr_dev("removing region [%#p - %#p] from %s\n", addr, addr + size, regions->name);

    if (size == 0) return 0;

    se = sb + size;
    if (se < sb) return -EINVAL;

    for (i = 0; i < regions->cnt; i++) {
        struct memblock_region *r  = &regions->regions[i];
        u64                     rb = r->base;
        u64                     re = r->base + r->size;

        if (se <= rb || sb >= re) continue;

        if (sb <= rb && se >= re) {
            regions->total_size -= r->size;

            memmove(&regions->regions[i], &regions->regions[i + 1], (regions->cnt - i - 1) * sizeof(*r));

            regions->cnt--;
            i--;
            continue;
        }

        if (sb <= rb) {
            phys_addr_t new_base = se;
            phys_addr_t new_size = re - se;

            regions->total_size -= (new_base - rb);

            r->base = new_base;
            r->size = new_size;
            continue;
        }

        if (se >= re) {
            phys_addr_t removed = re - sb;

            regions->total_size -= removed;

            r->size = sb - rb;
            continue;
        }

        if (regions->cnt >= regions->max) return -ENOMEM;

        memmove(&regions->regions[i + 2], &regions->regions[i + 1], (regions->cnt - i - 1) * sizeof(*r));

        regions->regions[i + 1].base = se;
        regions->regions[i + 1].size = re - se;

        r->size = sb - rb;

        regions->total_size -= (se - sb);

        regions->cnt++;

        break;
    }

    return 0;
}

int __init memblock_add(phys_addr_t addr, size_t size) {
    return __memblock_add(&memblock.memory, addr, size);
}

int __init memblock_remove(phys_addr_t addr, size_t size) {
    int res;
    if ((res = __memblock_remove(&memblock.memory, addr, size))) { return res; }
    return __memblock_remove(&memblock.reserved, addr, size);
}

void __init memblock_trim() {
    size_t                  i;
    struct memblock_region *r;

    for (i = 0; i < memblock.memory.cnt; ++i) {
        r       = &memblock.memory.regions[i];
        r->base = PG_ALIGN_UP(r->base);
        r->size = PG_ALIGN_DN(r->size);
    }

    for (i = 0; i < memblock.reserved.cnt; ++i) {
        r       = &memblock.reserved.regions[i];
        r->base = PG_ALIGN_DN(memblock.reserved.regions[i].base);
        r->size = PG_ALIGN_UP(memblock.reserved.regions[i].size);
    }

    memblock_sort_and_merge(memblock.memory.regions, &memblock.memory.cnt);
}

void __init memblock_reserve(phys_addr_t addr, size_t size) {
    memblock_pr_dev("reserving region [%#p - %#p]\n", addr, addr + size);
    __memblock_remove(&memblock.memory, addr, size);
    __memblock_add(&memblock.reserved, addr, size);
}

int __init memblock_is_any_reserved(phys_addr_t addr, size_t size) {
    for (size_t i = 0; i < memblock.reserved.cnt; ++i) {
        struct memblock_region *r = &memblock.reserved.regions[i];

        u64 rb  = r->base;
        u64 re  = r->base + r->size;
        u64 end = addr + size;

        if (addr < re && end > rb) { return 1; }
    }

    return 0;
}

static const struct memblock_region __init *memblock_get_reserved_region(phys_addr_t addr) {
    for (size_t i = 0; i < memblock.reserved.cnt; ++i) {
        struct memblock_region *r = &memblock.reserved.regions[i];

        u64 rb = r->base;
        u64 re = r->base + r->size;

        if (addr >= rb && addr < re) { return r; }
    }

    return NULL;
}

int __init memblock_is_memory(phys_addr_t addr, size_t size) {
    for (size_t i = 0; i < memblock.memory.cnt; ++i) {
        struct memblock_region *r = &memblock.memory.regions[i];

        u64 rb  = r->base;
        u64 re  = r->base + r->size;
        u64 end = addr + size;

        if (addr >= rb && end < re) { return 1; }
    }

    return 0;
}

static phys_addr_t __init __memblock_alloc(size_t size, size_t alignment) {
    if (size == 0 || alignment == 0) { return INVALID_PHYS_ADDR; }

    size_t                sz  = ALIGN_UP(size, alignment);
    struct memblock_type *mem = &memblock.memory;

    for (ssize_t i = mem->cnt - 1; i >= 0; i--) {
        struct memblock_region *r = &mem->regions[i];

        phys_addr_t start = r->base;
        phys_addr_t end   = r->base + r->size;

        if (r->size < sz) continue;

        phys_addr_t cand = ALIGN_DOWN(end - sz, alignment);

        while (cand >= start) {
            if (!memblock_is_memory(cand, sz)) {
                if (cand < alignment) break;
                cand -= alignment;
                continue;
            }

            if (memblock_is_any_reserved(cand, sz)) {
                const struct memblock_region *reserved = memblock_get_reserved_region(cand);
                if (!reserved || reserved->base < sz) break;
                cand = ALIGN_DOWN(reserved->base - sz, alignment);
                continue;
            }

            memblock_reserve(cand, sz);
            return cand;
        }
    }

    return INVALID_PHYS_ADDR;
}

phys_addr_t __init memblock_alloc(size_t size) {
    return __memblock_alloc(size, 1);
}

phys_addr_t __init memblock_aligned_alloc(size_t size, size_t alignment) {
    return __memblock_alloc(size, alignment);
}

void __init memblock_free(phys_addr_t addr, u64 size) {
    __memblock_remove(&memblock.reserved, addr, size);
    __memblock_add(&memblock.memory, addr, size);
}

static inline size_t get_max_order(pfn_t pfn, pfn_t max_pfn) {
    int order = MAX_ORDER - 1;
    while (order > 0) {
        if (pfn & ((1ul << order) - 1)) {
            order--;
            continue;
        }
        if (pfn + (1ul << order) > max_pfn) {
            order--;
            continue;
        }
        return order;
    }
    return 0;
}

static inline void __init memblock_free_pages_core(pfn_t lo, pfn_t hi) {
    int order;

    while (lo < hi) {
        BUG_ON(PageReserved(pfn_to_page(lo)));
        order = get_max_order(lo, hi);
        pm_free_pages(lo << PAGE_SHIFT, order);
        lo += 1ul << order;
    }
}

static void __init memblock_free_pages(phys_addr_t lo, phys_addr_t hi) {
    lo = ALIGN_UP(lo, PAGE_SIZE);
    hi = ALIGN_DOWN(hi, PAGE_SIZE);

    memblock_free_pages_core(lo >> PAGE_SHIFT, hi >> PAGE_SHIFT);
}

void __init memblock_free_all() {
    struct memblock_region *region;

    for (size_t i = 0; i < memblock.memory.cnt; ++i) {
        region = &memblock.memory.regions[i];
        memblock_free_pages(region->base, region->base + region->size);
    }

    memblock.memory.cnt        = 0;
    memblock.memory.total_size = 0;
}

#ifdef __DEBUG
void memblock_print_regions() {
    size_t                        i;
    const struct memblock_region *r;

    pr_info(pr_fmt("memory regions:\n"));
    for (i = 0; i < memblock.memory.cnt; ++i) {
        r = &memblock.memory.regions[i];
        pr_info(pr_fmt("[%#p - %#p]\n"), r->base, r->base + r->size);
    }

    pr_info(pr_fmt("reserved regions:\n"));
    for (i = 0; i < memblock.reserved.cnt; ++i) {
        r = &memblock.reserved.regions[i];
        pr_info(pr_fmt("[%#p - %#p]\n"), r->base, r->base + r->size);
    }
}
#endif
