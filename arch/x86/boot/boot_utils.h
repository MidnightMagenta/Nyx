#ifndef _BOOT_UTILS_H
#define _BOOT_UTILS_H

#include "printb.h"
#include <nyx/stddef.h>

#define hcf()                                                                                                          \
    while (1) { __asm__ volatile("hlt"); }

#define pr_fmt(f) "boot: " f "\n"

#define pr_error(f, ...) printb("Error (%s:%d): " f, __FILE__, __LINE__, ##__VA_ARGS__)
#define pr_warn(f, ...)  printb("Warning: " f, ##__VA_ARGS__)
#define pr_info(f, ...)  printb("Info: " f, ##__VA_ARGS__)

#ifdef __DEBUG
#define pr_dbg(f, ...) printb(f, ##__VA_ARGS__)
#else
#define pr_dbg(f, ...)
#endif

#define max(a, b) (a > b ? a : b)
#define min(a, b) (a < b ? a : b)

int   memcmpb(const void *a, const void *b, size_t c);
void *memsetb(void *p, int v, size_t c);
void *memmoveb(void *dest, const void *src, size_t len);
void *memcpyb(void *dest, const void *src, size_t len);
int   strcmpb(const char *a, const char *b);

#endif
