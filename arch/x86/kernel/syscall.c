#include <nyx/current.h>
#include <nyx/errno.h>
#include <nyx/syscall.h>
#include <nyx/types.h>

#include <asi/cpu.h>
#include <asi/gdt.h>
#include <asi/msr.h>
#include <asi/traps.h>

extern void syscall_entry();

#define SFMASK 0x200

void syscall_init() {
    u64 efer;

    wrmsr(MSR_STAR, ((u64) USER_CODE32_SEGMENT << 48) | ((u64) KERNEL_CODE_SEGMENT << 32));
    wrmsr(MSR_LSTAR, (u64) syscall_entry);
    wrmsr(MSR_CSTAR, 0);
    wrmsr(MSR_SFMASK, (u64) SFMASK);

    efer = rdmsr(MSR_EFER);
    efer |= EFER_SCE;
    wrmsr(MSR_EFER, efer);
}

// args in rdi, rsi, rdx, r10, r8, r9
// syscall number in rax
void __syscall_handler(struct trap_frame *tf) {
    register_t          retval;
    int                 err;
    struct syscall_args args = {
            tf->regs.rdi,
            tf->regs.rsi,
            tf->regs.rdx,
            tf->regs.r10,
            tf->regs.r8,
            tf->regs.r9,
    };

    if (tf->regs.rax >= syscall_table_size || !syscall_table[tf->regs.rax]) {
        tf->regs.rax = -ENOSYS;
        return;
    }

    err          = syscall_table[tf->regs.rax](current(), &args, &retval);
    tf->regs.rax = err ? (register_t) err : retval;
}
