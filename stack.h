#ifndef _HEADER_S
#define _HEADER_S

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

#define FOREACH_STACK(s, n) \
    for (struct stacknode *n = atomic_load(&(s)->root), *n##next = 0; \
            (n##next = n ? atomic_load(&n->next) : 0, n); n = n##next)

struct stacknode {
    struct stacknode * _Atomic next;
};

struct stack {
    _Atomic int64_t count;
    struct stacknode * _Atomic root;
};

void stack_push(struct stack *, struct stacknode *);

struct stacknode *stack_pop(struct stack *);

size_t stack_free(struct stack *);

#endif

