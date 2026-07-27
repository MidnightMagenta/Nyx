#ifndef _NYX_SPINLOCK_H
#define _NYX_SPINLOCK_H

#include <nyx/atomic.h>
#include <nyx/types.h>

typedef struct spinlock {
    atomic_bool_t __locked;
    flags_t       __old_flags;
} spinlock_t;

static inline void spinlock_init(spinlock_t *l) {
    atomic_clear(&l->__locked, ATOMIC_SEQ_CST);
}

#endif
