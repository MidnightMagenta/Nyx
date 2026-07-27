#ifndef _NYX_ATOMIC_H
#define _NYX_ATOMIC_H

#define ATOMIC_RELAXED __ATOMIC_RELAXED
#define ATOMIC_CONSUME __ATOMIC_CONSUME
#define ATOMIC_ACQUIRE __ATOMIC_ACQUIRE
#define ATOMIC_RELEASE __ATOMIC_RELEASE
#define ATOMIC_ACQ_REL __ATOMIC_ACQ_REL
#define ATOMIC_SEQ_CST __ATOMIC_SEQ_CST

#define __ATOMIC(T)                                                                                                    \
    struct {                                                                                                           \
        volatile T __val;                                                                                              \
    }

typedef __ATOMIC(bool) atomic_bool_t;
typedef __ATOMIC(char) atomic_char_t;
typedef __ATOMIC(int) atomic_int_t;
typedef __ATOMIC(unsigned int) atomic_uint_t;
typedef __ATOMIC(long) atomic_long_t;
typedef __ATOMIC(unsigned long) atomic_ulong_t;
typedef __ATOMIC(long long) atomic_llong_t;
typedef __ATOMIC(unsigned long long) atomic_ullong_t;

typedef atomic_int_t atomic_t;

#define atomic_test_and_set(aptr, memorder) __atomic_test_and_set(&(aptr)->__val, memorder)
#define atomic_clear(aptr, memorder)        __atomic_clear(&(aptr)->__val, memorder)

#define atomic_load_explicit(aptr, memorder)     __atomic_load_n(&(aptr)->__val, (memorder))
#define atomic_load(aptr)                        atomic_load_explicit(aptr, ATOMIC_ACQUIRE)
#define atomic_store_explicit(aptr, v, memorder) __atomic_store_n(&(aptr)->__val, (v), (memorder))
#define atomic_store(aptr, v)                    atomic_store_explicit(aptr, v, ATOMIC_RELEASE)

#define atomic_fetch_add(aptr, diff, memorder) __atomic_fetch_add(&(aptr)->__val, (diff), (memorder))
#define atomic_fetch_sub(aptr, diff, memorder) __atomic_fetch_sub(&(aptr)->__val, (diff), (memorder))
#define atomic_fetch_and(aptr, val, memorder)  __atomic_fetch_and(&(aptr)->__val, (val), (memorder))
#define atomic_fetch_xor(aptr, val, memorder)  __atomic_fetch_xor(&(aptr)->__val, (val), (memorder))
#define atomic_fetch_or(aptr, val, memorder)   __atomic_fetch_or(&(aptr)->__val, (val), (memorder))
#define atomic_fetch_nand(aptr, val, memorder) __atomic_fetch_nand(&(aptr)->__val, (val), (memorder))

#define atomic_fetch_and_raw(vptr, val, memorder) __atomic_fetch_and(vptr, val, memorder)

#define atomic_inc_e(aptr, memorder) atomic_fetch_add(aptr, 1, memorder)
#define atomic_dec_e(aptr, memorder) atomic_fetch_sub(aptr, 1, memorder)

#define atomic_thread_fence(memorder) __atomic_thread_fence(memorder)

#define refcnt_inc(aptr) atomic_inc_e(aptr, ATOMIC_RELAXED)
#define refcnt_dec(aptr) atomic_dec_e(aptr, ATOMIC_ACQ_REL)

#endif
