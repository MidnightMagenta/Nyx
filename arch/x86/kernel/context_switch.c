#include <mm/vmspace.h>
#include <nyx/proc.h>

extern struct thread *switch_to(struct thread *prev, struct thread *next);

struct thread *context_switch(struct thread *prev, struct thread *next) {
    if (prev->t_proc->p_mm != next->t_proc->p_mm) { vmspace_activate(next->t_proc->p_mm); }

    return switch_to(prev, next);
}
