#include <mm/mm_types.h>
#include <mm/physmem.h>
#include <mm/vmspace.h>
#include <nyx/atomic.h>
#include <nyx/errno.h>
#include <nyx/list.h>
#include <nyx/printk.h>
#include <nyx/proc.h>
#include <nyx/refcount.h>
#include <nyx/sched.h>
#include <nyx/string.h>
#include <nyx/types.h>
#include <nyx/vfs.h>

#include <asi/address.h>

#ifdef CONFIG_DEBUG_FORK
#define pr_fork_debug(fmt, ...) printk("syscall/fork:%d: " fmt, __LINE__, ##__VA_ARGS__)
#else
#define pr_wait_debug(fmt, ...) /* void */
#endif

struct syscall_args;

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

    list_init(&pr->p_thrds_list);
    list_init(&pr->p_children);
    list_init(&pr->p_siblings);

    atomic_store_explicit(&pr->p_flags, 0, ATOMIC_RELAXED);
    pr->p_parent  = parent;
    pr->p_state   = PS_NEW;
    pr->p_pid     = get_pid();
    pr->p_xstatus = 0;
    refcount_init(&pr->p_live_thrd_cnt, 1);

    memcpy(pr->p_name, parent->p_name, PROC_NAME_LEN);

    if (flags & FORK_NOZOMBIE) { atomic_fetch_or(&pr->p_flags, PF_NOZOMBIE, ATOMIC_RELAXED); }

    atomic_fetch_or(&pr->p_flags, PF_EMBRYO, ATOMIC_RELAXED);

    list_add_tail(&pr->p_siblings, &parent->p_children);
    list_add_tail(&pr->p_gproc_node, &proc_list);

    *newpr = pr;
    return 0;
}

static inline int new_thread(struct process *parent, struct thread **newt) {
    struct thread *t = alloc_thread(M_SLEEPOK);
    phys_addr_t    kstack_phys;

    if (!t) { return -ENOMEM; }

    atomic_store_explicit(&t->t_flags, 0, ATOMIC_RELAXED);
    t->t_state = TS_NEW;

    t->t_tid   = get_tid();
    t->t_proc  = parent;
    t->t_wchan = NULL;
    t->t_wmesg = NULL;
    list_init(&t->t_qnode);
    list_init(&t->t_thrd_node);
    list_init(&t->t_gthrd_node);

    kstack_phys = pm_get_zeroed_page(M_SLEEPOK);
    if (kstack_phys == INVALID_PHYS_ADDR) {
        free_thread(t);
        return -ENOMEM;
    }
    t->t_kstack = __va(kstack_phys);

    list_add_tail(&t->t_thrd_node, &parent->p_thrds_list);
    list_add_tail(&t->t_gthrd_node, &thread_list);

    *newt = t;
    return 0;
}

static inline int fork_vmspace(struct process *parent, struct process *pr, int flags) {
    if (flags & FORK_SHAREVM) {
        pr->p_mm = vmspace_share(parent);
        if (!pr->p_mm) { return -ENOSPC; }
    } else {
        pr->p_mm = vmspace_fork(parent);
        if (!pr->p_mm) { return -ENOMEM; }
    }

    return 0;
}

static inline int fork_files(struct process *parent, struct process *pr, int flags) {
    (void) flags;

    pr->p_fd = fdcopy(parent->p_fd);
    if (!pr->p_fd) { return -ENOMEM; }

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
    struct process *curpr = curp->t_proc;
    struct process *newpr;
    struct thread  *newthrd;

    if ((err = new_process(curpr, flags, &newpr))) { goto fail0; }
    if ((err = fork_vmspace(curpr, newpr, flags))) { goto fail1; }
    if ((err = fork_files(curpr, newpr, flags))) { goto fail2; }
    if ((err = new_thread(newpr, &newthrd))) { goto fail3; }

    arch_fork(curp, newthrd, func, arg ? arg : curp);

    if (newproc) { *newproc = newthrd; }
    if (retval) { *retval = newpr->p_pid; }

    pr_fork_debug("forked process [pid: %d] from process [pid: %d]\n", newpr->p_pid, curp->t_proc->p_pid);

    newpr->p_state = PS_NORMAL;

    atomic_fetch_and(&newpr->p_flags, ~PF_EMBRYO, ATOMIC_RELEASE);

    fork_start_thread(newthrd);

    return 0;

fail3:
    fdfree(newpr);
fail2:
    vmspace_put(newpr->p_mm);
fail1:
    free_proc(newpr);
fail0:
    return err;
}
