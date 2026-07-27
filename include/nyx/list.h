#ifndef _NYX_LIST_H
#define _NYX_LIST_H

#include <nyx/kernel.h>
#include <nyx/stddef.h>

#define LIST_HEAD_INIT(name) {&(name), &(name)}

#define LIST_HEAD(name) struct list_head name = LIST_HEAD_INIT(name)

struct list_head {
    struct list_head *next, *prev;
};

#define list_entry(ptr, type, member)       container_of(ptr, type, member)
#define list_first_entry(ptr, type, member) list_entry((ptr)->next, type, member)

static inline void list_init(struct list_head *entry) {
    entry->next = entry;
    entry->prev = entry;
}

static inline int list_is_head(const struct list_head *list, const struct list_head *head) {
    return list == head;
}

static inline void __list_add(struct list_head *new, struct list_head *prev, struct list_head *next) {
    next->prev = new;
    prev->next = new;
    new->next  = next;
    new->prev  = prev;
}

static inline void list_add(struct list_head *new, struct list_head *head) {
    __list_add(new, head, head->next);
}

static inline void list_add_tail(struct list_head *new, struct list_head *head) {
    __list_add(new, head->prev, head);
}

static inline void __list_del(struct list_head *prev, struct list_head *next) {
    next->prev = prev;
    prev->next = next;
}

static inline void list_del(struct list_head *entry) {
    __list_del(entry->prev, entry->next);
    entry->prev = entry;
    entry->next = entry;
}

static inline int list_is_empty(struct list_head *head) {
    return head->next == head;
}

static inline void list_replace(struct list_head *old, struct list_head *new) {
    new->next       = old->next;
    new->next->prev = new;
    new->prev       = old->prev;
    new->prev->next = new;
}

static inline void list_move(struct list_head *list, struct list_head *head) {
    __list_del(list->prev, list->next);
    list_add(list, head);
}

static inline void list_move_tail(struct list_head *list, struct list_head *head) {
    __list_del(list->prev, list->next);
    list_add_tail(list, head);
}

#define list_next_entry(pos, member) list_entry((pos)->member.next, typeof(*(pos)), member)

#define list_for_each(pos, head)         for (pos = (head)->next; pos != (head); pos = pos->next)
#define list_for_each_safe(pos, n, head) for (pos = (head)->next, n = pos->next; pos != (head); pos = n, n = pos->next)
#define list_entry_is_head(pos, head, member) list_is_head(&(pos)->member, (head))

#define list_for_each_entry(pos, head, member)                                                                         \
    for (pos = list_first_entry(head, typeof(*pos), member); !list_entry_is_head(pos, head, member);                   \
         pos = list_next_entry(pos, member))

#define list_for_each_entry_safe(pos, n, head, member)                                                                 \
    for (pos = list_first_entry(head, typeof(*pos), member), n = list_next_entry(pos, member);                         \
         !list_entry_is_head(pos, head, member);                                                                       \
         pos = n, n = list_next_entry(n, member))

#define list_entry(ptr, type, member)       container_of(ptr, type, member)
#define list_first_entry(ptr, type, member) list_entry((ptr)->next, type, member)

#endif
