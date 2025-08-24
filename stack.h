#ifndef _HEADER_S
#define _HEADER_S

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>

#define STACK_NEXT ((1ull << 56) - 1)

#define FOREACH_STACK(s, n) \
    for (struct stacknode *n = (struct stacknode *)(atomic_load(&(s)->root) & STACK_NEXT), *n##next = NULL; \
            (n##next = n ? (struct stacknode *)(atomic_load(&n->next) & STACK_NEXT) : NULL, n); n = n##next)

struct stacknode {
    _Atomic uint64_t next;
};

struct stack {
    _Atomic int64_t count;
    _Atomic uint64_t root;
};

void stack_push(uint8_t tid, struct stack *l, struct stacknode *n);

struct stacknode *stack_pop(uint8_t tid, struct stack *);

size_t stack_free(struct stack *);

#endif

