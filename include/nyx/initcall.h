#ifndef _NYX_INITCALL_H
#define _NYX_INITCALL_H

#include <nyx/compiler.h>
#include <nyx/linkage.h>

typedef void (*initfn_t)();

#define DECL_INITCALL(fn) static const initfn_t __initfn_##fn __attribute__((section(".initcalls.early"), used)) = fn
#define DEFINE_INITCALL(fn)                                                                                            \
    static void fn();                                                                                                  \
    DECL_INITCALL(fn);                                                                                                 \
    static void __init fn()

#endif
