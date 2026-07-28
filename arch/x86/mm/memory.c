#include <mm/memblock.h>
#include <mm/memory.h>
#include <mm/mm_types.h>
#include <mm/mmzone.h>
#include <nyx/align.h>
#include <nyx/kernel.h>
#include <nyx/limits.h>
#include <nyx/linkage.h>
#include <nyx/list.h>
#include <nyx/mem_units.h>
#include <nyx/minmax.h>
#include <nyx/string.h>
#include <nyx/types.h>

#include <asi/address.h>
#include <asi/mmap.h>
#include <asi/page.h>
#include <asi/setupdata.h>

#define pr_fmt(fmt) "memory: " fmt

extern pg_data_t contigmem_pagedata;

struct {
    phys_addr_t base;
    phys_addr_t top;
    char       *name;
} zone_descs[MAX_NR_ZONES] __initdata = {
        [ZONE_DMA]    = {0, 16 * MiB, "DMA"},
        [ZONE_DMA32]  = {16 * MiB, 4 * GiB, "DMA32"},
        [ZONE_NORMAL] = {4 * GiB, PHYS_ADDR_MAX, "NORMAL"},
};

#ifdef __DEBUG
static void print_zones() {
    for (size_t i = 0; i < MAX_NR_ZONES; ++i) {
        pr_dbg("zone %s:\n  "
               "mem_map addr:  0x%lx\n  "
               "start pfn:     %ld\n  "
               "spanned pages: %ld\n  "
               "present pages: %ld\n",
               contigmem_pagedata.zones[i].name,
               contigmem_pagedata.zones[i].zone_mem_map,
               contigmem_pagedata.zones[i].zone_start_pfn,
               contigmem_pagedata.zones[i].spanned_pages,
               contigmem_pagedata.zones[i].present_pages);
    }
}
#endif

static void __init get_hi_lo_pfn(pfn_t *lo, pfn_t *hi) {
    u64         start, end, idx = 0;
    mmap_type_t mem_type;

    *lo = PFN_MAX;
    *hi = 0;
    while (mmap_get_next(&start, &end, &mem_type, &idx)) {
        if (!mmap_type_is_memory(mem_type)) { continue; }
        *lo = MIN(*lo, start >> PAGE_SHIFT);
        *hi = MAX(*hi, end >> PAGE_SHIFT);
    }
}

static void __init init_zone_memmap(zone_t *zone, int zone_type) {
    u64          start, end, idx;
    mmap_type_t  mem_type;
    const pfn_t  zone_start = zone->zone_start_pfn, zone_end = zone_start + zone->spanned_pages;
    pfn_t        start_pfn, end_pfn;
    size_t       present_nr = 0;
    struct page *page;

    memset(zone->zone_mem_map, 0, zone->spanned_pages * sizeof(struct page));

    for (idx = 0; idx < zone->spanned_pages; ++idx) {
        page = &zone->zone_mem_map[idx];
        SetPageReserved(page);
        page->pg_zone_id = zone_type;
        page->pg_list    = (struct list_head) LIST_HEAD_INIT(page->pg_list);
    }

    idx = 0;
    while (mmap_get_next(&start, &end, &mem_type, &idx)) {
        start_pfn = ALIGN_UP(start, PAGE_SIZE) >> PAGE_SHIFT;
        end_pfn   = ALIGN_DOWN(end, PAGE_SIZE) >> PAGE_SHIFT;

        if (!mmap_type_is_memory(mem_type)) { continue; }
        if (end_pfn <= zone_start) { continue; }
        if (start_pfn >= zone_end) { continue; }

        if (start_pfn < zone_start) { start_pfn = zone_start; }
        if (end_pfn > zone_end) { end_pfn = zone_end; }

        while (start_pfn < end_pfn) {
            page = (start_pfn - zone->zone_start_pfn) + zone->zone_mem_map;
            ClearPageReserved(page);
            present_nr++;
            start_pfn++;
        }
    }

    zone->present_pages = present_nr;
}

static void __init init_zone(zone_t *zone, int zone_type) {
    pfn_t lopfn, hipfn;

    memset(zone, 0, sizeof(zone_t));

    for (size_t i = 0; i < MAX_ORDER; ++i) {
        zone->free_area[i].list[0] = (struct list_head) LIST_HEAD_INIT(zone->free_area[i].list[0]);
    }

    zone->name = zone_descs[zone_type].name;

    get_hi_lo_pfn(&lopfn, &hipfn);

    lopfn = MAX(zone_descs[zone_type].base >> PAGE_SHIFT, lopfn);
    hipfn = MIN(zone_descs[zone_type].top >> PAGE_SHIFT, hipfn);

    /* don't bother with 0 sized zones */
    if (hipfn <= lopfn) { return; }

    zone->zone_start_pfn = lopfn;
    zone->spanned_pages  = hipfn - lopfn;
    zone->zone_mem_map =
            (struct page *) phys_to_virt(memblock_aligned_alloc(zone->spanned_pages * sizeof(struct page), PAGE_SIZE));

    init_zone_memmap(zone, zone_type);
}

static void __init init_zonelists() {
    pgdata->zonelists[ZONE_DMA].zones[0] = &pgdata->zones[ZONE_DMA];
    pgdata->zonelists[ZONE_DMA].zones[1] = NULL;

    pgdata->zonelists[ZONE_DMA32].zones[0] = &pgdata->zones[ZONE_DMA32];
    pgdata->zonelists[ZONE_DMA32].zones[1] = &pgdata->zones[ZONE_DMA];
    pgdata->zonelists[ZONE_DMA32].zones[2] = NULL;

    pgdata->zonelists[ZONE_NORMAL].zones[0] = &pgdata->zones[ZONE_NORMAL];
    pgdata->zonelists[ZONE_NORMAL].zones[1] = &pgdata->zones[ZONE_DMA32];
    pgdata->zonelists[ZONE_NORMAL].zones[2] = &pgdata->zones[ZONE_DMA];
    pgdata->zonelists[ZONE_NORMAL].zones[3] = NULL;
}

static void __init init_pgdata() {
    size_t i;

    pgdata->start_pfn     = PFN_MAX;
    pgdata->spanned_pages = 0;
    pgdata->present_pages = 0;

    for (i = 0; i < MAX_NR_ZONES; ++i) {
        pgdata->start_pfn = MIN(pgdata->start_pfn, pgdata->zones[i].zone_start_pfn);
        pgdata->spanned_pages += pgdata->zones[i].spanned_pages;
        pgdata->present_pages += pgdata->zones[i].present_pages;
    }
}

static void __init init_zones() {
    for (size_t i = 0; i < MAX_NR_ZONES; ++i) { init_zone(&pgdata->zones[i], i); }
    init_zonelists();
}

void __init arch_init_memory() {
    init_zones();
    init_pgdata();

#ifdef __DEBUG
    print_zones();
#endif
}
