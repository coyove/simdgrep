#ifndef _HEADER_S
#define _HEADER_S

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>

#define FOREACH_STACK(s, n) \
    for (struct stacknode *n = (struct stacknode *)(atomic_load(&(s)->root) << 1 >> 1), *n##next = 0; \
            (n##next = n ? (struct stacknode *)(atomic_load(&n->next) << 1 >> 1) : 0, n); n = n##next)

struct stacknode {
    uint64_t _Atomic next;
};

struct stack {
    _Atomic int64_t count;
    uint64_t _Atomic root;
};

void stack_push(struct stack *, struct stacknode *);

struct stacknode *stack_pop(struct stack *);

size_t stack_free(struct stack *);

#endif

