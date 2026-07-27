#ifndef _STRING_H
#define _STRING_H

#include <nyx/compiler.h>
#include <nyx/stdarg.h>
#include <nyx/stddef.h>

// int    memcmp(const void *a, const void *b, size_t num);
void       *memcpy(void *restrict dest, const void *restrict src, size_t len);
void       *memset(void *ptr, int v, size_t num);
void       *memmove(void *dest, const void *src, size_t len);
int         memcmp(const void *a, const void *b, size_t len);
int         strcmp(const char *a, const char *b);
int         strncmp(const char *a, const char *b, size_t count);
const char *strchr(const char *s, int z);
const char *strrchr(const char *s, int z);
char       *strcpy(char *restrict dest, const char *restrict src);
char       *strncpy(char *restrict dest, const char *restrict src, size_t len);
size_t      strlcpy(char *restrict dest, const char *restrict src, size_t len);
size_t      strlen(const char *s);
size_t      strnlen(const char *s, size_t count);
int         vsprintf(char *buf, const char *fmt, va_list args);
int         sprintf(char *buf, const char *fmt, ...);

unsigned int strtou(const char *cp, char **endp, unsigned int base);

#endif
