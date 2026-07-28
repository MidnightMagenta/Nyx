#include <mm/vmspace.h>
#include <nyx/proc.h>

extern struct thread *switch_to(struct thread *prev, struct thread *next);

struct thread *context_switch(struct thread *prev, struct thread *next) {
    if (prev->proc->mm != next->proc->mm) { vmspace_activate(next->proc->mm); }

    return switch_to(prev, next);
}
