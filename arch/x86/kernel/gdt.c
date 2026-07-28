#include <nyx/types.h>

#include <asi/desc.h>
#include <asi/descriptors.h>
#include <asi/gdt.h>
#include <asi/system.h>
#include <asi/tss.h>

extern struct segment_desc gdt[GDT_ENTRIES];
extern struct desc_ptr     gdt_desc;

void gdt_init() {
    load_gdt(&gdt_desc);
    ltr(TSS_SEGMENT);
}

void gdt_setup() {
    struct ssd_data *tss       = (struct ssd_data *) &gdt[6];
    virt_addr_t      tss_base  = (virt_addr_t) &default_tss;
    virt_addr_t      tss_limit = (virt_addr_t) ((char *) &default_tss + sizeof(struct tss));

    tss->limit0      = tss_limit & 0xFFFF;
    tss->base0       = tss_base & 0xFFFF;
    tss->base1       = (tss_base >> 16) & 0xFF;
    tss->base2       = (tss_base >> 24) & 0xFF;
    tss->base3       = (tss_base >> 32) & 0xFFFFFFFF;
    tss->bits.limit1 = (tss_limit >> 16) & 0xF;
    tss->bits.dpl    = DPL0;
    tss->bits.type   = SEG_TSS;
    tss->bits.p      = 1;
    tss->bits.zero0  = 0;
    tss->bits.zero1  = 0;
    tss->bits.g      = 0;
    tss->zero        = 0;
}
