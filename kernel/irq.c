#include <mm/mm_types.h>
#include <mm/slab.h>
#include <nyx/errno.h>
#include <nyx/irq.h>
#include <nyx/linkage.h>
#include <nyx/list.h>
#include <nyx/panic.h>
#include <nyx/string.h>

#include <asi/irq_vectors.h>


static struct irq_desc irq_descs[NR_IRQS];
static kmem_cache_t   *irq_action_cache;

extern void arch_init_irq();

void __init init_irq() {
    arch_init_irq();
    irq_action_cache = kmem_create_cache("irq_action",
                                         sizeof(struct irq_action),
                                         _Alignof(struct irq_action),
                                         NULL,
                                         NULL,
                                         M_SLEEPOK);

    for (size_t i = 0; i < NR_IRQS; i++) {
        irq_descs[i].irq     = i;
        irq_descs[i].actions = (struct list_head) LIST_HEAD_INIT(irq_descs[i].actions);
    }
}

int irq_install_handler(unsigned int irq, irq_handler_func fn, const char *name, void *ctx) {
    struct irq_action *action;

    if (irq >= NR_IRQS) { return -EINVAL; }

    action = kmem_cache_alloc(irq_action_cache, 0);
    if (!action) { return -ENOMEM; }

    action->fn  = fn;
    action->ctx = ctx;
    strcpy(action->name, name);

    list_add(&action->list, &irq_descs[irq].actions);

    return 0;
}

int irq_remove_handler(unsigned int irq, irq_handler_func fn) {
    struct list_head  *cur;
    struct irq_action *action;

    if (irq >= NR_IRQS) { return -EINVAL; }
    if (list_is_empty(&irq_descs[irq].actions)) { return -ENOENT; }

    for (cur = irq_descs[irq].actions.next; cur->next != &irq_descs[irq].actions; cur = cur->next) {
        action = list_entry(cur, struct irq_action, list);
        if (action->fn == fn) {
            list_del(&action->list);
            kmem_cache_free(irq_action_cache, action);
            return 0;
        }
    }
    return -ENOENT;
}

extern void handle_irq(struct irq_desc *);

void irq_dispatch(unsigned int irq) {
    if (irq >= NR_IRQS) { panic("invalid irq number"); }

    handle_irq(&irq_descs[irq]);
}
