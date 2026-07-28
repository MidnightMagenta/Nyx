#include <nyx/percpu.h>
#include <nyx/proc.h>
#include <nyx/stddef.h>

#include <asi/proc.h>

#define ASM_OFFSET(name, st, member) __asm__ volatile("\n->" #name " %0" ::"i"(offsetof(st, member)))

void __asm_offsets_dummy_fn() {
    ASM_OFFSET(TASK_CONTEXT_RSP, struct thread, t_ctx.rsp);
    ASM_OFFSET(RSP0, struct percpu, rsp0);
    ASM_OFFSET(URSP, struct percpu, ursp);
}
