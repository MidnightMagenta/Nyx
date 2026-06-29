#include <nyx/string.h>

int memcmp(const void *a, const void *b, size_t len) {
    const unsigned char *ap, *bp;
    ap = (const unsigned char *) a;
    bp = (const unsigned char *) b;

    while (len--) {
        if (*ap++ != *bp++) { return ap[-1] < bp[-1] ? -1 : 1; }
    }
    return 0;
}
