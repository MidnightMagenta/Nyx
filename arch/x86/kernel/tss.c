#include <asi/page.h>
#include <asi/tss.h>
#include <nyx/types.h>

char ist1[PAGE_SIZE];

struct tss default_tss = {
        0,
        {0, 0, 0},
        0,
        {(u64) &ist1 + PAGE_SIZE, 0, 0, 0, 0, 0, 0},
        0,
        0,
        sizeof(struct tss),
};
