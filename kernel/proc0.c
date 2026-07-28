#include <mm/mm_types.h>
#include <nyx/atomic.h>
#include <nyx/list.h>
#include <nyx/proc.h>
#include <nyx/refcount.h>
#include <nyx/stddef.h>
#include <nyx/vfs.h>

extern char init_stack_top[];

struct vmspace  __proc0_vmspace;
struct filedesc __proc0_files;
struct thread   proc0;
struct process  proc0_proc;

void proc0_init() {
    __proc0_vmspace.v_pgd = NULL;
    refcount_init(&__proc0_vmspace.v_refcount, 1);
    list_init(&__proc0_vmspace.v_vmmap);

    atomic_store_explicit(&proc0.t_flags, 0, ATOMIC_RELAXED);
    proc0.t_state  = TS_RUNNING;
    proc0.t_kstack = init_stack_top;
    proc0.t_tid    = 0;
    proc0.t_proc   = &proc0_proc;
    proc0.t_wchan  = NULL;

    list_init(&proc0.t_qnode);
    list_init(&proc0.t_thrd_node);
    list_init(&proc0.t_gthrd_node);
    list_add_tail(&proc0.t_gthrd_node, &thread_list);

    atomic_store_explicit(&proc0_proc.p_flags, 0, ATOMIC_RELAXED);
    proc0_proc.p_state = PS_NORMAL;
    proc0_proc.p_mm    = &__proc0_vmspace;
    proc0_proc.p_pid   = 0;
    refcount_init(&proc0_proc.p_live_thrd_cnt, 1);
    proc0_proc.p_parent  = NULL;
    proc0_proc.p_xstatus = 0;

    fdinit(&proc0_proc);

    list_init(&proc0_proc.p_thrds_list);
    list_init(&proc0_proc.p_children);
    list_init(&proc0_proc.p_siblings);
    list_init(&proc0_proc.p_gproc_node);
    list_add_tail(&proc0.t_thrd_node, &proc0_proc.p_thrds_list);
    list_add_tail(&proc0_proc.p_gproc_node, &proc_list);
}
