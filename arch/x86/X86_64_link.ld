ENTRY(startup_64)

PHDRS {
    init_text   PT_LOAD FLAGS(5);          /* R_E */
    init_data   PT_LOAD FLAGS(6);          /* RW_ */
    text        PT_LOAD FLAGS(5);          /* R_E */
    data        PT_LOAD FLAGS(6);          /* RW_ */
/*  note        PT_NOTE FLAGS(0);          /* ___ */
}

#define ARCH_KERNEL_BASE 0xFFFFFFFF80000000

SECTIONS {
    . = ARCH_KERNEL_BASE;
    __image_start = .;
    __kernel_start = .;

    .text BLOCK(0x1000) : {
        __text_start = .;
        *(.text*)
        __text_end = .;
    } :text

    .rodata BLOCK(0x1000) : {
        __rodata_start = .;
        *(.rodata*)
        __rodata_end = .;
    } :text

    .data BLOCK(0x1000) : {
        __data_start = .;
        *(.data*)
    } :data
 
    .bss BLOCK(0x1000) (NOLOAD) : {
        __bss_start = .;
        *(COMMON)
        *(.bss*)
        __bss_end = .;
        __data_end = .;
    } :data
 
    __kernel_end = .;

    . = ALIGN(0x1000);
    __init_start = .;

    .init.text BLOCK(0x1000) : {
        *(.init.text*)
        *(.init.rodata*)
        . = ALIGN(8);
        __initcalls_start = .;
        KEEP(*(.initcalls.early))
        __initcalls_end = .;
    } :init_text

    .init.data BLOCK(0x1000) : {
        *(.init.data*)
    } :init_data

    .init.bss BLOCK(0x1000) (NOLOAD) : {
        *(.init.bss*)
    } :init_data

    __init_end = .;
    
#ifdef CONFIG_KERNEL_TESTS
    .kernel_tests BLOCK(0x1000) : {
        __kernel_tests_start = .;
        KEEP(*(.kernel_tests*))
        __kernel_tests_end = .;
    } :init_data
#endif

    __image_end = .;

    /DISCARD/ : {
        *(.note.gnu.property)
        *(.eh_frame)
#ifndef CONFIG_KERNEL_TESTS
        *(.kernel_tests)
#endif
    }
}
