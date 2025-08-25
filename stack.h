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

#define STACK_FREE_VARARG(_1,_2,NAME,...) NAME

#define STACK_FREE(s) do { FOREACH_STACK(s, n) free(n); } while(0); 

#define STACK_FREE2(s, f) do { FOREACH_STACK(s, n) f(n); } while(0); 

#define stack_free(...) STACK_FREE_VARARG(__VA_ARGS__, STACK_FREE2, STACK_FREE)(__VA_ARGS__)

#endif

