#include <mm/memblock.h>
#include <mm/memory.h>
#include <mm/mm_types.h>
#include <mm/mmzone.h>
#include <mm/virtmem.h>
#include <nyx/align.h>
#include <nyx/linkage.h>
#include <nyx/panic.h>
#include <nyx/proc.h>
#include <nyx/types.h>

#include <asi/bootparam.h>
#include <asi/link_symbols.h>
#include <asi/memory.h>
#include <asi/mmap.h>
#include <asi/page.h>
#include <asi/setupdata.h>

extern void idt_setup_interrupts();
extern void init_percpu();
extern void cpu_init();
extern void gdt_setup();

static void __init add_memblock_regions() {
    u64         rb, re, idx;
    mmap_type_t rt;

    idx = 0;
    while (mmap_get_next(&rb, &re, &rt, &idx)) {
        memblock_add(rb, re - rb);
        if (!mmap_type_is_memory(rt)) { memblock_reserve(rb, re - rb); }
    }

    memblock_trim();
}

void __init setup_arch() {
    idt_setup_interrupts();
    gdt_setup();
    mmap_setup_map();
    memblock_init();
    add_memblock_regions();
    init_percpu();
    cpu_init();
}

extern struct process proc0_proc;

#define map_symbol(sym_start, sym_end, flags)                                                                          \
    vm_map_raw(proc0_proc.p_mm->v_pgd,                                                                                 \
               ALIGN_DOWN(load_base + SYMBOL_OFFSET((sym_start)), PAGE_SIZE),                                          \
               (virt_addr_t) (sym_start),                                                                              \
               ALIGN_UP(((char *) (sym_end) - (char *) (sym_start)), PAGE_SIZE),                                       \
               flags,                                                                                                  \
               M_SLEEPOK)

#define get_page_count()   (pgdata->spanned_pages)
#define get_start_of_mem() (pgdata->start_pfn << PAGE_SHIFT)

void __init map_kernel() {
    u64 load_base = bootparams->kernel_load_base;

    proc0_proc.p_mm->v_pgd = vm_get_page_table(M_SLEEPOK);
    if (!proc0_proc.p_mm->v_pgd) { early_panic("could not allocate kernel page table"); }

    map_symbol(__text_start, __text_end, VM_READ | VM_EXEC);
    map_symbol(__rodata_start, __rodata_end, VM_READ);
    map_symbol(__data_start, __data_end, VM_READ | VM_WRITE);
    map_symbol(__init_start, __init_end, VM_READ | VM_WRITE | VM_EXEC);
#ifdef CONFIG_KERNEL_TESTS
    map_symbol(__kernel_tests_start, __kernel_tests_end, VM_READ | VM_WRITE | VM_EXEC);
#endif

    vm_map_raw(proc0_proc.p_mm->v_pgd,
               get_start_of_mem(),
               ARCH_DIRECT_MAP_BASE,
               get_page_count() << PAGE_SHIFT,
               VM_READ | VM_WRITE,
               M_SLEEPOK);
    vm_activate(proc0_proc.p_mm->v_pgd);
}
