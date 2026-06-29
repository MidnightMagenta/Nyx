#include <nyx/ctype.h>
#include <nyx/string.h>

unsigned int strtou(const char *cp, char **endp, unsigned int base) {
    unsigned int res = 0, value;

    if (!base) {
        base = 10;
        if (*cp == '0') {
            base = 8;
            cp++;
            if ((*cp == 'x') && isxdigit(cp[1])) {
                base = 16;
                cp++;
            }
        }
    }

    while (isxdigit(*cp) &&
           (value = isdigit(*cp) ? *cp - '0' : (islower(*cp) ? toupper(*cp) : *cp) - 'A' + 10) < base) {
        res = res * base + value;
        cp++;
    }

    if (endp) *endp = (char *) cp;

    return res;
}
