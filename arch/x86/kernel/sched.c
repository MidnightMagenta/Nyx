#include <nyx/proc.h>
#include <nyx/types.h>

#include <asi/page.h>
#include <asi/tss.h>

void arch_schedule_tail(struct thread *prev, struct thread *next) {
    (void) prev;
    default_tss.rsp[0] = (u64) next->t_kstack + PAGE_SIZE;
}
