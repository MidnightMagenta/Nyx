#include <nyx/kernel.h>
#include <nyx/linkage.h>
#include <nyx/minmax.h>
#include <nyx/printk.h>
#include <nyx/string.h>
#include <nyx/types.h>

#include <asi/bootparam.h>
#include <asi/bug.h>
#include <asi/mmap.h>
#include <asi/page.h>
#include <asi/setupdata.h>

#define pr_fmt(fmt) "mmap: " fmt

struct mmap_map mmap_map;

static const char *__mmap_type_strtab[] = {
        [MMAP_TYPE_RAM]          = "available",
        [MMAP_TYPE_BOOT_RECLAIM] = "boot reclaimable",
        [MMAP_TYPE_ACPI_RECLAIM] = "acpi reclaimable",
        [MMAP_TYPE_ACPI_NVS]     = "nvs",
        [MMAP_TYPE_RESERVED]     = "reserved",
        [MMAP_TYPE_BAD]          = "unusable",
};

static __init const char *mmap_type_string(mmap_type_t type) {
    return __mmap_type_strtab[type];
}

static __init void mmap_print_map() {
    for (u64 i = 0; i < mmap_map.nr_entries; ++i) {
        struct mmap_entry *entry = &mmap_map.map[i];
        printk(pr_fmt("[%#p - %#p] %s\n"), entry->addr, entry->addr + entry->size, mmap_type_string(entry->type));
    }
}

static int __init mmap_sanitize(struct mmap_entry *map, u64 max_nr_map, u64 *nr_map) {
    struct event {
        u64 addr;
        enum event_kind { EV_OPEN = 1, EV_CLOSE = 0 } kind;
        mmap_type_t type;
    };
    static_assert(EV_CLOSE < EV_OPEN, "CLOSE must sort before OPEN at same address");

    static struct event      events[MMAP_MAX_ENTRIES * 2] __initdata;
    static struct mmap_entry new_mmap[MMAP_MAX_ENTRIES * 2] __initdata;
    static int               active[MMAP_NR_TYPES] __initdata;
    u64                      prev;
    bool                     have_prev;
    u64                      old_nr, new_nr, ev_nr;
    u64                      i, j;

    memset(events, 0, sizeof(events));
    memset(new_mmap, 0, sizeof(new_mmap));
    memset(active, 0, sizeof(active));

    old_nr = *nr_map;
    BUG_ON(old_nr > max_nr_map);

    for (i = 0; i < old_nr; ++i) {
        if (map[i].addr + map[i].size < map[i].addr) { return -1; }
    }

    ev_nr = 0;
    for (i = 0; i < old_nr; ++i) {
        if (map[i].size == 0) { continue; }
        events[ev_nr++] = (struct event){map[i].addr, EV_OPEN, map[i].type};
        events[ev_nr++] = (struct event){map[i].addr + map[i].size, EV_CLOSE, map[i].type};
    }

    for (i = 1; i < ev_nr; ++i) {
        struct event key = events[i];
        j                = i;

        while (j > 0 &&
               (events[j - 1].addr > key.addr || (events[j - 1].addr == key.addr && events[j - 1].kind > key.kind))) {
            events[j] = events[j - 1];
            j--;
        }
        events[j] = key;
    }

    new_nr    = 0;
    prev      = 0;
    have_prev = false;
    for (i = 0; i < ev_nr; ++i) {
        u64 addr = events[i].addr;

        if (have_prev && addr > prev) {
            int winner = -1;
            for (int t = MMAP_NR_TYPES - 1; t >= 0; --t) {
                if (active[t]) {
                    winner = t;
                    break;
                }
            }
            if (winner >= 0) {
                BUG_ON(new_nr >= ARRAY_SIZE(new_mmap));
                new_mmap[new_nr++] = (struct mmap_entry){prev, addr - prev, winner};
            }
        }

        if (events[i].kind == EV_OPEN) {
            active[events[i].type]++;
        } else {
            active[events[i].type]--;
        }

        prev      = addr;
        have_prev = true;
    }

    j = 0;
    for (i = 0; i < new_nr; ++i) {
        if (j > 0 && new_mmap[j - 1].type == new_mmap[i].type &&
            new_mmap[j - 1].addr + new_mmap[j - 1].size == new_mmap[i].addr) {
            new_mmap[j - 1].size += new_mmap[i].size;
        } else {
            new_mmap[j++] = new_mmap[i];
        }
    }

    new_nr = j;
    BUG_ON(new_nr > max_nr_map);

    memcpy(map, new_mmap, new_nr * sizeof(struct mmap_entry));
    *nr_map = new_nr;

    return 0;
}

int mmap_any_mapped(u64 start, u64 end, mmap_type_t type) {
    for (u64 i = 0; i < mmap_map.nr_entries; ++i) {
        u64 entry_start = mmap_map.map[i].addr;
        u64 entry_end   = entry_start + mmap_map.map[i].size;

        if (start < entry_end && end > entry_start) {
            if (mmap_map.map[i].type == type) { continue; }
            return 1;
        }
    }

    return 0;
}

int mmap_all_mapped(u64 start, u64 end, mmap_type_t type) {
    for (u64 i = 0; i < mmap_map.nr_entries; ++i) {
        u64 entry_start = mmap_map.map[i].addr;
        u64 entry_end   = entry_start + mmap_map.map[i].size;

        if (start >= entry_start && end < entry_end) {
            if (mmap_map.map[i].type != type) { continue; }
            return 1;
        }
    }

    return 0;
}

void mmap_add_region(u64 start, u64 end, mmap_type_t type) {
    mmap_map.map[mmap_map.nr_entries++] = (struct mmap_entry){start, end - start, type};
    mmap_sanitize(mmap_map.map, ARRAY_SIZE(mmap_map.map), &mmap_map.nr_entries);
}

int mmap_get_next(u64 *start, u64 *end, mmap_type_t *type, u64 *idx) {
    const struct mmap_entry *ent;

    BUG_ON(!start || !end || !type || !idx);

    if (*idx >= mmap_map.nr_entries) { return 0; }

    ent    = &mmap_map.map[(*idx)++];
    *start = ent->addr;
    *end   = ent->addr + ent->size;
    *type  = ent->type;
    return 1;
}

pfn_t mmap_get_highest_pfn() {
    pfn_t lowest = PFN_MAX;
    for (size_t i = 0; i < mmap_map.nr_entries; ++i) { lowest = MIN(lowest, mmap_map.map[i].addr); }

    return lowest;
}

pfn_t mmap_get_lowest_pfn() {
    pfn_t highest = 0;
    for (size_t i = 0; i < mmap_map.nr_entries; ++i) {
        if (mmap_is_memory(&mmap_map.map[i])) { continue; }
        highest = MIN(highest, mmap_map.map[i].addr + mmap_map.map[i].size);
    }

    return highest;
}

void __init mmap_setup_map() {
    memcpy(&mmap_map, &bootparams->mem_map, sizeof(struct mmap_map));
    printk(pr_fmt("bootloader provided memory map:\n"));
    mmap_print_map();
    mmap_add_region(bootparams->initramfs_base,
                    bootparams->initramfs_base + bootparams->initramfs_size,
                    MMAP_TYPE_RESERVED);
    mmap_sanitize(mmap_map.map, ARRAY_SIZE(mmap_map.map), &mmap_map.nr_entries);
#ifdef __DEBUG
    printk(pr_fmt("sanitized memory map:\n"));
    mmap_print_map();
#endif
}
