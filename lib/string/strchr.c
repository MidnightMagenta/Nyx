#include <nyx/string.h>

const char *strchr(const char *s, int z) {
    while (*s != '\0') {
        if (*s == (char) z) { return s; }
        s++;
    }
    return NULL;
}

const char *strrchr(const char *s, int z) {
    const char *last = NULL;

    while (*s != '\0') {
        if (*s == (char) z) { last = s; }
        s++;
    }
    return last;
}
