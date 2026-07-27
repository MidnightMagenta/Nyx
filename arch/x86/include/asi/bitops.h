#ifndef _ASI_BITOPS_H
#define _ASI_BITOPS_H

static inline int test_bit(int nr, void *addr) {
    return ((1ull << (nr & 63)) & (((const unsigned long long *) addr)[nr >> 6])) != 0;
}

static inline void set_bit(int nr, void *addr) {
    ((unsigned long long *) addr)[nr >> 6] |= (1ull << (nr & 63));
}

static inline void clear_bit(int nr, void *addr) {
    ((unsigned long long *) addr)[nr >> 6] &= ~(1ull << (nr & 63));
}

#define BIT(n) (1ull << n)

#define __ilog2i(x)  (31 - __builtin_clz(x))
#define __ilog2l(x)  (63 - __builtin_clzl(x))
#define __ilog2ll(x) (63 - __builtin_clzll(x))

#define ilog2(x)  _Generic((x), unsigned int: __ilog2i(x), unsigned long: __ilog2l(x), unsigned long long: __ilog2ll(x))
#define cilog2(x) ((x) <= 1 ? 0 : ilog2((x) - 1) + 1)

#endif
