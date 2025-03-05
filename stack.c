#include "stack.h"

#include <stdlib.h>
#include <stdio.h>

void stack_push(struct stack *l, void *v)
{
    struct stacknode *root = atomic_load(&l->root);
	struct stacknode *n = (struct stacknode *)malloc(sizeof(struct stacknode));
	n->value = v;
	n->next = root;
    n->removed = 0;
	n->next2 = root;
	while (!atomic_compare_exchange_weak(&l->root, &root, n)) {
        root = atomic_load(&l->root);
		n->next = root;
		n->next2 = root;
	}
    atomic_fetch_add(&l->count, 1);
}

bool stack_pop(struct stack *l, void **v)
{
	struct stacknode *first = 0;
    for (struct stacknode *n = atomic_load(&l->root); n; n = atomic_load(&n->next)) {
        int32_t zero = 0;
		if (atomic_compare_exchange_weak(&n->removed, &zero, 1)) {
			if (first)
				atomic_store(&first->next, n->next);
            atomic_fetch_sub(&l->count, 1);
            *v = n->value;
			return true;
		}
		if (!first)
			first = n;
	}
	return false;
}

void stack_print(struct stack *l)
{
    printf("[");
    for (struct stacknode *n = atomic_load(&l->root); n; n = atomic_load(&n->next)) {
        if (n == l->root) 
            printf("[ROOT]");
        printf("%x%s ", (uint64_t)n, n->removed ? "[UNLINK]" : "");
	}
    printf("]");
}

int stack_free(struct stack *l)
{
    int c = 0;
    for (struct stacknode *n = atomic_load(&l->root); n; ) {
        struct stacknode *next = atomic_load(&n->next2);
        free(n);
        n = next;
        c++;
	}
    return c;
}
