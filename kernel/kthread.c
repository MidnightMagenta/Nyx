#include <nyx/kthread.h>
#include <nyx/proc.h>
#include <nyx/string.h>

#include <asi/bug.h>

extern struct thread proc0;

int kthread_create(void (*entry)(void *), void *arg, const char *name) {
    int            res;
    struct thread *t;

    if ((res = do_fork(&proc0, FORK_NOZOMBIE | FORK_SHAREVM, entry, arg, NULL, &t))) { return res; }

    strncpy(t->proc->name, name, PROC_NAME_LEN);
    return 0;
}

void kthread_exit() {
    BUG();
}
