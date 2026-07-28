#include <nyx/initcall.h>
#include <nyx/linkage.h>

extern initfn_t __initcalls_start[], __initcalls_end[];

void __init do_initcalls() {
    for (initfn_t *fn = __initcalls_start; fn < __initcalls_end; fn++) {
        if (fn) { (*fn)(); }
    }
}
