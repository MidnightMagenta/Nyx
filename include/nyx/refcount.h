#ifndef _NYX_REFCOUNT_H
#define _NYX_REFCOUNT_H

#include <nyx/atomic.h>

struct refcount {
    atomic_int_t __count;
};

static inline void refcount_init(struct refcount *rc, int v) {
    atomic_store_explicit(&rc->__count, v, ATOMIC_RELEASE);
}

static inline void refcount_set(struct refcount *rc, int v) {
    atomic_store_explicit(&rc->__count, v, ATOMIC_RELEASE);
}

static inline int refcount_get_inc(struct refcount *rc) {
    return atomic_fetch_add(&rc->__count, 1, ATOMIC_ACQ_REL);
}

static inline int refcount_get_dec(struct refcount *rc) {
    return atomic_fetch_sub(&rc->__count, 1, ATOMIC_ACQ_REL);
}

static inline void refcount_inc(struct refcount *rc) {
    atomic_fetch_add(&rc->__count, 1, ATOMIC_RELEASE);
}

static inline void refcount_dec(struct refcount *rc) {
    atomic_fetch_sub(&rc->__count, 1, ATOMIC_RELEASE);
}

static inline int refcount_get(struct refcount *rc) {
    return atomic_load_explicit(&rc->__count, ATOMIC_ACQUIRE);
}

static inline bool refcount_dec_and_test(struct refcount *rc) {
    return refcount_get_dec(rc) == 1;
}

#endif
