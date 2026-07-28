#include <nyx/current.h>
#include <nyx/linkage.h>
#include <nyx/list.h>
#include <nyx/percpu.h>
#include <nyx/proc.h>
#include <nyx/sched.h>
#include <nyx/sched_percpu.h>
#include <nyx/string.h>
#include <nyx/types.h>

#include <asi/bug.h>
#include <asi/cpu.h>
#include <asi/irq.h>
#include <asi/page.h>

extern struct thread *context_switch(struct thread *prev, struct thread *next);

extern void proc_init();
extern void proc0_init();
extern void map_kernel();
extern void wait_init();
extern void exit_tail(struct thread *t);
extern void arch_schedule_tail(struct thread *prev, struct thread *next);

void __init init_sched() {
    proc0_init();
    map_kernel();
    get_pcpu()->current_task         = &proc0;
    get_pcpu()->scheds.cpu_idle_proc = &proc0;
    get_pcpu()->scheds.flags         = 0;
    list_init(&get_pcpu()->scheds.runq);
    proc_init();
    wait_init();
}

static struct thread *pick_next(struct sched_percpu *schedc) {
    struct thread *next = NULL;

    if (!list_is_empty(&schedc->runq)) {
        next = list_first_entry(&schedc->runq, struct thread, t_qnode);
        rmrunqueue(next);
    }

    return next ? next : schedc->cpu_idle_proc;
}

void schedule_tail(struct thread *prev, struct thread *next) {
    arch_schedule_tail(prev, next);
    get_pcpu()->rsp0 = (u64) next->t_kstack + PAGE_SIZE;
    if (prev->t_state == TS_ZOMBIE) { exit_tail(prev); }
}

void schedule() {
    struct sched_percpu *schedc = &get_pcpu()->scheds;
    struct thread       *prev, *next;
    flags_t              flags;

    if (list_is_empty(&schedc->runq) && current()->t_state == TS_RUNNING) { return; }

    flags = arch_irq_save();

    prev = current();
    next = pick_next(schedc);

    schedc->flags &= ~SCHED_NEED_RESCHED;

    if (next == schedc->cpu_idle_proc && prev->t_state == TS_RUNNING) {
        arch_irq_restore(flags);
        return;
    }

    if (prev->t_state == TS_RUNNING) {
        prev->t_state = TS_RUNNABLE;
        if (prev != schedc->cpu_idle_proc) { setrunqueue(prev->t_cpu, prev); }
    }

    get_pcpu()->current_task = next;
    next->t_state            = TS_RUNNING;

    prev = context_switch(prev, next);

    arch_irq_restore(flags);

    schedule_tail(prev, current());
}

struct cpu_info *sched_pickcpu(struct thread *t) {
    // TODO: fudged function

    (void) t;
    return &__cpus[0];
}

void setrunqueue(struct cpu_info *cpu, struct thread *t) {
    struct sched_percpu *schedc;
    flags_t              flags;

    BUG_ON(!list_is_empty(&t->t_qnode));

    if (cpu == NULL) { cpu = sched_pickcpu(t); }

    flags = arch_irq_save();

    t->t_cpu   = cpu;
    t->t_state = TS_RUNNABLE;

    schedc = &t->t_cpu->percpu->scheds;
    schedc->nr_run++;
    list_add_tail(&t->t_qnode, &schedc->runq);

    arch_irq_restore(flags);
}

void rmrunqueue(struct thread *t) {
    struct sched_percpu *schedc;
    flags_t              flags;

    flags = arch_irq_save();

    schedc = &t->t_cpu->percpu->scheds;
    schedc->nr_run--;
    list_del(&t->t_qnode);

    arch_irq_restore(flags);
}

void scheduler_tick() {
    if (!list_is_empty(&get_pcpu()->scheds.runq)) { get_pcpu()->scheds.flags |= SCHED_NEED_RESCHED; }
}
