#include <nyx/proc.h>
#include <nyx/string.h>
#include <nyx/types.h>

#include <asi/cpu.h>
#include <asi/page.h>
#include <asi/proc.h>
#include <asi/traps.h>

extern void proc_trampoline();

void arch_fork(struct thread *t1, struct thread *t2, void (*func)(void *), void *arg) {
    struct switchframe *sf = (struct switchframe *) ((char *) t2->kstack + PAGE_SIZE) - 1;
    struct trap_frame  *tf = (struct trap_frame *) ((char *) t1->kstack + PAGE_SIZE) - 1;

    t2->ctx.rsp = (u64) sf;
    memcpy(&sf->tf, tf, sizeof(struct trap_frame));

    sf->tf.regs.rax = 0;

    sf->r12 = (u64) func;
    sf->r13 = (u64) arg;
    sf->rip = (u64) proc_trampoline;
}
