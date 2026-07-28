#include <fs/cpio.h>
#include <mm/mm_types.h>
#include <mm/vmspace.h>
#include <nyx/current.h>
#include <nyx/fcntl.h>
#include <nyx/kernel.h>
#include <nyx/kthread.h>
#include <nyx/linkage.h>
#include <nyx/panic.h>
#include <nyx/sched.h>
#include <nyx/stddef.h>
#include <nyx/string.h>
#include <nyx/vfs.h>
#include <nyx/wait.h>

#include <asi/bootparam.h>
#include <asi/bug.h>
#include <asi/irq.h>

#ifdef CONFIG_KERNEL_TESTS
extern void __do_kernel_tests();
#else
#define __do_kernel_tests()
#endif

static void __noreturn __idle_task_fn() {
    for (;;) { asm volatile("hlt"); }
}

extern void setup_arch();
extern void init_memory();
extern void init_irq();
extern void init_timer();
extern void init_sched();
extern void init_vfs();
extern void init_filesystems();
extern void do_initcalls();
extern void reaper(void *arg);
void        init_proc(void *arg);

extern struct thread proc0;
struct thread       *initproc;

static __init void mount_root() {
    struct cpio_args args = {
            .base = __va(get_initramfs()),
            .len  = get_initramfs_len(),
    };
    BUG_ON(vfs_mountroot("cpio", &args));
}

static void __init mount_devfs() {
    struct nameidata nd = {
            .ni_dirp   = "/dev",
            .ni_segflg = UIO_SYSSPACE,
            .ni_op     = NAMEI_LOOKUP,
            .ni_flags  = 0,
            .ni_proc   = current()->proc,
    };

    BUG_ON(namei(&nd));
    BUG_ON(do_mount("devfs", nd.ni_vp, NULL, NULL));
}

static __init void start_init() {
    int stdinfd, stdoutfd, stderrfd;
    if (do_fork(&proc0, FORK_NOZOMBIE | FORK_SHAREVM, &init_proc, NULL, NULL, &initproc) != 0) {
        panic("failed to start init");
    }
    strncpy(initproc->proc->name, "init", PROC_NAME_LEN);

    BUG_ON(vfs_open(initproc->proc, "/dev/console", UIO_SYSSPACE, O_RDWR, 0, &stdinfd));
    BUG_ON(kern_dup(initproc->proc, stdinfd, &stdoutfd));
    BUG_ON(kern_dup(initproc->proc, stdinfd, &stderrfd));
    BUG_ON(stdinfd != 0 || stdoutfd != 1 || stderrfd != 2);
}

void __init start_kernel() {
    pr_info("kernel build ID: %s\n", NYX_BUILD_ID);
    setup_arch();
    init_memory();
    init_irq();
    init_vfs();
    init_filesystems();
    mount_root();
    init_timer();
    init_sched();
    mount_devfs();
    do_initcalls();

    struct nameidata nd;
    nd.ni_dirp   = "/../../test/path/a/../a/././..///../path/a//testfile.txt";
    nd.ni_segflg = UIO_SYSSPACE;
    nd.ni_op     = NAMEI_LOOKUP;
    nd.ni_flags  = 0;
    nd.ni_proc   = current()->proc;

    if (!namei(&nd)) {
        printk("Found vnode: %#p\n", nd.ni_vp);
    } else {
        printk("namei failed :(\n");
    }

    char buf[256];
    memset(buf, 0, 256);

    vn_rdwr(UIO_READ, nd.ni_vp, buf, 255, 0, NULL);

    printk("%s\n", buf);

    __do_kernel_tests();

    pr_dbg("finish\n");

    start_init();
    kthread_create(reaper, NULL, "procreaper");

    arch_irq_enable();

    __idle_task_fn();

    BUG();
}

#include "fudgeasm.h"
#include <asi/gdt.h>

void fudge_exec() {
    struct thread  *t  = current();
    struct process *pr = t->proc;
    struct vmspace *mm = vmspace_new(pr);
    struct vmspace *oldmm;
    flags_t         flags;

    struct trap_frame *tf = thread_trap_frame(t);
    tf->frame.rip         = 0x10000;
    tf->frame.cs          = USER_CODE64_SEGMENT;
    tf->frame.ss          = USER_DATA_SEGMENT;
    tf->frame.rflags      = 0x202;

    vmspace_mapcopy(mm,
                    0x10000,
                    init_fudgeasm_bin,
                    init_fudgeasm_bin_len,
                    VM_EXEC | VM_READ | VM_WRITE | VM_USER,
                    M_SLEEPOK);

    oldmm = pr->mm;

    flags = arch_irq_save();
    vmspace_activate(mm);
    pr->mm = mm;
    arch_irq_restore(flags);

    vmspace_put(oldmm);
}

void init_proc(void *arg) {
    (void) arg;
    fudge_exec();
    return;
}
