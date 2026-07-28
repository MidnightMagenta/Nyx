#include <mm/memblock.h>
#include <mm/mmzone.h>
#include <nyx/linkage.h>

struct pg_data_s  contigmem_pagedata;
struct pg_data_s *pgdata = &contigmem_pagedata;

extern void init_page_alloc();
extern void arch_init_memory();
extern void kmem_cache_init();
extern void kmalloc_init();
extern void vmspace_init();

void __init init_memory() {
    arch_init_memory();
    init_page_alloc();
    memblock_print_regions();
    memblock_free_all();
    kmem_cache_init();
    kmalloc_init();
    vmspace_init();
}
