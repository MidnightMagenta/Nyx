#include "../boot_utils.h"
#include "../elf.h"
#include "../printb.h"
#include "../region_alloc.h"
#include "multiboot2.h"

#include <nyx/align.h>
#include <nyx/limits.h>
#include <nyx/mem_units.h>
#include <nyx/stddef.h>
#include <nyx/types.h>

#include <asi/bootparam.h>
#include <asi/memory.h>
#include <asi/page.h>
#include <asi/setupdata.h>

extern char __kernel_blob_start;
extern char __kernel_blob_end;

void *kernel_blob_start = &__kernel_blob_start;
void *kernel_blob_end   = &__kernel_blob_end;

void __attribute__((noreturn)) die() {
    hcf();
}

/* KERNEL LOADING */

struct kernel_loadinfo {
    struct memregion k_pregion;
    struct memregion k_vregion;
    u64              k_entry;
};

/* This really should never fail... */
static int verify_ehdr(const Elf64_Ehdr *const ehdr) {
    if (memcmpb(&ehdr->e_ident[EI_MAG0], ELFMAG, SELFMAG) != 0) { return 1; }
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) { return 1; }
    if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) { return 1; }
    if (ehdr->e_type != ET_EXEC) { return 1; } // TODO: minor, KASLR - add ET_DYN support
    if (ehdr->e_machine != EM_AMD64) { return 1; }
    if (ehdr->e_version != EV_CURRENT) { return 1; }
    return 0;
}

static int get_load_range(const Elf64_Ehdr *ehdr, const Elf64_Phdr *phdrs, u64 *laddr, u64 *haddr) {
    *laddr = U64_MAX;
    *haddr = 0;

    for (int i = 0; i < ehdr->e_phnum; ++i) {
        if (phdrs[i].p_type != PT_LOAD) { continue; }
        *laddr = min(*laddr, phdrs[i].p_vaddr);
        *haddr = max(*haddr, (phdrs[i].p_vaddr + phdrs[i].p_memsz));
    }

    // really should never fail...
    if (*laddr == U64_MAX) {
        pr_error(pr_fmt("Kernel payload ELF contains no loadable segments"));
        return 1;
    }

    *laddr = ALIGN_DOWN(*laddr, PAGE_SIZE);
    *haddr = ALIGN_UP(*haddr, PAGE_SIZE);

    pr_dbg(pr_fmt("Kernel payload memory range [0x%lx, 0x%lx]"), *laddr, *haddr);

    return 0;
}

static int alloc_kernel_region(u64 laddr, u64 haddr, struct memregion *region) {
    *region = ra_get_region(haddr - laddr, PAGE_SIZE);

    if (region->size == 0) {
        pr_error(pr_fmt("Could not allocate memory for kernel payload"));
        return 1;
    }

    pr_dbg(pr_fmt("Kernel payload physical memory range [0x%lx, 0x%lx]"), region->base, region->base + region->size);

    memsetb((void *) region->base, 0, region->size);

    return 0;
}

static int load_segments(const Elf64_Ehdr *ehdr, const Elf64_Phdr *phdrs, u64 laddr, struct memregion region) {
    for (int i = 0; i < ehdr->e_phnum; ++i) {
        const Elf64_Phdr *ph = &phdrs[i];

        if (ph->p_type != PT_LOAD) { continue; }
        if ((void *) ((char *) kernel_blob_start + ph->p_offset + ph->p_filesz) > kernel_blob_end) {
            pr_error(pr_fmt("Kernel payload offset out of range. p_offset is 0x%lx"), ph->p_offset);
            return 1;
        }
        if (ph->p_memsz < ph->p_filesz) {
            pr_error(pr_fmt("Invalid sections size. p_memsz is 0x%lx p_filesz is 0x%lx"), ph->p_memsz, ph->p_filesz);
            return 1;
        }
        if (ph->p_memsz == 0) { continue; }

        u64 load_base = (ph->p_paddr - laddr) + region.base;
        memcpyb((void *) load_base, (void *) ((char *) kernel_blob_start + ph->p_offset), ph->p_filesz);
    }

    return 0;
}

static int load_kernel(struct kernel_loadinfo *loadinfo) {
    int              res;
    u64              laddr, haddr;
    Elf64_Ehdr      *ehdr;
    Elf64_Phdr      *phdrs;
    struct memregion kernel_region;

    if (!loadinfo) {
        pr_error(pr_fmt("Invalid call to load_kernel()"));
        die();
    }

    ehdr = (Elf64_Ehdr *) kernel_blob_start;
    if ((res = verify_ehdr(ehdr)) != 0) {
        pr_error(pr_fmt("Could not verify kernel payload ELF header"));
        return res;
    }

    phdrs = (Elf64_Phdr *) ((char *) kernel_blob_start + ehdr->e_phoff);

    if ((res = get_load_range(ehdr, phdrs, &laddr, &haddr)) != 0) { return res; }

    if ((res = alloc_kernel_region(laddr, haddr, &kernel_region)) != 0) { return res; }

    if ((res = load_segments(ehdr, phdrs, laddr, kernel_region)) != 0) { return res; }

    memsetb(loadinfo, 0, sizeof(struct kernel_loadinfo));

    loadinfo->k_pregion = kernel_region;
    loadinfo->k_vregion = (struct memregion){laddr, haddr - laddr};
    loadinfo->k_entry   = (u64) ehdr->e_entry;

    return 0;
}

/* !KERNEL LOADING */

/* MEMORY MAPPING */

#define PAGE_HEAP_SIZE 8 * MiB

static struct memregion page_heap;
static u64              alloc_ptr;

static int init_page_heap() {
    page_heap = ra_get_region(PAGE_HEAP_SIZE, PAGE_SIZE);
    if (page_heap.size == 0) {
        pr_error(pr_fmt("Failed to allocate page heap"));
        return 1;
    }
    alloc_ptr = page_heap.base;
    return 0;
}

static void *pgheap_alloc() {
    if (alloc_ptr >= page_heap.base + page_heap.size) { return NULL; }
    u64 alloc = alloc_ptr;
    alloc_ptr += PAGE_SIZE;
    return (void *) alloc;
}

enum {
    PAGE_SIZE_4KiB,
    PAGE_SIZE_2MiB,
};

#define PML4_IDX(a) ((a >> 39) & 0x1FF)
#define PDPT_IDX(a) ((a >> 30) & 0x1FF)
#define PD_IDX(a)   ((a >> 21) & 0x1FF)
#define PT_IDX(a)   ((a >> 12) & 0x1FF)

#define PAGE_PRESENT (1ULL << 0)
#define PAGE_RW      (1ULL << 1)
#define PAGE_PS      (1ULL << 7)

#define PAGE_ADDR_MASK 0x000FFFFFFFFFF000ULL

static int map_page(u64 *pml4, phys_addr_t paddr, virt_addr_t vaddr, int size) {
    u64 *pdpt, *pd, *pt;
    u64  pml4i = PML4_IDX(vaddr);
    u64  pdpti = PDPT_IDX(vaddr);
    u64  pdi   = PD_IDX(vaddr);
    u64  pti   = PT_IDX(vaddr);

    if (size == PAGE_SIZE_2MiB) {
        if ((paddr & ((2 * MiB) - 1)) || (vaddr & ((2 * MiB) - 1))) {
            pr_error(pr_fmt("Invalid alignment for 2 MiB mapping. vaddr 0x%lx paddr 0x%lx"), vaddr, paddr);
            return 1;
        }
    } else if (size == PAGE_SIZE_4KiB) {
        if ((paddr & (PAGE_SIZE - 1)) || (vaddr & (PAGE_SIZE - 1))) {
            pr_error(pr_fmt("Invalid alignment for 4 KiB mapping. vaddr 0x%lx paddr 0x%lx"), vaddr, paddr);
            return 1;
        }
    } else {
        pr_error(pr_fmt("Invalid page size %d"), size);
        return 1;
    }

    if (!(pml4[pml4i] & PAGE_PRESENT)) {
        u64 page = (u64) pgheap_alloc();
        if (page == 0) {
            pr_error(pr_fmt("Allocation failure"));
            return 1;
        }
        memsetb((void *) page, 0, PAGE_SIZE);
        pml4[pml4i] = page | (PAGE_PRESENT | PAGE_RW);
    }

    pdpt = (u64 *) (pml4[pml4i] & PAGE_ADDR_MASK);

    if (!(pdpt[pdpti] & PAGE_PRESENT)) {
        u64 page = (u64) pgheap_alloc();
        if (page == 0) {
            pr_error(pr_fmt("Allocation failure"));
            return 1;
        }
        memsetb((void *) page, 0, PAGE_SIZE);
        pdpt[pdpti] = page | (PAGE_PRESENT | PAGE_RW);
    }

    pd = (u64 *) (pdpt[pdpti] & PAGE_ADDR_MASK);

    if (size == PAGE_SIZE_2MiB) {
        if (pd[pdi] & PAGE_PRESENT && (pd[pdi] & PAGE_PS)) {
            pr_warn(pr_fmt("Remapping 2 MiB page at vaddr 0x%lx to paddr 0x%lx. "
                           "Previous paddr was 0x%lx"),
                    vaddr,
                    paddr,
                    (pd[pdi] & PAGE_ADDR_MASK));
        }
        pd[pdi] = (paddr | (PAGE_PRESENT | PAGE_RW | PAGE_PS)); // PS = 1, RW = 1, P = 1
        return 0;
    }

    if (pd[pdi] & PAGE_PS) {
        pr_error(pr_fmt("Cannot split existing 2 MiB mapping"));
        return 1;
    }

    if (!(pd[pdi] & PAGE_PRESENT)) {
        u64 page = (u64) pgheap_alloc();
        if (page == 0) {
            pr_error(pr_fmt("Allocation failure"));
            return 1;
        }
        memsetb((void *) page, 0, PAGE_SIZE);
        pd[pdi] = page | (PAGE_PRESENT | PAGE_RW);
    }

    pt = (u64 *) (pd[pdi] & PAGE_ADDR_MASK);

    if (pt[pti] & PAGE_PRESENT) {
        pr_warn(pr_fmt("Remapping 4 KiB page at vaddr 0x%lx to paddr 0x%lx. "
                       "Previous paddr was 0x%lx"),
                vaddr,
                paddr,
                (pt[pti] & PAGE_ADDR_MASK));
    }

    pt[pti] = (paddr | (PAGE_PRESENT | PAGE_RW));

    return 0;
}

static int map_range(u64 *pml4, phys_addr_t paddr, virt_addr_t vaddr, u64 len) {
    int         res;
    phys_addr_t paddr_end = paddr + len;
    virt_addr_t vaddr_end = vaddr + len;

    if (paddr + len < paddr || vaddr + len < vaddr) {
        pr_error(pr_fmt("Memory out of range. vaddr 0x%lx, paddr 0x%lx, length 0x%lx"), vaddr, paddr, len);
        return 1;
    }

    if ((paddr & (PAGE_SIZE - 1)) || (vaddr & (PAGE_SIZE - 1))) {
        pr_error(pr_fmt("Invalid alignment for range mapping. vaddr 0x%lx paddr 0x%lx"), vaddr, paddr);
        return 1;
    }

    if (len & (PAGE_SIZE - 1)) {
        pr_error(pr_fmt("Invalid length for range mapping. vaddr 0x%lx, paddr "
                        "0x%lx, length 0x%lx"),
                 vaddr,
                 paddr,
                 len);
        return 1;
    }

    while (paddr < paddr_end) {
        if (!(paddr & ((2 * MiB) - 1)) && !(vaddr & ((2 * MiB) - 1)) && (paddr_end - paddr) >= (2 * MiB) &&
            (vaddr_end - vaddr) >= (2 * MiB)) {
            if ((res = map_page(pml4, paddr, vaddr, PAGE_SIZE_2MiB)) != 0) { return res; }
            paddr += 2 * MiB;
            vaddr += 2 * MiB;
            continue;
        }

        if ((res = map_page(pml4, paddr, vaddr, PAGE_SIZE_4KiB)) != 0) { return res; }
        paddr += PAGE_SIZE;
        vaddr += PAGE_SIZE;
    }

    return 0;
}

static int get_mem_range(u64 bi, u64 *loaddr, u64 *hiaddr) {
    unsigned int         bi_size = *(unsigned int *) bi;
    struct mb2_tag      *bi_tag  = (struct mb2_tag *) (bi + 8);
    struct mb2_tag      *bi_end  = (struct mb2_tag *) (bi + bi_size);
    struct mb2_tag_mmap *mmap    = NULL;

    while (bi_tag <= bi_end && bi_tag->type != MB2_TAG_END) {
        if (bi_tag->type == MB2_TAG_MMAP) { mmap = (struct mb2_tag_mmap *) bi_tag; }
        bi_tag = (struct mb2_tag *) ((char *) bi_tag + ALIGN_UP(bi_tag->size, 8));
    }

    if (!mmap) {
        pr_error(pr_fmt("Could not get mmap"));
        return 1;
    }

    *loaddr = U64_MAX;
    *hiaddr = 0;

    for (u8 *p = (u8 *) mmap->entries; p < (u8 *) mmap + mmap->size; p += mmap->entry_size) {
        struct mb2_mmap_entry *mmap_entry = (struct mb2_mmap_entry *) p;
        if (mmap_entry->type != MB2_MMAP_AVAILABLE) { continue; }
        *loaddr = min(*loaddr, mmap_entry->addr);
        *hiaddr = max(*hiaddr, mmap_entry->addr + mmap_entry->len);
    }

    return 0;
}

static int map_memory(u64 bi, const struct kernel_loadinfo *loadinfo, struct memregion *pgrange) {
    int  res;
    u64 *pml4;
    u64  loaddr, hiaddr;

    if ((res = init_page_heap()) != 0) { return res; }

    pgrange->base = page_heap.base;
    pgrange->size = page_heap.size;

    if ((res = get_mem_range(bi, &loaddr, &hiaddr)) != 0) { return res; }

    pml4 = pgheap_alloc();
    if (!pml4) {
        pr_error(pr_fmt("Failed to allocate PML4"));
        return 1;
    }

    memsetb(pml4, 0, PAGE_SIZE);

    // identity map memory
    if ((res = map_range(pml4, loaddr, loaddr, hiaddr - loaddr)) != 0) { return res; }

    // direct map memory
    if ((res = map_range(pml4, loaddr, ARCH_DIRECT_MAP_BASE + loaddr, hiaddr - loaddr))) { return res; }

    // map the kernel
    if (loadinfo->k_pregion.size != loadinfo->k_vregion.size) {
        pr_error(pr_fmt("Kernel memory regions have different sizes. Physical size %d Virtual size %d"),
                 loadinfo->k_pregion.size,
                 loadinfo->k_vregion.size);
        return 1;
    }

    if ((res = map_range(pml4, loadinfo->k_pregion.base, loadinfo->k_vregion.base, loadinfo->k_vregion.size))) {
        return res;
    }

    __asm__ volatile("mov %0, %%cr3" ::"r"(pml4)
                     : "memory");

    return 0;
}

/* !MEMORY MAPPING */

/* BOOT PARAMS */

static int create_mmap(const struct mb2_tag_mmap    *bi_mmap,
                       const struct kernel_loadinfo *loadinfo,
                       const struct memregion       *page_reserved_range,
                       struct boot_params           *bootparams) {
    int mmap_entry_count = 0;
    u64 mmap_base, mmap_size;
    u32 mmap_type;

    for (u8 *p = (u8 *) bi_mmap->entries; p < (u8 *) bi_mmap + bi_mmap->size; p += bi_mmap->entry_size) {
        if (mmap_entry_count >= MMAP_MAX_ENTRIES) {
            if (mmap_entry_count >= MMAP_MAX_ENTRIES) {
                pr_error(pr_fmt("Can't create boot_params memory map. Not enough space"));
            }
        }

        struct mb2_mmap_entry *mmap_entry = (struct mb2_mmap_entry *) p;
        mmap_base                         = mmap_entry->addr;
        mmap_size                         = mmap_entry->len;

        switch (mmap_entry->type) {
            case MB2_MMAP_AVAILABLE:
                mmap_type = MMAP_TYPE_RAM;
                break;
            case MB2_MMAP_ACPI_RECLAIMABLE:
                mmap_type = MMAP_TYPE_ACPI_RECLAIM;
                break;
            case MB2_MMAP_NVS:
                mmap_type = MMAP_TYPE_ACPI_NVS;
                break;
            case MB2_MMAP_BADRAM:
                mmap_type = MMAP_TYPE_BAD;
                break;
            default:
                mmap_type = MMAP_TYPE_RESERVED;
                break;
        }

        bootparams->mem_map.map[mmap_entry_count++] = (struct mmap_entry){mmap_base, mmap_size, mmap_type};
    }


    bootparams->mem_map.map[mmap_entry_count++] =
            (struct mmap_entry){page_reserved_range->base, page_reserved_range->size, MMAP_TYPE_BOOT_RECLAIM};
    bootparams->mem_map.map[mmap_entry_count++] =
            (struct mmap_entry){loadinfo->k_pregion.base, loadinfo->k_pregion.size, MMAP_TYPE_RESERVED};

    bootparams->mem_map.nr_entries = mmap_entry_count;

    return 0;
}

static int create_boot_params(u64                           bi,
                              const struct kernel_loadinfo *loadinfo,
                              const struct memregion       *page_reserved_range,
                              struct boot_params          **bootparams) {
    int              res;
    struct memregion bp_region;
    struct mb2_tag  *bi_tag, *bi_end;
    u32              bi_size;

    bp_region = ra_get_region(ALIGN_UP(sizeof(struct boot_params), PAGE_SIZE), PAGE_SIZE);
    if (bp_region.size == 0) {
        pr_error(pr_fmt("Failed to allocate boot_params"));
        return 1;
    }

    memsetb((void *) bp_region.base, 0, bp_region.size);

    *bootparams                     = (struct boot_params *) bp_region.base;
    (*bootparams)->version          = BP_VERSION_1;
    (*bootparams)->size             = bp_region.size;
    (*bootparams)->kernel_load_base = loadinfo->k_pregion.base;

    bi_size = *(u32 *) bi;
    bi_tag  = (struct mb2_tag *) (bi + 8);
    bi_end  = (struct mb2_tag *) (bi + bi_size);

    while (bi_tag <= bi_end && bi_tag->type != MB2_TAG_END) {
        switch (bi_tag->type) {
            case MB2_TAG_CMDLINE:
                break;
            case MB2_TAG_BOOTLAODER_NAME:
                break;
            case MB2_TAG_MODULE: {
                struct mb2_tag_module *mod = (struct mb2_tag_module *) bi_tag;
                if (strcmpb((const char *) mod->str, "initramfs") != 0) { break; }
                printb("initramfs found at [0x%lx - 0x%lx]\n", mod->mod_start, mod->mod_end);
                (*bootparams)->initramfs_base = mod->mod_start;
                (*bootparams)->initramfs_size = mod->mod_end - mod->mod_start;
            } break;
            case MB2_TAG_BASIC_MEMINFO:
                break;
            case MB2_TAG_BOOTDEV:
                break;
            case MB2_TAG_MMAP:
                if ((res = create_mmap((struct mb2_tag_mmap *) bi_tag, loadinfo, page_reserved_range, *bootparams))) {
                    return res;
                }
                break;
            case MB2_TAG_VBE:
                break;
            case MB2_TAG_FB:
                break;
            case MB2_TAG_ELF_SECTIONS:
                break;
            case MB2_TAG_APM:
                break;
            case MB2_TAG_EFI32:
                break;
            case MB2_TAG_EFI64:
                break;
            case MB2_TAG_SMBIOS:
                break;
            case MB2_TAG_ACPI1:
                break;
            case MB2_TAG_ACPI2:
                break;
            case MB2_TAG_NET:
                break;
            case MB2_TAG_EFI_MMAP:
                break;
            case MB2_TAG_EFI_BS:
                break;
            case MB2_TAG_EFI32_IH:
                break;
            case MB2_TAG_EFI64_IH:
                break;
            case MB2_TAG_LOAD_BASE_ADDR:
                break;
            default:
                pr_warn(pr_fmt("Unknown mb2 tag type %d"), bi_tag->type);
                break;
        }

        bi_tag = (struct mb2_tag *) ((char *) bi_tag + ALIGN_UP(bi_tag->size, 8));
    }

    return 0;
}

/* !BOOT PARAMS */

extern void jump_kernel(u64 boot_params, u64 entry);

/*
 * This code is responsible for loading the embeded kernel image
 * into a memory location, then mapping that memory, as well as
 * providing a direct map of all physical memory starting at address
 * 0xFFFF888000000000, populate the boot_params structure, and jump
 * to the kernel's entry point.
 */
void boot_main(u64 bi) {
    struct kernel_loadinfo kloadinfo;
    struct memregion       page_reserved_range;
    struct boot_params    *bootparams;

    boot_serial_init();

    if (ra_init(bi) != 0) { die(); }
    if (load_kernel(&kloadinfo) != 0) { die(); }
    if (map_memory(bi, &kloadinfo, &page_reserved_range) != 0) { die(); }
    if (create_boot_params(bi, &kloadinfo, &page_reserved_range, &bootparams)) { die(); }

    jump_kernel((u64) bootparams, kloadinfo.k_entry);

    hcf();
}
