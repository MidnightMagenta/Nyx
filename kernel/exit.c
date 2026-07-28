#include <mm/physmem.h>
#include <mm/vmspace.h>
#include <nyx/atomic.h>
#include <nyx/compiler.h>
#include <nyx/errno.h>
#include <nyx/list.h>
#include <nyx/panic.h>
#include <nyx/printk.h>
#include <nyx/proc.h>
#include <nyx/refcount.h>
#include <nyx/sched.h>
#include <nyx/stddef.h>
#include <nyx/syscall.h>
#include <nyx/types.h>
#include <nyx/vfs.h>
#include <nyx/wait.h>
#include <uapi/posix_types.h>

#include <asi/address.h>
#include <asi/bug.h>
#include <asi/system.h>

#define pr_fmt(fmt) "syscall: " fmt

#ifdef CONFIG_DEBUG_EXIT
#define pr_exit_debug(fmt, ...) printk("syscall/exit:%d: " fmt, __LINE__, ##__VA_ARGS__)
#else
#define pr_exit_debug(fmt, ...) /* void */
#endif

#ifdef CONFIG_DEBUG_WAIT
#define pr_wait_debug(fmt, ...) printk("syscall/wait:%d: " fmt, __LINE__, ##__VA_ARGS__)
#else
#define pr_wait_debug(fmt, ...) /* void */
#endif

static LIST_HEAD(deadqueue);

void proc_zap(struct process *pr);

int __noreturn sys_exit(struct thread *t, struct syscall_args *args, register_t *retval) {
    (void) retval;
    (void) t;
    do_exit(t, args->arg1, EXIT_NORMAL);

    __unreachable;
    hcf();
}

static void reparent_children(struct process *pr) {
    struct list_head *cur, *n;
    struct process   *child;

    list_for_each_safe(cur, n, &pr->p_children) {
        child = list_entry(cur, struct process, p_siblings);

        list_move_tail(&child->p_siblings, &initproc->t_proc->p_children);
        child->p_parent = initproc->t_proc;
    }
}

// TODO: major - assumes single threaded
void __noreturn do_exit(struct thread *t, int code, int flags) {
    struct process *pr;
    (void) flags;

    pr = t->t_proc;
    if (t->t_proc->p_pid == 1) { panic("init exiting with %d", code); }

    atomic_fetch_or(&t->t_flags, TF_EXITING, ATOMIC_ACQ_REL);

    pr_exit_debug("process (pid: %d, name: %s) exiting with %d\n", pr->p_pid, pr->p_name, code);

    BUG_ON(refcount_get(&pr->p_live_thrd_cnt) != 1); // unimplemented
    atomic_fetch_or(&pr->p_flags, PF_EXITING, ATOMIC_ACQ_REL);

    pr->p_state = PS_ZOMBIE;
    t->t_state  = TS_ZOMBIE;

    fdfree(t->t_proc);

    pr->p_xstatus = code;

    reparent_children(pr);
    schedule();

    __unreachable;
    hcf();
}

void exit_tail(struct thread *t) {
    BUG_ON(!list_is_empty(&t->t_qnode));
    list_add_tail(&t->t_qnode, &deadqueue);
    wakeup(&deadqueue);
}

int sys_wait3(struct thread *t, struct syscall_args *args, register_t *retval) {
    int flags = 0;

    if (args->arg3 & WAIT_NOHANG) { flags |= WAIT_NOHANG; }

    return do_wait(t, args->arg1, (int *) args->arg2, retval, flags);
}

static inline struct process *find_child(struct process *pr, pid_t pid, int flags) {
    struct process   *child;
    struct list_head *cur;
    (void) flags;

again:
    sleep_setup(pr, "wait");

    list_for_each(cur, &pr->p_children) {
        child = list_entry(cur, struct process, p_siblings);
        if (child->p_state == PS_ZOMBIE && atomic_load_explicit(&child->p_flags, ATOMIC_ACQUIRE) & PF_REALZOMBIE) {
            if (pid == (pid_t) -1 || child->p_pid == pid) {
                sleep_finish(0);
                return child;
            }
        }
    }

    if (flags & WAIT_NOHANG) { return NULL; }

    sleep_finish(1);
    goto again;
}

int do_wait(struct thread *t, pid_t pid, int *stat_loc, register_t *retval, int flags) {
    struct process *pr = t->t_proc;
    struct process *child;

    pr_wait_debug("pid %d: waiting on [pid: %d] with stat_loc %#p and flags %x\n", pr->p_pid, pid, stat_loc, flags);
    // we have no children we could wait for
    if (list_is_empty(&pr->p_children)) {
        pr_wait_debug("pid %d: no children to wait on\n", pr->p_pid);
        return -ECHILD;
    }

    child = find_child(pr, pid, flags);

    if (!child) {
        *retval = -1;
        return 0;
    }

    pr_wait_debug("pid %d: waited on [pid: %d] with exit status %d\n", pr->p_pid, child->p_pid, child->p_xstatus);

    if (copyout(stat_loc, (char *) &child->p_xstatus, sizeof(int))) { return -EFAULT; }
    *retval = child->p_pid;
    list_del(&child->p_siblings);

    free_proc(child);

    return 0;
}

void reaper(void *arg) {
    struct thread  *t;
    struct process *pr;
    (void) arg;

    for (;;) {
        while (list_is_empty(&deadqueue)) {
            sleep_setup(&deadqueue, "reaper");
            sleep_finish(list_is_empty(&deadqueue));
        }

        t = list_first_entry(&deadqueue, struct thread, t_qnode);
        list_del(&t->t_qnode);
        pr = t->t_proc;

        pm_free_page((phys_addr_t) __pa(t->t_kstack));
        vmspace_put(pr->p_mm);

        free_thread(t);

        if (atomic_load_explicit(&pr->p_flags, ATOMIC_ACQUIRE) & PF_NOZOMBIE) {
            proc_zap(pr);
        } else {
            atomic_fetch_or(&pr->p_flags, PF_REALZOMBIE, ATOMIC_ACQ_REL);
            wakeup(pr->p_parent);
        }
    }
}

void proc_zap(struct process *pr) {
    free_proc(pr);
}
