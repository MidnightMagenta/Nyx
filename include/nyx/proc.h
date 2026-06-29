#ifndef _NYX_PROC_H
#define _NYX_PROC_H

#include <mm/mm_types.h>
#include <nyx/atomic.h>
#include <nyx/vfs.h>
#include <uapi/posix_types.h>

#include <asi/cpu.h>
#include <asi/page.h>
#include <asi/proc.h>

#define PROC_NAME_LEN 32

struct process;
struct thread;

struct thread {
    atomic_ulong_t flags;

    struct list_head qnode;

    enum thread_state {
        TS_NEW,
        TS_RUNNABLE,
        TS_RUNNING,
        TS_SLEEPING,
        TS_ZOMBIE,
    } state;

    struct thread_context ctx;
    void                 *kstack;
    pid_t                 tid;
    struct process       *proc;
    const volatile void  *wchan;
    const char           *wmesg;
    struct cpu_info      *cpu;

    struct list_head thrd_node;
    struct list_head gthrd_node;
};

#define TF_EXITING (1 << 2)

struct process {
    atomic_ulong_t flags;

    enum proc_state {
        PS_NEW,
        PS_NORMAL,
        PS_ZOMBIE,
    } state;

    struct vmspace  *mm;
    pid_t            pid;
    struct refcount  live_thrd_cnt;
    struct list_head thrds_list;
    struct process  *parent;
    struct list_head children;
    struct list_head siblings;
    int              xstatus;

    struct vnode *cwd;
    struct files *files;

    struct list_head gproc_node;
    char             name[PROC_NAME_LEN];
};

#define PF_NOZOMBIE   (1 << 0)
#define PF_EMBRYO     (1 << 1)
#define PF_EXITING    (1 << 2)
#define PF_REALZOMBIE (1 << 3)

extern struct list_head proc_list;
extern struct list_head thread_list;
extern struct thread    proc0;
extern struct thread   *initproc;

static inline void thread_set_state(struct thread *t, enum thread_state state) {
    t->state = state;
}

static inline void proc_set_state(struct process *p, enum proc_state state) {
    p->state = state;
}

struct process *alloc_proc(int gfp_flags);
struct thread  *alloc_thread(int gfp_flags);
void            free_proc(struct process *pr);
void            free_thread(struct thread *thrd);

pid_t get_pid();
void  put_pid(pid_t pid);
pid_t get_tid();
void  put_tid(pid_t tid);

#define FORK_FORK     (1 << 0)
#define FORK_VFORK    (1 << 1)
#define FORK_NOZOMBIE (1 << 2)
#define FORK_SHAREVM  (1 << 3)
#define FORK_SHAREFD  (1 << 4)

#define EXIT_NORMAL (1 << 0)
#define EXIT_THREAD (1 << 1)

#define WAIT_NOHANG (1 << 0)

int  do_fork(struct thread  *curp,
             int             flags,
             void            (*func)(void *),
             void           *arg,
             register_t     *retval,
             struct thread **newproc);
void do_exit(struct thread *t, int code, int flags);
int  do_wait(struct thread *t, pid_t pid, int *stat_loc, register_t *retval, int flags);

static inline struct trap_frame *thread_trap_frame(struct thread *t) {
    return (struct trap_frame *) ((char *) t->kstack + PAGE_SIZE) - 1;
}

#endif
