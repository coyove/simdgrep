#include "stack.h"

#include <stdlib.h>
#include <stdio.h>

void stack_push(struct stack *l, struct stacknode *n)
{
    uint64_t root = 0, mark = 1ull << 63;
    if (!atomic_compare_exchange_weak(&n->next, &root, mark)) {
        // This node already exists in this (or another) stack.
        return;
	}
RETRY:
    root = atomic_load(&l->root);
	atomic_store(&n->next, mark | root);
	if (!atomic_compare_exchange_weak(&l->root, &root, (uint64_t)n)) {
        goto RETRY;
	}
    atomic_fetch_add(&l->count, 1);
}

struct stacknode *stack_pop(struct stack *l)
{
    while (1) {
        uint64_t root = atomic_load(&l->root), n = root << 1 >> 1;
        if (!n)
            break;
        struct stacknode *nn = (struct stacknode *)n;
        if (atomic_compare_exchange_weak(&l->root, &root, nn->next)) {
            atomic_fetch_add(&l->count, -1);
            atomic_store(&nn->next, 0);
            return nn;
        }
    }
	return NULL;
}

size_t stack_free(struct stack *s)
{
    size_t c = 0;
    FOREACH_STACK(s, n) {
        free(n);
        c++;
	}
    return c;
}
