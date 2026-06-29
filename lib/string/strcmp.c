#include <nyx/string.h>

int strcmp(const char *a, const char *b) {
    size_t i   = 0;
    size_t res = 0;
    while ((a[i] == b[i]) && (a[i] != '\0') && (b[i] != '\0')) { i++; }
    res = ((unsigned char) a[i] - (unsigned char) b[i]);
    return (int) res;
}

int strncmp(const char *a, const char *b, size_t count) {
    unsigned char ap, bp;

    while (count--) {
        ap = (unsigned char) *a++;
        bp = (unsigned char) *b++;

        if (ap != bp) { return ap - bp; }
        if (ap == '\0') { return 0; }
    }

    return 0;
}
