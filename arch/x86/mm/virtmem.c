#include <mm/address.h>
#include <mm/mm_types.h>
#include <mm/physmem.h>
#include <mm/virtmem.h>
#include <nyx/errno.h>
#include <nyx/panic.h>
#include <nyx/refcount.h>
#include <nyx/string.h>
#include <nyx/types.h>

#include <asi/address.h>
#include <asi/bitops.h>
#include <asi/bug.h>
#include <asi/memory.h>
#include <asi/page.h>
#include <asi/page_data.h>
#include <asi/system.h>

#define pr_fmt(fmt) "arch virtmem: " fmt
#ifdef CONFIG_VIRTMEM_DEV_PRINT
#define virtmem_dev_pr(fmt, ...) printk(pr_fmt(fmt), ##__VA_ARGS__)
#else
#define virtmem_dev_pr(fmt, ...)
#endif

#define __PAGE_TABLE_ENTRY_COUNT 512

#define __PAGE_INDEX_MASK 0x1FFull
#define __PAGE_4K_MASK    0xFFFull
#define __PAGE_2M_MASK    0x1FFFFFull
#define __PAGE_1G_MASK    0x3FFFFFFFull
#define __PAGE_ADDR_MASK  0x000FFFFFFFFFF000ull

#define __PAGE_ADDR(pgd) (pgd & __PAGE_ADDR_MASK)

#define __PAGE_4K_SIZE         0x1000ull
#define __PAGE_2M_SIZE         0x200000ull
#define __PAGE_1G_SIZE         0x40000000ull
#define __PAGE_PML4_ENTRY_SIZE 0x8000000000ull

#define __PAGE_4K_PGCNT         1u
#define __PAGE_2M_PGCNT         (__PAGE_2M_SIZE >> PAGE_SHIFT)
#define __PAGE_1G_PGCNT         (__PAGE_1G_SIZE >> PAGE_SHIFT)
#define __PAGE_PML4_ENTRY_PGCNT (__PAGE_PML4_ENTRY_SIZE >> PAGE_SHIFT)

#define __PAGE_PML4_SHIFT 39
#define __PAGE_PDPT_SHIFT 30
#define __PAGE_PDT_SHIFT  21
#define __PAGE_PTT_SHIFT  12

#define __PML4T_IDX(a)    ((a >> __PAGE_PML4_SHIFT) & __PAGE_INDEX_MASK)
#define __PDPT_IDX(a)     ((a >> __PAGE_PDPT_SHIFT) & __PAGE_INDEX_MASK)
#define __PDT_IDX(a)      ((a >> __PAGE_PDT_SHIFT) & __PAGE_INDEX_MASK)
#define __PTT_IDX(a)      ((a >> __PAGE_PTT_SHIFT) & __PAGE_INDEX_MASK)
#define __PG_OFFSET_4K(a) (a & __PAGE_4K_MASK)
#define __PG_OFFSET_2M(a) (a & __PAGE_2M_MASK)
#define __PG_OFFSET_1G(a) (a & __PAGE_1G_MASK)

#define __PG_PRESENT_BIT       0
#define __PG_WRITE_BIT         1
#define __PG_USER_BIT          2
#define __PG_WRITE_THROUGH_BIT 3
#define __PG_CACHE_DISABLE_BIT 4
#define __PG_ACCESSED_BIT      5
#define __PG_NX_BIT            63

#define __PG_LLE_DIRTY_BIT  6
#define __PG_LLE_GLOBAL_BIT 8

#define __PG_PTE_PAT_BIT 7

// flags for 2M and 1G pages (PDE and PDPE)
#define __PG_PDE_PAGE_SIZE_BIT 7
#define __PG_PDE_PAT_BIT       12 // valid only if PS=1

#define __PG_PRESENT       BIT(__PG_PRESENT_BIT)
#define __PG_WRITE         BIT(__PG_WRITE_BIT)
#define __PG_USER          BIT(__PG_USER_BIT)
#define __PG_WRITE_THROUGH BIT(__PG_WRITE_THROUGH_BIT)
#define __PG_CACHE_DISABLE BIT(__PG_CACHE_DISABLE_BIT)
#define __PG_ACCESSED      BIT(__PG_ACCESSED_BIT)
#define __PG_NX            BIT(__PG_NX_BIT)

#define __PG_LLE_DIRTY  BIT(__PG_LLE_DIRTY_BIT)
#define __PG_LLE_GLOBAL BIT(__PG_LLE_GLOBAL_BIT)

#define __PG_PTE_PAT BIT(__PG_PTE_PAT_BIT)

// flags for 2M and 1G pages (PDE and PDPE)
#define __PG_PDE_PAGE_SIZE BIT(__PG_PDE_PAGE_SIZE_BIT)
#define __PG_PDE_PAT       BIT(__PG_PDE_PAT_BIT) // valid only if PS=1

/*
 * struct page's refcnt_private field is used to count the number of
 * active entries in the page table, if PG_pgtable is set.
 */
static inline int inc_pgtable_refcount(pgd_t *pgd) {
    struct page *page = virt_to_page(pgd);
    BUG_ON(!PagePgtable(page));
    return refcount_get_inc(&page->refcnt_private);
}

static inline int dec_pgtable_refcount(pgd_t *pgd) {
    struct page *page = virt_to_page(pgd);
    BUG_ON(!PagePgtable(page));
    return refcount_get_dec(&page->refcnt_private);
}

static inline int is_pgtable_empty(pgd_t *pgd) {
    struct page *page = virt_to_page(pgd);
    BUG_ON(!PagePgtable(page));
    return !refcount_get(&page->refcnt_private);
}

pgd_t *vm_get_page_table(int gfp_flags) {
    phys_addr_t  phys = pm_get_zeroed_page(gfp_flags);
    struct page *page;
    if (phys == INVALID_PHYS_ADDR) { return NULL; }

    page = phys_to_page(phys);
    SetPagePgtable(page);
    refcount_init(&page->refcnt_private, 0);

    return __va(phys);
}

void vm_free_page_table(pgd_t *pgd) {
    struct page *page;

    BUG_ON(!is_pgtable_empty(pgd));

    page = virt_to_page(pgd);
    ClearPagePgtable(page);
    __pm_free_page(page);
}

static inline u64 get_pte_flags(int flags) {
    u64 pte_flags = 0;

    set_bit(__PG_PRESENT_BIT, &pte_flags);
    if (flags & VM_WRITE) { set_bit(__PG_WRITE_BIT, &pte_flags); }
    if (flags & VM_USER) { set_bit(__PG_USER_BIT, &pte_flags); }
    // TODO: minor, VMM/EXEC_PROT - check if CPU supports NX, and if EFER.NXE == 1
    // if (!(flags & VM_EXEC)) { set_bit(__PG_NX_BIT, &pte_flags); }
    if (flags & VM_CACHE_DISABLE) { set_bit(__PG_CACHE_DISABLE_BIT, &pte_flags); }

    return pte_flags;
}

static inline pgd_t *get_table(pgd_t *pgd, int idx, int gfp_flags) {
    pgd_t *out_table;

    if (!(pgd[idx] & __PG_PRESENT)) {
        out_table = vm_get_page_table(gfp_flags);
        if (!out_table) { return NULL; }
        pgd[idx] = (pgd_t) __pa(out_table) | __PG_PRESENT | __PG_WRITE | __PG_USER;
        inc_pgtable_refcount(pgd);
    }

    return (pgd_t *) __va(__PAGE_ADDR(pgd[idx]));
}

static inline int can_map_2m(phys_addr_t phys, virt_addr_t virt, size_t len) {
    if (phys & __PAGE_2M_MASK || virt & __PAGE_2M_MASK) { return 0; }
    if (len < (__PAGE_2M_SIZE >> PAGE_SHIFT)) { return 0; }
    return 1;
}

#ifdef CONFIG_USE_GIGANTIC_PAGES
static inline int can_map_1g(phys_addr_t phys, virt_addr_t virt, size_t len) {
    // TODO: minor, VMM - check CPU supports gigantic pages
    if (phys & __PAGE_1G_MASK || virt & __PAGE_1G_MASK) { return 0; }
    if (len < (__PAGE_1G_SIZE >> PAGE_SHIFT)) { return 0; }
    return 1;
}

static inline int map_1g_page(pgd_t *pgd, phys_addr_t phys, virt_addr_t virt, u64 flags, int gfp_flags) {
    pgd_t *pdpt;

    virtmem_dev_pr("mapping 1G page at physical address %#p to virtual %#p with flags 0x%016lx\n", phys, virt, flags);

    pdpt = get_table(pgd, __PML4T_IDX(virt), gfp_flags);
    if (!pdpt) { return -ENOMEM; }

    pdpt[__PDPT_IDX(virt)] = (phys & __PAGE_ADDR_MASK) | __PG_PDE_PAGE_SIZE | flags;
    inc_pgtable_refcount(pdpt);
    invlpg(virt);
    return 0;
}
#endif

static inline int map_2m_page(pgd_t *pgd, phys_addr_t phys, virt_addr_t virt, u64 flags, int gfp_flags) {
    pgd_t *pdpt, *pdt;

    virtmem_dev_pr("mapping 2M page at physical address %#p to virtual %#p with flags 0x%016lx\n", phys, virt, flags);

    pdpt = get_table(pgd, __PML4T_IDX(virt), gfp_flags);
    if (!pdpt) { return -ENOMEM; }

    pdt = get_table(pdpt, __PDPT_IDX(virt), gfp_flags);
    if (!pdt) { return -ENOMEM; }

    pdt[__PDT_IDX(virt)] = (phys & __PAGE_ADDR_MASK) | __PG_PDE_PAGE_SIZE | flags;
    inc_pgtable_refcount(pdt);
    invlpg(virt);
    return 0;
}

static inline int map_4k_page(pgd_t *pgd, phys_addr_t phys, virt_addr_t virt, u64 flags, int gfp_flags) {
    pgd_t *pdpt, *pdt, *ptt;

    virtmem_dev_pr("mapping 4K page at physical address %#p to virtual %#p with flags 0x%016lx\n", phys, virt, flags);

    pdpt = get_table(pgd, __PML4T_IDX(virt), gfp_flags);
    if (!pdpt) { return -ENOMEM; }

    pdt = get_table(pdpt, __PDPT_IDX(virt), gfp_flags);
    if (!pdt) { return -ENOMEM; }

    ptt = get_table(pdt, __PDT_IDX(virt), gfp_flags);
    if (!ptt) { return -ENOMEM; }

    ptt[__PTT_IDX(virt)] = (phys & __PAGE_ADDR_MASK) | flags;
    inc_pgtable_refcount(ptt);
    invlpg(virt);
    return 0;
}

static int vm_arch_map(pgd_t      *pgd,
                       phys_addr_t phys,
                       virt_addr_t virt,
                       size_t      pg_cnt,
                       u64         flags,
                       int         gfp_flags,
                       bool        inc_refcnts) {
    int          res;
    struct page *pg;

    while (pg_cnt) {
#ifdef CONFIG_USE_GIGANTIC_PAGES
        if (can_map_1g(phys, virt, pg_cnt)) {
            if (inc_refcnts) {
                BUG(); // unimplemented
            }

            if ((res = map_1g_page(pgd, phys, virt, flags, gfp_flags))) { return res; }
            pg_cnt -= __PAGE_1G_SIZE >> PAGE_SHIFT;
            phys += __PAGE_1G_SIZE;
            virt += __PAGE_1G_SIZE;
            continue;
        }
#endif

        if (can_map_2m(phys, virt, pg_cnt)) {
            if ((res = map_2m_page(pgd, phys, virt, flags, gfp_flags))) { return res; }

            if (inc_refcnts) {
                pg = phys_to_page(phys);
                BUG_ON(PageBuddy(pg));
                BUG_ON(!PageHead(pg));
                BUG_ON(pg->head_order != ilog2(__PAGE_2M_PGCNT));
                refcount_inc(&pg->refcnt);
            }

            pg_cnt -= __PAGE_2M_SIZE >> PAGE_SHIFT;
            phys += __PAGE_2M_SIZE;
            virt += __PAGE_2M_SIZE;

            continue;
        }

        if (inc_refcnts) {
            pg = phys_to_page(phys);
            BUG_ON(PageBuddy(pg));
            BUG_ON(!PageHead(pg));
            BUG_ON(pg->head_order != ilog2(__PAGE_4K_PGCNT));
            refcount_inc(&pg->refcnt);
        }

        if ((res = map_4k_page(pgd, phys, virt, flags, gfp_flags))) { return res; }
        pg_cnt--;
        phys += __PAGE_4K_SIZE;
        virt += __PAGE_4K_SIZE;
    }

    BUG_ON(pg_cnt != 0);
    return 0;
}

static inline bool skip_absent(bool present, virt_addr_t *virt, size_t *pg_cnt, size_t entry_size, size_t entry_pgcnt) {
    if (present) { return false; }
    BUG_ON(*pg_cnt < entry_pgcnt);
    *virt += entry_size;
    *pg_cnt -= entry_pgcnt;
    return true;
}

#define skip_absent_pml4(ent, virt, pg_cnt)                                                                            \
    skip_absent((*(ent) & __PG_PRESENT), &(virt), &(pg_cnt), __PAGE_PML4_ENTRY_SIZE, __PAGE_PML4_ENTRY_PGCNT)

#define skip_absent_pdpt(ent, virt, pg_cnt)                                                                            \
    skip_absent((*(ent) & __PG_PRESENT), &(virt), &(pg_cnt), __PAGE_1G_SIZE, __PAGE_1G_PGCNT)

#define skip_absent_pd(ent, virt, pg_cnt)                                                                              \
    skip_absent((*(ent) & __PG_PRESENT), &(virt), &(pg_cnt), __PAGE_2M_SIZE, __PAGE_2M_PGCNT)

#define skip_absent_pt(ent, virt, pg_cnt)                                                                              \
    skip_absent((*(ent) & __PG_PRESENT), &(virt), &(pg_cnt), __PAGE_4K_SIZE, __PAGE_4K_PGCNT)

static inline bool try_free_table(pgd_t *table, pgd_t *parent_entry, pgd_t *parent_table) {
    int rc = dec_pgtable_refcount(table);
    if (rc != 1) return false;
    *parent_entry = 0;
    dec_pgtable_refcount(parent_table);
    vm_free_page_table(table);
    return true;
}

static inline void unmap_leaf(pgd_t *entry, virt_addr_t virt, struct page *pg, size_t order) {
    *entry = 0;
    invlpg(virt);
    if (refcount_get_dec(&pg->refcnt) == 1) { __pm_free_pages(pg, order); }
}

static void vm_arch_umap(pgd_t *pgd, virt_addr_t virt, size_t pg_cnt) {
    pgd_t       *pml4e, *pdpte, *pde, *pte;
    pgd_t       *pdpt, *pd, *pt;
    struct page *pg;

    while (pg_cnt) {
        pml4e = &pgd[__PML4T_IDX(virt)];
        if (skip_absent_pml4(pml4e, virt, pg_cnt)) { continue; }

        pdpt  = __va(__PAGE_ADDR(*pml4e));
        pdpte = &pdpt[__PDPT_IDX(virt)];
        if (skip_absent_pdpt(pdpte, virt, pg_cnt)) { continue; }

#ifdef CONFIG_USE_GIGANTIC_PAGES
        if (*pdpte & __PG_PDE_PAGE_SIZE) {
            BUG(); // unimplemented
        }
#else
        BUG_ON(*pdpte & __PG_PDE_PAGE_SIZE);
#endif

        pd  = __va(__PAGE_ADDR(*pdpte));
        pde = &pd[__PDT_IDX(virt)];
        if (skip_absent_pd(pde, virt, pg_cnt)) { continue; }

        if (*pde & __PG_PDE_PAGE_SIZE) {
            pg = phys_to_page(__PAGE_ADDR(*pde));
            unmap_leaf(pde, virt, pg, ilog2(__PAGE_2M_PGCNT));

            BUG_ON(virt & (__PAGE_2M_SIZE - 1));
            BUG_ON(pg_cnt < __PAGE_2M_PGCNT);
            virt += __PAGE_2M_SIZE;
            pg_cnt -= __PAGE_2M_PGCNT;

            if (!try_free_table(pd, pdpte, pdpt)) { continue; }
            if (!try_free_table(pdpt, pml4e, pgd)) { continue; }

            continue;
        }

        pt  = __va(__PAGE_ADDR(*pde));
        pte = &pt[__PTT_IDX(virt)];
        if (skip_absent_pt(pte, virt, pg_cnt)) { continue; }


        pg = phys_to_page(__PAGE_ADDR(*pte));
        unmap_leaf(pte, virt, pg, 1);

        virt += PAGE_SIZE;
        pg_cnt -= __PAGE_4K_PGCNT;

        if (!try_free_table(pt, pde, pd)) { continue; }
        if (!try_free_table(pd, pdpte, pdpt)) { continue; }
        if (!try_free_table(pdpt, pml4e, pgd)) { continue; }
    }
}

int vm_map(pgd_t *pgd, phys_addr_t phys, virt_addr_t virt, size_t len, unsigned long flags, int gfp_flags) {
    u64 pte_base_flags;

    if (!pgd || len == 0) { return -EINVAL; }
    if (phys & __PAGE_4K_MASK || virt & __PAGE_4K_MASK || len & __PAGE_4K_MASK) { return -EINVAL; }

    pte_base_flags = get_pte_flags(flags);
    return vm_arch_map(pgd, phys, virt, len >> PAGE_SHIFT, pte_base_flags, gfp_flags, true);
}

int vm_map_raw(pgd_t *pgd, phys_addr_t phys, virt_addr_t virt, size_t len, unsigned long flags, int gfp_flags) {
    u64 pte_base_flags;

    if (!pgd || len == 0) { return -EINVAL; }
    if (phys & __PAGE_4K_MASK || virt & __PAGE_4K_MASK || len & __PAGE_4K_MASK) { return -EINVAL; }

    pte_base_flags = get_pte_flags(flags);
    return vm_arch_map(pgd, phys, virt, len >> PAGE_SHIFT, pte_base_flags, gfp_flags, false);
}

int vm_umap(pgd_t *pgd, virt_addr_t virt, size_t len) {
    if (!pgd) { return -EINVAL; }
    if (virt & __PAGE_4K_MASK || len & __PAGE_4K_SIZE) { return -EINVAL; }

    len >>= PAGE_SHIFT;
    vm_arch_umap(pgd, virt, len);

    return 0;
}

int vm_copy_kernel(pgd_t *dst, pgd_t *src) {
    memcpy(&dst[256], &src[256], 256 * 8);
    return 0;
}

int vm_copy_user(pgd_t *dst, pgd_t *src, int flags) {
    pgd_t      *dpml4t = dst;
    pgd_t      *spml4t = src;
    pgd_t      *dpdpt, *dpdt, *dptt;
    pgd_t      *spdpt, *spdt, *sptt;
    phys_addr_t cpybuffer;

    for (size_t i = 0; i < 256; i++) {
        if (!(spml4t[i] & __PG_PRESENT)) { continue; }
        dpdpt     = vm_get_page_table(flags);
        dpml4t[i] = ((phys_addr_t) __pa(dpdpt) & __PAGE_ADDR_MASK) | (spml4t[i] & ~__PAGE_ADDR_MASK);
        spdpt     = (pgd_t *) __va(__PAGE_ADDR(spml4t[i]));

        for (size_t j = 0; j < __PAGE_TABLE_ENTRY_COUNT; j++) {
            if (!(spdpt[j] & __PG_PRESENT)) { continue; }
            if (spdpt[j] & __PG_PDE_PAGE_SIZE) { panic("gigantic page in fork"); }
            dpdt     = vm_get_page_table(flags);
            dpdpt[j] = ((phys_addr_t) __pa(dpdt) & __PAGE_ADDR_MASK) | (spdpt[j] & ~__PAGE_ADDR_MASK);
            spdt     = (pgd_t *) __va(__PAGE_ADDR(spdpt[j]));

            for (size_t k = 0; k < __PAGE_TABLE_ENTRY_COUNT; k++) {
                if (!(spdt[k] & __PG_PRESENT)) { continue; }
                if (spdt[k] & __PG_PDE_PAGE_SIZE) {
                    cpybuffer = __pm_get_free_pages(flags | __M_ZERO, 9);
                    if (cpybuffer == INVALID_PHYS_ADDR) { return -ENOSPC; }
                    memcpy(__va(cpybuffer), __va(spdt[k] & __PAGE_ADDR_MASK), __PAGE_2M_SIZE);
                    dpdt[k] = (cpybuffer & __PAGE_ADDR_MASK) | (spdt[k] & ~__PAGE_ADDR_MASK);
                    continue;
                }

                dptt    = vm_get_page_table(flags);
                dpdt[k] = ((phys_addr_t) __pa(dptt) & __PAGE_ADDR_MASK) | (spdt[k] & ~__PAGE_ADDR_MASK);
                sptt    = (pgd_t *) __va(__PAGE_ADDR(spdt[k]));

                for (size_t l = 0; l < __PAGE_TABLE_ENTRY_COUNT; l++) {
                    if (!(sptt[l] & __PG_PRESENT)) { continue; }
                    cpybuffer = __pm_get_free_page(flags | __M_ZERO);
                    if (cpybuffer == INVALID_PHYS_ADDR) { return -ENOSPC; }
                    memcpy(__va(cpybuffer), __va(sptt[l] & __PAGE_ADDR_MASK), __PAGE_4K_SIZE);
                    dptt[l] = (cpybuffer & __PAGE_ADDR_MASK) | (sptt[l] & ~__PAGE_ADDR_MASK);
                }
            }
        }
    }

    return 0;
}

// TODO: performance, VM - this is naive copy. Implement COW
int vm_copy(pgd_t *dst, pgd_t *src, int flags) {
    int res;

    if ((res = vm_copy_kernel(dst, src)) != 0) { return res; }
    return vm_copy_user(dst, src, flags);
}

void vm_free_user(pgd_t *pgd) {
    vm_arch_umap(pgd, ARCH_USER_START, (ARCH_USER_END - ARCH_USER_START + 1) >> PAGE_SHIFT);
}

void vm_activate(pgd_t *pgd) {
    phys_addr_t pgd_addr = (phys_addr_t) __pa(pgd);

    __asm__ volatile("mov %0, %%cr3"
                     :
                     : "r"(pgd_addr)
                     : "memory");
}

static pgd_t *get_entry(pgd_t *pgd, virt_addr_t virt) {
    pgd_t *pdpt, *pd, *pt;
    pgd_t *pml4e, *pdpte, *pde, *pte;

    pml4e = &pgd[__PML4T_IDX(virt)];
    if (!(*pml4e & __PG_PRESENT)) { return NULL; }
    pdpt = __va(__PAGE_ADDR(*pml4e));

    pdpte = &pdpt[__PDPT_IDX(virt)];
    if (!(*pdpte & __PG_PRESENT)) { return NULL; }
#ifdef CONFIG_USE_GIGANTIC_PAGES
    if ((*pdpte & __PG_PDE_PAGE_SIZE)) { return pdpte; }
#endif
    pd = __va(__PAGE_ADDR(*pdpte));

    pde = &pd[__PDT_IDX(virt)];
    if (!(*pde & __PG_PRESENT)) { return NULL; }
    if ((*pde & __PG_PDE_PAGE_SIZE)) { return pde; }
    pt = __va(__PAGE_ADDR(*pde));

    pte = &pt[__PTT_IDX(virt)];
    if (!(*pte & __PG_PRESENT)) { return NULL; }
    return pte;
}

phys_addr_t vm_getphys(pgd_t *pgd, virt_addr_t virt) {
    pgd_t *entry = get_entry(pgd, virt);
    if (entry) { return __PAGE_ADDR(*entry); }
    return INVALID_PHYS_ADDR;
}

static inline int __access_ok(pgd_t *pte, virt_addr_t virt, size_t len) {
    if (virt < ARCH_USER_START || virt + len > ARCH_USER_END) { return 0; }

    if (!pte) { return 0; }
    if (!(*pte & __PG_PRESENT)) { return 0; }
    if (!(*pte & __PG_USER)) { return 0; }


    return 1;
}

int vm_copyout(pgd_t *pgd, virt_addr_t dst_virt, char *src, size_t len) {
    size_t      n;
    phys_addr_t pa;
    phys_addr_t va;
    pgd_t      *entry;

    while (len > 0) {
        va = PG_ALIGN_DN(dst_virt);
        pa = vm_getphys(pgd, va);
        if (pa == INVALID_PHYS_ADDR) { return -EADDRNOTAVAIL; }

        entry = get_entry(pgd, va);
        if (!(*entry & __PG_WRITE)) { return -EACCES; }

        n = PAGE_SIZE - (dst_virt - va);
        if (n > len) { n = len; }

        if (!__access_ok(entry, va, n)) { return -EACCES; }
        memmove(__va(pa + (dst_virt - va)), src, n);

        len -= n;
        src += n;
        dst_virt = va + PAGE_SIZE;
    }

    return 0;
}

int vm_copyin(pgd_t *pgd, char *dst, virt_addr_t src_virt, size_t len) {
    size_t      n;
    phys_addr_t pa;
    virt_addr_t va;

    while (len > 0) {
        va = PG_ALIGN_DN(src_virt);
        pa = vm_getphys(pgd, va);

        if (pa == INVALID_PHYS_ADDR) { return -EADDRNOTAVAIL; }

        n = PAGE_SIZE - (src_virt - va);
        if (n > len) { n = len; }

        if (!vm_access_ok(pgd, va, n)) { return -EACCES; }
        memmove(dst, __va(pa + (src_virt - va)), n);

        len -= n;
        dst += n;
        src_virt = va + PAGE_SIZE;
    }

    return 0;
}

int vm_copyinstr(pgd_t *pgd, void *dst, virt_addr_t srcva, size_t len, size_t *done) {
    size_t      n, i;
    phys_addr_t pa;
    virt_addr_t va;
    char       *d;
    const char *s;

    *done = 0;

    while (len > 0) {
        va = PG_ALIGN_DN(srcva);
        pa = vm_getphys(pgd, va);

        if (pa == INVALID_PHYS_ADDR) { return -EADDRNOTAVAIL; }

        n = PAGE_SIZE - (srcva - va);
        if (n > len) { n = len; }

        if (!vm_access_ok(pgd, va, n)) { return -EACCES; }

        d = (char *) dst;
        s = __va(pa + (srcva - va));

        i = n;
        if (d < s) {
            while (i--) {
                if (!*s) {
                    *d = '\0';
                    (*done)++;
                    return 0;
                }
                *d++ = *s++;
                (*done)++;
            }
        } else {
            char *lasts = (char *) (s + n - 1);
            char *lastd = d + n - 1;

            while (i--) {
                if (!*lasts) {
                    *d = '\0';
                    (*done)++;
                    return 0;
                }
                *lastd-- = *lasts--;
                (*done)++;
            }
        }

        len -= n;
        dst   = d;
        srcva = va + PAGE_SIZE;
    }

    return -ENAMETOOLONG;
}

int vm_access_ok(pgd_t *pgd, virt_addr_t virt, size_t len) {
    pgd_t *pte;
    if (virt < ARCH_USER_START || virt + len > ARCH_USER_END) { return 0; }

    pte = get_entry(pgd, virt);

    if (!pte) { return 0; }
    if (!(*pte & __PG_PRESENT)) { return 0; }
    if (!(*pte & __PG_USER)) { return 0; }


    return 1;
}
