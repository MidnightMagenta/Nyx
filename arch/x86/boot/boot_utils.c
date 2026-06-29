#include "boot_utils.h"

int memcmpb(const void *a, const void *b, size_t c) {
    const unsigned char *aptr = (const unsigned char *) a;
    const unsigned char *bptr = (const unsigned char *) b;
    while (c--) {
        if (*aptr != *bptr) { return *aptr - *bptr; }
        aptr++;
        bptr++;
    }
    return 0;
}

void *memsetb(void *p, int v, size_t c) {
    unsigned char *strPtr = (unsigned char *) p;
    while (c--) { *strPtr++ = (unsigned char) v; }
    return p;
}

void *memmoveb(void *dest, const void *src, size_t len) {
    char       *d = dest;
    const char *s = src;
    if (d < s)
        while (len--) *d++ = *s++;
    else {
        char *lasts = (char *) (s + (len - 1));
        char *lastd = d + (len - 1);
        while (len--) *lastd-- = *lasts--;
    }
    return dest;
}

void *memcpyb(void *dest, const void *src, size_t len) {
    unsigned char       *dstPtr = (unsigned char *) dest;
    const unsigned char *srcPtr = (const unsigned char *) src;

    while (len--) { *dstPtr++ = *srcPtr++; }
    return dest;
}

int strcmpb(const char *a, const char *b) {
    unsigned long i   = 0;
    unsigned long res = 0;
    while ((a[i] == b[i]) && (a[i] != '\0') && (b[i] != '\0')) { i++; }
    res = ((unsigned char) a[i] - (unsigned char) b[i]);
    return (int) res;
}
