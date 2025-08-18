#include "stack.h"

#include <stdlib.h>
#include <stdio.h>

void stack_push(struct stack *l, struct stacknode *n)
{
    struct stacknode *root = atomic_load(&l->root);
	n->next = root;
	while (!atomic_compare_exchange_weak(&l->root, &root, n)) {
        root = atomic_load(&l->root);
		n->next = root;
	}
    atomic_fetch_add(&l->count, 1);
}

struct stacknode *stack_pop(struct stack *l)
{
    while (1) {
        struct stacknode *n = atomic_load(&l->root);
        if (!n)
            break;
        if (atomic_compare_exchange_weak(&l->root, &n, n->next))
            return n;
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
