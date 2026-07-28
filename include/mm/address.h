#ifndef _MM_ADDRESS_H
#define _MM_ADDRESS_H

#include <asi/address.h>
#include <asi/memory.h>
#include <asi/page.h>

#include <mm/memory.h>
#include <mm/mm_types.h>
#include <mm/mmzone.h>
#include <nyx/types.h>

static inline pfn_t page_to_pfn(struct page *page) {
    zone_t *zone = &pgdata->zones[page->pg_zone_id];
    return (page - zone->zone_mem_map) + zone->zone_start_pfn;
}

static inline struct page *pfn_to_page(pfn_t pfn) {
    for (int i = 0; i < MAX_NR_ZONES; i++) {
        zone_t *zone = &pgdata->zones[i];
        if (pfn >= zone->zone_start_pfn && pfn < zone->zone_start_pfn + zone->spanned_pages) {
            return &zone->zone_mem_map[pfn - zone->zone_start_pfn];
        }
    }
    return NULL;
}

static inline struct page *phys_to_page(phys_addr_t phys) {
    return pfn_to_page(phys >> PAGE_SHIFT);
}

static inline phys_addr_t page_to_phys(struct page *page) {
    return (phys_addr_t) (page_to_pfn(page) << PAGE_SHIFT);
}

#define page_address(page) __va((void *) page_to_phys((page)))
#define virt_to_page(virt) phys_to_page((phys_addr_t) __pa((phys_addr_t) (virt)))

#endif
