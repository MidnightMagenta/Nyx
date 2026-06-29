#include <mm/physmem.h>
#include <mm/vmspace.h>
#include <nyx/atomic.h>
#include <nyx/current.h>
#include <nyx/errno.h>
#include <nyx/list.h>
#include <nyx/proc.h>
#include <nyx/sched.h>
#include <nyx/string.h>
#include <nyx/syscall.h>
#include <uapi/posix_types.h>

#include <asi/address.h>

extern void arch_fork(struct thread *t1, struct thread *t2, void (*func)(void *), void *arg);
extern void child_return(void *arg);

int sys_fork(struct thread *t, struct syscall_args *args, register_t *retval) {
    (void) args;
    return do_fork(t, FORK_FORK, child_return, NULL, retval, NULL);
}

int sys_vfork(struct thread *t, struct syscall_args *args, register_t *retval) {
    (void) args;
    return do_fork(t, FORK_VFORK | FORK_SHAREVM, child_return, NULL, retval, NULL);
}

static inline int new_process(struct process *parent, int flags, struct process **newpr) {
    struct process *pr = alloc_proc(M_SLEEPOK);

    if (!pr) { return -ENOMEM; }

    list_init(&pr->thrds_list);
    list_init(&pr->children);
    list_init(&pr->siblings);

    atomic_store_explicit(&pr->flags, 0, ATOMIC_RELAXED);
    pr->parent  = parent;
    pr->state   = PS_NEW;
    pr->pid     = get_pid();
    pr->xstatus = 0;
    pr->cwd     = parent->cwd;
    refcount_init(&pr->live_thrd_cnt, 1);

    memcpy(pr->name, parent->name, PROC_NAME_LEN);

    if (flags & FORK_NOZOMBIE) { atomic_fetch_or(&pr->flags, PF_NOZOMBIE, ATOMIC_RELAXED); }

    atomic_fetch_or(&pr->flags, PF_EMBRYO, ATOMIC_RELAXED);

    list_add_tail(&pr->siblings, &parent->children);
    list_add_tail(&pr->gproc_node, &proc_list);

    *newpr = pr;
    return 0;
}

static inline int new_thread(struct process *parent, struct thread **newt) {
    struct thread *t = alloc_thread(M_SLEEPOK);
    phys_addr_t    kstack_phys;

    if (!t) { return -ENOMEM; }

    atomic_store_explicit(&t->flags, 0, ATOMIC_RELAXED);
    t->state = TS_NEW;

    t->tid   = get_tid();
    t->proc  = parent;
    t->wchan = NULL;
    t->wmesg = NULL;
    list_init(&t->qnode);
    list_init(&t->thrd_node);
    list_init(&t->gthrd_node);

    kstack_phys = pm_get_zeroed_page(M_SLEEPOK);
    if (kstack_phys == INVALID_PHYS_ADDR) {
        free_thread(t);
        return -ENOMEM;
    }
    t->kstack = __va(kstack_phys);

    list_add_tail(&t->thrd_node, &parent->thrds_list);
    list_add_tail(&t->gthrd_node, &thread_list);

    *newt = t;
    return 0;
}

static inline int fork_vmspace(struct process *parent, struct process *pr, int flags) {
    if (flags & FORK_SHAREVM) {
        pr->mm = vmspace_share(parent);
        if (!pr->mm) { return -ENOSPC; }
    } else {
        pr->mm = vmspace_fork(parent);
        if (!pr->mm) { return -ENOMEM; }
    }

    return 0;
}

static inline int fork_files(struct process *parent, struct process *pr, int flags) {
    if (flags & FORK_SHAREFD) {
        pr->files = files_share(parent);
        if (!pr->files) { return -ENOSPC; }
    } else {
        pr->files = files_fork(parent);
        if (!pr->files) { return -ENOMEM; }
    }

    return 0;
}

static inline void fork_start_thread(struct thread *t) {
    setrunqueue(NULL, t);
}

int do_fork(struct thread  *curp,
            int             flags,
            void            (*func)(void *),
            void           *arg,
            register_t     *retval,
            struct thread **newproc) {
    int             err;
    struct process *curpr = curp->proc;
    struct process *newpr;
    struct thread  *newthrd;

    if ((err = new_process(curpr, flags, &newpr))) { goto fail0; }
    if ((err = fork_vmspace(curpr, newpr, flags))) { goto fail1; }
    if ((err = fork_files(curpr, newpr, flags))) { goto fail2; }
    if ((err = new_thread(newpr, &newthrd))) { goto fail3; }

    arch_fork(curp, newthrd, func, arg ? arg : curp);

    if (newproc) { *newproc = newthrd; }
    if (retval) { *retval = newpr->pid; }

    newpr->state = PS_NORMAL;

    atomic_fetch_and(&newpr->flags, ~PF_EMBRYO, ATOMIC_RELEASE);

    fork_start_thread(newthrd);

    return 0;

fail3:
fail2:
    vmspace_put(newpr->mm);
fail1:
    free_proc(newpr);
fail0:
    return err;
}
