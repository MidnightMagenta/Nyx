#include <mm/mm_types.h>
#include <mm/slab.h>
#include <nyx/linkage.h>
#include <nyx/list.h>
#include <nyx/proc.h>
#include <nyx/string.h>
#include <uapi/posix_types.h>

static kmem_cache_t *proc_struct_cache;
static kmem_cache_t *thread_struct_cache;

LIST_HEAD(proc_list);
LIST_HEAD(thread_list);

void __init proc_init() {
    proc_struct_cache =
            kmem_create_cache("proc_struct", sizeof(struct process), _Alignof(struct process), NULL, NULL, M_SLEEPOK);
    thread_struct_cache =
            kmem_create_cache("thrd_struct", sizeof(struct thread), _Alignof(struct thread), NULL, NULL, M_SLEEPOK);
}

struct process *alloc_proc(int gfp_flags) {
    return kmem_cache_alloc(proc_struct_cache, gfp_flags);
}

struct thread *alloc_thread(int gfp_flags) {
    return kmem_cache_alloc(thread_struct_cache, gfp_flags);
}

void free_proc(struct process *pr) {
    put_pid(pr->p_pid);
    list_del(&pr->p_siblings);
    list_del(&pr->p_gproc_node);
    kmem_cache_free(proc_struct_cache, pr);
}

void free_thread(struct thread *thrd) {
    put_tid(thrd->t_tid);
    list_del(&thrd->t_thrd_node);
    list_del(&thrd->t_gthrd_node);
    kmem_cache_free(thread_struct_cache, thrd);
}

// TODO: PID recycling
static pid_t cur_pid = 1;
static pid_t cur_tid = 1;

pid_t get_pid() {
    return cur_pid++;
}

void put_pid(pid_t pid) {
    (void) pid;
}

pid_t get_tid() {
    return cur_tid++;
}

void put_tid(pid_t tid) {
    (void) tid;
}
