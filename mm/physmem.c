#include <mm/address.h>
#include <mm/memory.h>
#include <mm/mm_types.h>
#include <mm/mmzone.h>
#include <mm/physmem.h>
#include <nyx/linkage.h>
#include <nyx/list.h>
#include <nyx/string.h>
#include <nyx/types.h>

#include <asi/bug.h>

#define pr_fmt(fmt) "pmm: " fmt

#ifdef CONFIG_PHYSMEM_DEV_PRINT
#define physmem_pr_dev(fmt, ...) printk(pr_fmt(fmt), ##__VA_ARGS__)
#else
#define physmem_pr_dev(fmt, ...) /* void */
#endif


void __init init_page_alloc() { /* void */ }

#define buddy_of(addr, order) ((addr) ^ (1ul << (order)))

static inline int page_is_buddy(struct page *page, unsigned long order) {
    if (!PageBuddy(page)) { return 0; }
    return page->pg_private == order;
}

static struct page *__rm_block(zone_t *zone, unsigned long order) {
    struct free_area_s *area;
    unsigned long       cur_order;
    struct page        *page, *buddy;
    size_t              size;

    for (cur_order = order; cur_order < MAX_ORDER; ++cur_order) {
        area = &zone->free_area[cur_order];
        if (!list_is_empty(&area->list[0])) { goto block_found; }
    }
    return NULL;

block_found:
    page = list_first_entry(&area->list[0], struct page, pg_list);
    list_del(&page->pg_list);
    ClearPageBuddy(page);
    page->pg_private = 0;
    area->free_count--;

    size = 1ul << cur_order;
    while (cur_order > order) {
        cur_order--;
        area = &zone->free_area[cur_order];
        size >>= 1;
        buddy = page + size;
        list_add(&buddy->pg_list, &area->list[0]);
        area->free_count++;
        SetPageBuddy(buddy);
        buddy->pg_private = cur_order;
    }

    zone->free_pages -= 1ul << order;
    return page;
}

static void __add_block(struct page *page, zone_t *zone, unsigned long order) {
    struct page *base     = zone->zone_mem_map;
    size_t       page_idx = page - base;
    size_t       buddy_idx;
    struct page *buddy;

    zone->free_pages += 1ul << order;

    while (order < MAX_ORDER - 1) {
        buddy_idx = buddy_of(page_idx, order);
        buddy     = base + buddy_idx;

        if (!page_is_buddy(buddy, order)) { break; }

        list_del(&buddy->pg_list);
        ClearPageBuddy(buddy);

        zone->free_area[order].free_count--;

        page_idx &= ~(1ul << order);
        page = base + page_idx;
        order++;
    }

    list_add(&page->pg_list, &zone->free_area[order].list[0]);
    SetPageBuddy(page);
    page->pg_private = order;
    zone->free_area[order].free_count++;
}

static inline int gfp_zonelist(int gfp_mask) {

#ifdef CONFIG_ZONE_DMA
    if (gfp_mask & __M_DMA) { return ZONE_DMA; }
#endif
#ifdef CONFIG_ZONE_DMA32
    if (gfp_mask & __M_DMA32) { return ZONE_DMA32; }
#endif
#ifdef CONFIG_ZONE_HIGHMEM
    if (gfp_mask & __M_HIMEM) { return ZONE_HIMEM; }
#endif

    return ZONE_NORMAL;
}

struct page *pm_alloc_pages(int gfp_mask, unsigned long order) {
    const struct zonelist *zlist;
    struct page           *page;
    zone_t                *zone;

    zlist = &pgdata->zonelists[gfp_zonelist(gfp_mask)];
    for (int i = 0; zlist->zones[i] != NULL; ++i) {
        zone = zlist->zones[i];
        page = __rm_block(zone, order);
        if (page) {
            physmem_pr_dev("allocating block of order %d from zone %s at %#p\n",
                           order,
                           zlist->zones[i]->name,
                           page_to_phys(page));
            goto found_page;
        }
    }

    // TODO: major, PMM - implement OOM strategies
    if (gfp_mask & __M_NOSLEEP) {
        // can use reserved pool
        return NULL;
    }
    if (gfp_mask & __M_SLEEPOK) {
        // can sleep
        return NULL;
    }
    return NULL; // oom

found_page:
    if (gfp_mask & __M_ZERO) {
        void  *addr = page_address(page);
        size_t len  = (1ul << order);
        memset(addr, 0, len);
    }

    SetPageHead(page);
    page->pg_head_order = order;

    return page;
}

phys_addr_t __pm_get_free_pages(int gfp_mask, unsigned long order) {
    if (order >= MAX_ORDER) { return INVALID_PHYS_ADDR; }
    struct page *page = pm_alloc_pages(gfp_mask, order);
    if (page) { return page_to_phys(page); }

    return INVALID_PHYS_ADDR;
}

void __pm_free_pages(struct page *page, unsigned long order) {
    BUG_ON(order >= MAX_ORDER);
    BUG_ON(!page);
#ifdef __DEBUG
    bool page_real = false;
    for (int i = 0; i < MAX_NR_ZONES; ++i) {
        if (page >= pgdata->zones[i].zone_mem_map &&
            page < (pgdata->zones[i].zone_mem_map + pgdata->zones[i].spanned_pages)) {
            page_real = true;
        }
    }
    BUG_ON(!page_real);
#endif
    physmem_pr_dev("freeing block at %#p at order %d\n", page_to_phys(page), order);
    ClearPageHead(page);
    zone_t *zone = &pgdata->zones[page->pg_zone_id];
    __add_block(page, zone, order);
}

void pm_free_pages(phys_addr_t addr, unsigned long order) {
    struct page *page = phys_to_page(addr);
    __pm_free_pages(page, order);
}
