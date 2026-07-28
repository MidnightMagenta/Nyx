#include <nyx/percpu.h>
#include <nyx/types.h>

struct percpu *get_pcpu() {
    u64 pcpu_addr;
    asm volatile("mov %%gs:0, %0"
                 : "=r"(pcpu_addr));
    return (struct percpu *) pcpu_addr;
}
