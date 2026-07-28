#include <nyx/percpu.h>
#include <nyx/types.h>

#include <asi/msr.h>

extern void syscall_init();
extern void gdt_init();
extern void idt_init();

void cpu_init() {
    syscall_init();
    gdt_init();
    idt_init();
    wrmsr(MSR_KERNEL_GS_BASE, (u64) &__pcpu[0]); // TODO: get cpuid
    asm volatile("swapgs");
}
